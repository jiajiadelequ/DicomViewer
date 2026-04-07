#include "studyloader.h"

#include "casepackagereader.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QTemporaryFile>
#include <QTextStream>
#include <gdcmException.h>
#include <QStringList>

#include <vtkImageData.h>
#include <vtkImageShiftScale.h>
#include <vtkMatrix3x3.h>
#include <vtkMatrix4x4.h>
#include <vtkNIFTIImageHeader.h>
#include <vtkNIFTIImageReader.h>
#include <vtkOBJReader.h>
#include <vtkPLYReader.h>
#include <vtkPointData.h>
#include <vtkPolyData.h>
#include <vtkPolyDataReader.h>
#include <vtkSTLReader.h>
#include <vtkXMLPolyDataReader.h>

#include <itkGDCMImageIO.h>
#include <itkGDCMSeriesFileNames.h>
#include <itkImage.h>
#include <itkImageSeriesReader.h>
#include <itkImageToVTKImageFilter.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace
{
using VolumePixelType = short;
constexpr unsigned int VolumeDimension = 3;
using VolumeImageType = itk::Image<VolumePixelType, VolumeDimension>;
using VolumeReaderType = itk::ImageSeriesReader<VolumeImageType>;
using VolumeNamesGeneratorType = itk::GDCMSeriesFileNames;
using VolumeImageIOType = itk::GDCMImageIO;
using VolumeConnectorType = itk::ImageToVTKImageFilter<VolumeImageType>;

Q_LOGGING_CATEGORY(lcStudyLoadDiagnostics, "dicomviewer.studyloader.diagnostics", QtWarningMsg)

bool isCancelled(const StudyLoadFeedback *feedback)
{
    return feedback != nullptr && feedback->isCancelled && feedback->isCancelled();
}

void reportProgress(const StudyLoadFeedback *feedback, const QString &message, int percent)
{
    if (feedback != nullptr && feedback->reportProgress) {
        feedback->reportProgress(message, percent);
    }
}

StudyLoadResult cancelledResult()
{
    StudyLoadResult result;
    result.cancelled = true;
    return result;
}

bool isNiftiFilePath(const QString &filePath)
{
    const QString normalizedPath = QDir::fromNativeSeparators(filePath).toLower();
    return normalizedPath.endsWith(QStringLiteral(".nii"))
        || normalizedPath.endsWith(QStringLiteral(".nii.gz"));
}

bool nearlyEqual(double lhs, double rhs, double epsilon = 1e-8)
{
    return std::abs(lhs - rhs) <= epsilon;
}

struct ObjMaterialReference
{
    QString mtllibPath;
    QString materialName;
    bool requiresSanitizedLoad = false;
};

ObjMaterialReference inspectObjMaterialReference(const QString &objPath)
{
    ObjMaterialReference reference;
    QFile file(objPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return reference;
    }

    QTextStream stream(&file);
    const QDir baseDir = QFileInfo(objPath).absoluteDir();
    while (!stream.atEnd()) {
        const QString line = stream.readLine().trimmed();
        if (line.startsWith(QStringLiteral("mtllib "))) {
            if (reference.mtllibPath.isEmpty()) {
                const QString relativePath = line.mid(7).trimmed();
                if (!relativePath.isEmpty()) {
                    reference.mtllibPath = QDir::toNativeSeparators(baseDir.filePath(relativePath));
                }
            }
            reference.requiresSanitizedLoad = true;
        } else if (line.startsWith(QStringLiteral("usemtl "))) {
            if (reference.materialName.isEmpty()) {
                reference.materialName = line.mid(7).trimmed();
            }
            reference.requiresSanitizedLoad = true;
        }
    }
    return reference;
}

bool writeSanitizedObjFile(const QString &objPath, QTemporaryFile *temporaryFile)
{
    if (temporaryFile == nullptr) {
        return false;
    }

    QFile file(objPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream input(&file);
    QTextStream output(temporaryFile);
    while (!input.atEnd()) {
        const QString line = input.readLine();
        if (line.startsWith(QStringLiteral("mtllib "))) {
            continue;
        }
        if (line.startsWith(QStringLiteral("usemtl "))) {
            continue;
        }
        output << line << '\n';
    }
    output.flush();
    temporaryFile->flush();
    return temporaryFile->error() == QFileDevice::NoError;
}

LoadedModelData::MaterialData parseMtlMaterial(const QString &mtlPath, const QString &targetMaterialName)
{
    LoadedModelData::MaterialData material;
    if (mtlPath.isEmpty()) {
        return material;
    }

    QFile file(mtlPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return material;
    }

    QTextStream stream(&file);
    QString currentMaterialName;
    bool inTargetBlock = false;
    bool seenAnyMaterial = false;

    auto matchesTarget = [&targetMaterialName](const QString &name) {
        return targetMaterialName.isEmpty() || name == targetMaterialName;
    };

    while (!stream.atEnd()) {
        const QString rawLine = stream.readLine().trimmed();
        if (rawLine.isEmpty() || rawLine.startsWith(QLatin1Char('#'))) {
            continue;
        }

        const QStringList parts = rawLine.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (parts.isEmpty()) {
            continue;
        }

        const QString keyword = parts.front();
        if (keyword == QStringLiteral("newmtl")) {
            currentMaterialName = rawLine.mid(6).trimmed();
            inTargetBlock = matchesTarget(currentMaterialName);
            seenAnyMaterial = true;
            if (!targetMaterialName.isEmpty() && material.hasMaterial && !inTargetBlock) {
                break;
            }
            continue;
        }

        if (!inTargetBlock) {
            continue;
        }

        if ((keyword == QStringLiteral("Kd") || keyword == QStringLiteral("Ka")) && parts.size() >= 4) {
            material.color[0] = parts[1].toDouble();
            material.color[1] = parts[2].toDouble();
            material.color[2] = parts[3].toDouble();
            material.hasMaterial = true;
        } else if (keyword == QStringLiteral("Ks") && parts.size() >= 4) {
            material.specularColor[0] = parts[1].toDouble();
            material.specularColor[1] = parts[2].toDouble();
            material.specularColor[2] = parts[3].toDouble();
            material.specularStrength = std::clamp((material.specularColor[0]
                                                    + material.specularColor[1]
                                                    + material.specularColor[2]) / 3.0,
                                                   0.0,
                                                   1.0);
            material.hasMaterial = true;
        } else if (keyword == QStringLiteral("Ns") && parts.size() >= 2) {
            material.specularPower = std::max(1.0, parts[1].toDouble());
            material.hasMaterial = true;
        } else if (keyword == QStringLiteral("d") && parts.size() >= 2) {
            material.opacity = std::clamp(parts[1].toDouble(), 0.0, 1.0);
            material.hasMaterial = true;
        } else if (keyword == QStringLiteral("Tr") && parts.size() >= 2) {
            material.opacity = std::clamp(1.0 - parts[1].toDouble(), 0.0, 1.0);
            material.hasMaterial = true;
        }
    }

    if (!material.hasMaterial && targetMaterialName.isEmpty() && seenAnyMaterial) {
        file.seek(0);
        stream.seek(0);
        QString fallbackMaterial;
        while (!stream.atEnd()) {
            const QString rawLine = stream.readLine().trimmed();
            if (rawLine.startsWith(QStringLiteral("newmtl "))) {
                fallbackMaterial = rawLine.mid(6).trimmed();
                break;
            }
        }
        if (!fallbackMaterial.isEmpty()) {
            return parseMtlMaterial(mtlPath, fallbackMaterial);
        }
    }

    return material;
}

LoadedModelData::MaterialData inferDefaultMaterialFromFilePath(const QString &filePath)
{
    LoadedModelData::MaterialData material;
    const QString name = QFileInfo(filePath).completeBaseName().toLower();

    auto setColor = [&material](double r, double g, double b,
                                double opacity = 1.0,
                                double specular = 0.08,
                                double specularPower = 8.0) {
        material.color[0] = r;
        material.color[1] = g;
        material.color[2] = b;
        material.opacity = opacity;
        material.specularStrength = specular;
        material.specularPower = specularPower;
        material.hasMaterial = true;
    };

    // Reference anchor:
    // Slicer's GenericAnatomyColors uses warm orange for generic organs and red for blood.
    // Organ-specific hues below are an inference to match common medical illustration practice.
    if (name == QStringLiteral("liver")) {
        setColor(0.62, 0.29, 0.23);
    } else if (name == QStringLiteral("spleen")) {
        setColor(0.45, 0.16, 0.33);
    } else if (name == QStringLiteral("pancreas")) {
        setColor(0.92, 0.72, 0.44);
    } else if (name == QStringLiteral("kidney_left") || name == QStringLiteral("kidney_right")) {
        setColor(0.58, 0.22, 0.20);
    } else if (name == QStringLiteral("adrenal_gland_left") || name == QStringLiteral("adrenal_gland_right")) {
        setColor(0.90, 0.78, 0.46);
    } else if (name == QStringLiteral("gallbladder")) {
        setColor(0.20, 0.53, 0.24);
    } else if (name == QStringLiteral("stomach")) {
        setColor(0.84, 0.56, 0.63);
    } else if (name == QStringLiteral("duodenum")) {
        setColor(0.93, 0.76, 0.58);
    } else if (name == QStringLiteral("small_bowel")) {
        setColor(0.94, 0.80, 0.62);
    } else if (name == QStringLiteral("colon")) {
        setColor(0.78, 0.66, 0.50);
    } else if (name == QStringLiteral("aorta")) {
        setColor(0.78, 0.12, 0.12, 1.0, 0.18, 18.0);
    } else if (name == QStringLiteral("inferior_vena_cava")) {
        setColor(0.20, 0.34, 0.72, 1.0, 0.14, 14.0);
    } else if (name == QStringLiteral("portal_vein_and_splenic_vein")) {
        setColor(0.28, 0.46, 0.76, 1.0, 0.14, 14.0);
    } else {
        setColor(0.87, 0.51, 0.40);
    }

    return material;
}

QString formatItkDirection(const VolumeImageType::DirectionType &direction)
{
    return QStringLiteral("[[%1, %2, %3], [%4, %5, %6], [%7, %8, %9]]")
        .arg(direction(0, 0), 0, 'f', 6)
        .arg(direction(0, 1), 0, 'f', 6)
        .arg(direction(0, 2), 0, 'f', 6)
        .arg(direction(1, 0), 0, 'f', 6)
        .arg(direction(1, 1), 0, 'f', 6)
        .arg(direction(1, 2), 0, 'f', 6)
        .arg(direction(2, 0), 0, 'f', 6)
        .arg(direction(2, 1), 0, 'f', 6)
        .arg(direction(2, 2), 0, 'f', 6);
}

QString formatVtkDirection(vtkImageData *imageData)
{
    auto *directionMatrix = imageData != nullptr ? imageData->GetDirectionMatrix() : nullptr;
    if (directionMatrix == nullptr) {
        return QStringLiteral("<null>");
    }

    return QStringLiteral("[[%1, %2, %3], [%4, %5, %6], [%7, %8, %9]]")
        .arg(directionMatrix->GetElement(0, 0), 0, 'f', 6)
        .arg(directionMatrix->GetElement(0, 1), 0, 'f', 6)
        .arg(directionMatrix->GetElement(0, 2), 0, 'f', 6)
        .arg(directionMatrix->GetElement(1, 0), 0, 'f', 6)
        .arg(directionMatrix->GetElement(1, 1), 0, 'f', 6)
        .arg(directionMatrix->GetElement(1, 2), 0, 'f', 6)
        .arg(directionMatrix->GetElement(2, 0), 0, 'f', 6)
        .arg(directionMatrix->GetElement(2, 1), 0, 'f', 6)
        .arg(directionMatrix->GetElement(2, 2), 0, 'f', 6);
}

void applyItkDirectionToVtkImage(VolumeImageType *itkImage, vtkImageData *vtkImage)
{
    if (itkImage == nullptr || vtkImage == nullptr) {
        return;
    }

    auto directionMatrix = vtkSmartPointer<vtkMatrix3x3>::New();
    const auto &itkDirection = itkImage->GetDirection();
    for (unsigned int row = 0; row < VolumeDimension; ++row) {
        for (unsigned int column = 0; column < VolumeDimension; ++column) {
            directionMatrix->SetElement(static_cast<int>(row),
                                        static_cast<int>(column),
                                        itkDirection(row, column));
        }
    }

    vtkImage->SetDirectionMatrix(directionMatrix);
}

vtkSmartPointer<vtkMatrix4x4> buildNiftiInternalMatrix(vtkNIFTIImageReader *reader)
{
    if (reader == nullptr) {
        return nullptr;
    }

    vtkMatrix4x4 *niftiMatrix = reader->GetSFormMatrix();
    if (niftiMatrix == nullptr) {
        niftiMatrix = reader->GetQFormMatrix();
    }
    if (niftiMatrix == nullptr) {
        return nullptr;
    }

    // vtkNIFTIImageReader reports NIfTI physical space in RAS.
    // The rest of this viewer uses the ITK/DICOM-style internal volume contract,
    // i.e. physical coordinates encoded directly on vtkImageData in LPS.
    auto rasToLps = vtkSmartPointer<vtkMatrix4x4>::New();
    rasToLps->Identity();
    rasToLps->SetElement(0, 0, -1.0);
    rasToLps->SetElement(1, 1, -1.0);

    auto internalMatrix = vtkSmartPointer<vtkMatrix4x4>::New();
    vtkMatrix4x4::Multiply4x4(rasToLps, niftiMatrix, internalMatrix);
    return internalMatrix;
}

bool buildPersistentImageData(vtkImageData *vtkImage,
                              const QString &sourcePath,
                              const QString &sourceLabel,
                              vtkSmartPointer<vtkImageData> *persistentImageData,
                              QString *errorMessage)
{
    if (persistentImageData == nullptr) {
        return false;
    }

    if (vtkImage == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("VTK 转换失败，未生成影像数据: %1").arg(sourcePath);
        }
        return false;
    }

    int extent[6] { 0, -1, 0, -1, 0, -1 };
    vtkImage->GetExtent(extent);
    if (extent[0] > extent[1] || extent[2] > extent[3] || extent[4] > extent[5]) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("%1维度无效，无法显示: %2").arg(sourceLabel, sourcePath);
        }
        return false;
    }

    auto *scalars = vtkImage->GetPointData() != nullptr
        ? vtkImage->GetPointData()->GetScalars()
        : nullptr;
    if (scalars == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("%1没有可显示的像素标量数据: %2").arg(sourceLabel, sourcePath);
        }
        return false;
    }

    auto persistentImage = vtkSmartPointer<vtkImageData>::New();
    // 深拷贝以切断对当前 ITK/VTK 桥接管线生命周期的依赖。
    persistentImage->DeepCopy(vtkImage);
    persistentImage->Modified();
    *persistentImageData = persistentImage;
    return true;
}

void applyNiftiPhysicalSpace(vtkMatrix4x4 *physicalMatrix, vtkImageData *vtkImage)
{
    if (physicalMatrix == nullptr || vtkImage == nullptr) {
        return;
    }

    auto directionMatrix = vtkSmartPointer<vtkMatrix3x3>::New();
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            directionMatrix->SetElement(row, column, physicalMatrix->GetElement(row, column));
        }
    }

    vtkImage->SetDirectionMatrix(directionMatrix);
    vtkImage->SetOrigin(physicalMatrix->GetElement(0, 3),
                        physicalMatrix->GetElement(1, 3),
                        physicalMatrix->GetElement(2, 3));
}

vtkSmartPointer<vtkImageData> buildNiftiImageData(vtkNIFTIImageReader *reader)
{
    if (reader == nullptr || reader->GetOutput() == nullptr) {
        return nullptr;
    }

    auto orientedImage = vtkSmartPointer<vtkImageData>::New();
    orientedImage->DeepCopy(reader->GetOutput());
    applyNiftiPhysicalSpace(buildNiftiInternalMatrix(reader), orientedImage);

    const double slope = reader->GetRescaleSlope();
    const double intercept = reader->GetRescaleIntercept();
    if (nearlyEqual(slope, 1.0) && nearlyEqual(intercept, 0.0)) {
        orientedImage->Modified();
        return orientedImage;
    }

    const double effectiveSlope = nearlyEqual(slope, 0.0) ? 1.0 : slope;
    auto shiftScale = vtkSmartPointer<vtkImageShiftScale>::New();
    shiftScale->SetInputData(orientedImage);
    shiftScale->SetShift(intercept / effectiveSlope);
    shiftScale->SetScale(effectiveSlope);
    shiftScale->SetOutputScalarTypeToDouble();
    shiftScale->Update();

    auto rescaledImage = vtkSmartPointer<vtkImageData>::New();
    rescaledImage->DeepCopy(shiftScale->GetOutput());
    applyNiftiPhysicalSpace(buildNiftiInternalMatrix(reader), rescaledImage);
    rescaledImage->Modified();
    return rescaledImage;
}

WindowLevelPresetData readNiftiWindowLevelPreset(vtkNIFTIImageReader *reader)
{
    WindowLevelPresetData preset;
    auto *header = reader != nullptr ? reader->GetNIFTIHeader() : nullptr;
    if (header == nullptr) {
        return preset;
        
    }

    const double calMin = header->GetCalMin();
    const double calMax = header->GetCalMax();
    if (!std::isfinite(calMin) || !std::isfinite(calMax) || calMax <= calMin) {
        return preset;
    }

    preset.window = calMax - calMin;
    preset.level = 0.5 * (calMin + calMax);
    preset.explanation = QStringLiteral("NIfTI cal_min/cal_max");
    preset.isValid = true;
    return preset;
}

bool parseFirstDicomDecimal(const QString &rawValue, double *parsedValue)
{
    if (parsedValue == nullptr) {
        return false;
    }

    const QStringList components = rawValue.split(QLatin1Char('\\'), Qt::SkipEmptyParts);
    for (const QString &component : components) {
        bool ok = false;
        const double value = component.trimmed().toDouble(&ok);
        if (ok) {
            *parsedValue = value;
            return true;
        }
    }

    return false;
}

QString firstDicomValue(const QString &rawValue)
{
    const QStringList components = rawValue.split(QLatin1Char('\\'), Qt::SkipEmptyParts);
    return components.isEmpty() ? rawValue.trimmed() : components.front().trimmed();
}

QString readMetaDataTag(VolumeImageIOType *imageIO, const std::string &fileName, const char *tag)
{
    if (imageIO == nullptr || fileName.empty()) {
        return QStringLiteral("<n/a>");
    }

    try {
        imageIO->SetFileName(fileName);
        imageIO->ReadImageInformation();
    } catch (const itk::ExceptionObject &) {
        return QStringLiteral("<error>");
    } catch (const gdcm::Exception &) {
        return QStringLiteral("<error>");
    }

    std::string value;
    if (imageIO->GetValueFromTag(tag, value)) {
        return QString::fromStdString(value);
    }

    return QStringLiteral("<missing>");
}

WindowLevelPresetData readWindowLevelPreset(VolumeImageIOType *imageIO, const std::string &fileName)
{
    WindowLevelPresetData preset;
    if (imageIO == nullptr || fileName.empty()) {
        return preset;
    }

    try {
        imageIO->SetFileName(fileName);
        imageIO->ReadImageInformation();
    } catch (const itk::ExceptionObject &) {
        return preset;
    } catch (const gdcm::Exception &) {
        return preset;
    }

    std::string centerValue;
    std::string widthValue;
    if (!imageIO->GetValueFromTag("0028|1050", centerValue)
        || !imageIO->GetValueFromTag("0028|1051", widthValue)) {
        return preset;
    }

    const QString centerText = QString::fromStdString(centerValue);
    const QString widthText = QString::fromStdString(widthValue);
    if (!parseFirstDicomDecimal(centerText, &preset.level)
        || !parseFirstDicomDecimal(widthText, &preset.window)
        || preset.window < 1.0) {
        return preset;
    }

    std::string explanationValue;
    if (imageIO->GetValueFromTag("0028|1055", explanationValue)) {
        preset.explanation = firstDicomValue(QString::fromStdString(explanationValue));
    }

    preset.isValid = true;
    return preset;
}

std::vector<std::string> selectLargestSeriesFiles(VolumeNamesGeneratorType *namesGenerator)
{
    std::vector<std::string> largestSeriesFiles;
    if (namesGenerator == nullptr) {
        return largestSeriesFiles;
    }

    const auto &seriesUIDs = namesGenerator->GetSeriesUIDs();
    for (const auto &seriesUID : seriesUIDs) {
        const auto &seriesFiles = namesGenerator->GetFileNames(seriesUID);
        if (seriesFiles.size() > largestSeriesFiles.size()) {
            largestSeriesFiles = seriesFiles;
        }
    }

    return largestSeriesFiles;
}

std::vector<std::string> toNativeDicomFileNames(const QStringList &filePaths)
{
    std::vector<std::string> nativeFileNames;
    nativeFileNames.reserve(static_cast<std::size_t>(filePaths.size()));
    for (const QString &filePath : filePaths) {
        if (filePath.isEmpty()) {
            continue;
        }

        nativeFileNames.push_back(QDir::toNativeSeparators(filePath).toStdString());
    }
    return nativeFileNames;
}

void logOrientationDiagnostics(const QString &dicomPath,
                               VolumeImageType *itkImage,
                               vtkImageData *vtkImage,
                               const std::vector<std::string> &fileNames,
                               const WindowLevelPresetData &preset)
{
    if (itkImage == nullptr || vtkImage == nullptr) {
        return;
    }

    const auto itkDirection = itkImage->GetDirection();
    const auto itkOrigin = itkImage->GetOrigin();
    const auto itkSpacing = itkImage->GetSpacing();
    const auto itkSize = itkImage->GetLargestPossibleRegion().GetSize();

    double vtkOrigin[3] { 0.0, 0.0, 0.0 };
    double vtkSpacing[3] { 0.0, 0.0, 0.0 };
    int vtkExtent[6] { 0, -1, 0, -1, 0, -1 };
    vtkImage->GetOrigin(vtkOrigin);
    vtkImage->GetSpacing(vtkSpacing);
    vtkImage->GetExtent(vtkExtent);

    qCInfo(lcStudyLoadDiagnostics).noquote() << QStringLiteral("[DICOM] path=%1").arg(dicomPath);
    qCInfo(lcStudyLoadDiagnostics).noquote()
        << QStringLiteral("[DICOM] file-count=%1 first=%2 last=%3")
               .arg(static_cast<qlonglong>(fileNames.size()))
               .arg(fileNames.empty() ? QStringLiteral("<none>") : QString::fromStdString(fileNames.front()))
               .arg(fileNames.empty() ? QStringLiteral("<none>") : QString::fromStdString(fileNames.back()));
    qCInfo(lcStudyLoadDiagnostics).noquote()
        << QStringLiteral("[DICOM] itk-size=[%1, %2, %3] itk-spacing=[%4, %5, %6] itk-origin=[%7, %8, %9]")
               .arg(static_cast<qlonglong>(itkSize[0]))
               .arg(static_cast<qlonglong>(itkSize[1]))
               .arg(static_cast<qlonglong>(itkSize[2]))
               .arg(itkSpacing[0], 0, 'f', 6)
               .arg(itkSpacing[1], 0, 'f', 6)
               .arg(itkSpacing[2], 0, 'f', 6)
               .arg(itkOrigin[0], 0, 'f', 6)
               .arg(itkOrigin[1], 0, 'f', 6)
               .arg(itkOrigin[2], 0, 'f', 6);
    qCInfo(lcStudyLoadDiagnostics).noquote() << QStringLiteral("[DICOM] itk-direction=%1").arg(formatItkDirection(itkDirection));
    qCInfo(lcStudyLoadDiagnostics).noquote()
        << QStringLiteral("[DICOM] vtk-extent=[%1, %2, %3, %4, %5, %6] vtk-spacing=[%7, %8, %9] vtk-origin=[%10, %11, %12]")
               .arg(vtkExtent[0])
               .arg(vtkExtent[1])
               .arg(vtkExtent[2])
               .arg(vtkExtent[3])
               .arg(vtkExtent[4])
               .arg(vtkExtent[5])
               .arg(vtkSpacing[0], 0, 'f', 6)
               .arg(vtkSpacing[1], 0, 'f', 6)
               .arg(vtkSpacing[2], 0, 'f', 6)
               .arg(vtkOrigin[0], 0, 'f', 6)
               .arg(vtkOrigin[1], 0, 'f', 6)
               .arg(vtkOrigin[2], 0, 'f', 6);
    qCInfo(lcStudyLoadDiagnostics).noquote() << QStringLiteral("[DICOM] vtk-direction=%1").arg(formatVtkDirection(vtkImage));

    auto metaImageIO = VolumeImageIOType::New();
    qCInfo(lcStudyLoadDiagnostics).noquote()
        << QStringLiteral("[DICOM] first-instance=%1 first-position=%2 first-orientation=%3")
               .arg(fileNames.empty() ? QStringLiteral("<none>") : readMetaDataTag(metaImageIO, fileNames.front(), "0020|0013"))
               .arg(fileNames.empty() ? QStringLiteral("<none>") : readMetaDataTag(metaImageIO, fileNames.front(), "0020|0032"))
               .arg(fileNames.empty() ? QStringLiteral("<none>") : readMetaDataTag(metaImageIO, fileNames.front(), "0020|0037"));
    qCInfo(lcStudyLoadDiagnostics).noquote()
        << QStringLiteral("[DICOM] last-instance=%1 last-position=%2 last-orientation=%3")
               .arg(fileNames.empty() ? QStringLiteral("<none>") : readMetaDataTag(metaImageIO, fileNames.back(), "0020|0013"))
               .arg(fileNames.empty() ? QStringLiteral("<none>") : readMetaDataTag(metaImageIO, fileNames.back(), "0020|0032"))
               .arg(fileNames.empty() ? QStringLiteral("<none>") : readMetaDataTag(metaImageIO, fileNames.back(), "0020|0037"));
    qCInfo(lcStudyLoadDiagnostics).noquote()
        << QStringLiteral("[DICOM] voi-window=%1 voi-level=%2 explanation=%3")
               .arg(preset.isValid ? QString::number(preset.window, 'f', 3) : QStringLiteral("<missing>"))
               .arg(preset.isValid ? QString::number(preset.level, 'f', 3) : QStringLiteral("<missing>"))
               .arg(preset.explanation.isEmpty() ? QStringLiteral("<none>") : preset.explanation);
}

void logNiftiOrientationDiagnostics(const QString &filePath,
                                    vtkNIFTIImageReader *reader,
                                    vtkImageData *vtkImage,
                                    const WindowLevelPresetData &preset)
{
    if (reader == nullptr || vtkImage == nullptr) {
        return;
    }

    double vtkOrigin[3] { 0.0, 0.0, 0.0 };
    double vtkSpacing[3] { 0.0, 0.0, 0.0 };
    int vtkExtent[6] { 0, -1, 0, -1, 0, -1 };
    vtkImage->GetOrigin(vtkOrigin);
    vtkImage->GetSpacing(vtkSpacing);
    vtkImage->GetExtent(vtkExtent);

    qCInfo(lcStudyLoadDiagnostics).noquote() << QStringLiteral("[NIFTI] file=%1").arg(filePath);
    qCInfo(lcStudyLoadDiagnostics).noquote()
        << QStringLiteral("[NIFTI] vtk-extent=[%1, %2, %3, %4, %5, %6] vtk-spacing=[%7, %8, %9] vtk-origin=[%10, %11, %12] slope=%13 intercept=%14")
               .arg(vtkExtent[0])
               .arg(vtkExtent[1])
               .arg(vtkExtent[2])
               .arg(vtkExtent[3])
               .arg(vtkExtent[4])
               .arg(vtkExtent[5])
               .arg(vtkSpacing[0], 0, 'f', 6)
               .arg(vtkSpacing[1], 0, 'f', 6)
               .arg(vtkSpacing[2], 0, 'f', 6)
               .arg(vtkOrigin[0], 0, 'f', 6)
               .arg(vtkOrigin[1], 0, 'f', 6)
               .arg(vtkOrigin[2], 0, 'f', 6)
               .arg(reader->GetRescaleSlope(), 0, 'f', 6)
               .arg(reader->GetRescaleIntercept(), 0, 'f', 6);
    qCInfo(lcStudyLoadDiagnostics).noquote() << QStringLiteral("[NIFTI] vtk-direction=%1").arg(formatVtkDirection(vtkImage));
    auto *header = reader->GetNIFTIHeader();
    qCInfo(lcStudyLoadDiagnostics).noquote()
        << QStringLiteral("[NIFTI] cal-min=%1 cal-max=%2 initial-window=%3 initial-level=%4 preset=%5")
               .arg(header != nullptr ? QString::number(header->GetCalMin(), 'f', 6) : QStringLiteral("<missing>"))
               .arg(header != nullptr ? QString::number(header->GetCalMax(), 'f', 6) : QStringLiteral("<missing>"))
               .arg(preset.isValid ? QString::number(preset.window, 'f', 6) : QStringLiteral("<auto>"))
               .arg(preset.isValid ? QString::number(preset.level, 'f', 6) : QStringLiteral("<auto>"))
               .arg(preset.explanation.isEmpty() ? QStringLiteral("<none>") : preset.explanation);
}

vtkSmartPointer<vtkPolyData> loadModelPolyData(const QString &filePath, LoadedModelData::MaterialData *material)
{
    const QString nativeFilePath = QDir::toNativeSeparators(filePath);
    const QByteArray utf8Path = nativeFilePath.toUtf8();
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    vtkSmartPointer<vtkPolyData> polyData = vtkSmartPointer<vtkPolyData>::New();

    if (suffix == QStringLiteral("stl")) {
        auto reader = vtkSmartPointer<vtkSTLReader>::New();
        reader->SetFileName(utf8Path.constData());
        reader->Update();
        polyData->DeepCopy(reader->GetOutput());
        if (material != nullptr && !material->hasMaterial) {
            *material = inferDefaultMaterialFromFilePath(filePath);
        }
    } else if (suffix == QStringLiteral("obj")) {
        const ObjMaterialReference materialReference = inspectObjMaterialReference(nativeFilePath);
        if (material != nullptr) {
            *material = parseMtlMaterial(materialReference.mtllibPath, materialReference.materialName);
            if (!material->hasMaterial) {
                *material = inferDefaultMaterialFromFilePath(filePath);
            }
        }

        QByteArray effectiveUtf8Path = utf8Path;
        std::unique_ptr<QTemporaryFile> sanitizedObj;
        if (materialReference.requiresSanitizedLoad) {
            sanitizedObj = std::make_unique<QTemporaryFile>(QDir::tempPath() + QStringLiteral("/dicomviewer-obj-XXXXXX.obj"));
            sanitizedObj->setAutoRemove(false);
            if (sanitizedObj->open()) {
                if (writeSanitizedObjFile(nativeFilePath, sanitizedObj.get())) {
                    effectiveUtf8Path = QDir::toNativeSeparators(sanitizedObj->fileName()).toUtf8();
                }
                sanitizedObj->close();
            } else {
                sanitizedObj.reset();
            }
        }

        auto reader = vtkSmartPointer<vtkOBJReader>::New();
        reader->SetFileName(effectiveUtf8Path.constData());
        reader->Update();
        polyData->DeepCopy(reader->GetOutput());

        if (sanitizedObj != nullptr) {
            QFile::remove(sanitizedObj->fileName());
        }
    } else if (suffix == QStringLiteral("ply")) {
        auto reader = vtkSmartPointer<vtkPLYReader>::New();
        reader->SetFileName(utf8Path.constData());
        reader->Update();
        polyData->DeepCopy(reader->GetOutput());
        if (material != nullptr && !material->hasMaterial) {
            *material = inferDefaultMaterialFromFilePath(filePath);
        }
    } else if (suffix == QStringLiteral("vtp")) {
        auto reader = vtkSmartPointer<vtkXMLPolyDataReader>::New();
        reader->SetFileName(utf8Path.constData());
        reader->Update();
        polyData->DeepCopy(reader->GetOutput());
        if (material != nullptr && !material->hasMaterial) {
            *material = inferDefaultMaterialFromFilePath(filePath);
        }
    } else if (suffix == QStringLiteral("vtk")) {
        auto reader = vtkSmartPointer<vtkPolyDataReader>::New();
        reader->SetFileName(utf8Path.constData());
        reader->Update();
        polyData->DeepCopy(reader->GetOutput());
        if (material != nullptr && !material->hasMaterial) {
            *material = inferDefaultMaterialFromFilePath(filePath);
        }
    } else {
        return nullptr;
    }

    if (polyData->GetNumberOfPoints() <= 0 && polyData->GetNumberOfCells() <= 0) {
        return nullptr;
    }

    return polyData;
}

void loadDicomData(const QString &dicomPath,
                   const QStringList &knownDicomFiles,
                   StudyLoadResult *result,
                   const StudyLoadFeedback *feedback)
{
    if (result == nullptr || dicomPath.isEmpty()) {
        return;
    }

    try {
        if (isCancelled(feedback)) {
            *result = cancelledResult();
            return;
        }

        reportProgress(feedback,
                       QStringLiteral("正在识别主 DICOM 序列...\n%1").arg(dicomPath),
                       40);
        std::vector<std::string> fileNames = toNativeDicomFileNames(knownDicomFiles);
        if (fileNames.empty()) {
            auto namesGenerator = VolumeNamesGeneratorType::New();
            namesGenerator->SetUseSeriesDetails(true);
            namesGenerator->SetDirectory(dicomPath.toStdString());
            fileNames = selectLargestSeriesFiles(namesGenerator);
        }

        if (fileNames.empty()) {
            result->errorMessage = QStringLiteral("未在目录中识别到有效的 DICOM 序列: %1").arg(dicomPath);
            return;
        }

        if (isCancelled(feedback)) {
            *result = cancelledResult();
            return;
        }

        reportProgress(feedback,
                       QStringLiteral("正在读取 DICOM 体数据...\n共 %1 个文件").arg(static_cast<int>(fileNames.size())),
                       55);
        auto imageIO = VolumeImageIOType::New();
        auto reader = VolumeReaderType::New();
        reader->SetImageIO(imageIO);
        reader->SetFileNames(fileNames);
        reader->ForceOrthogonalDirectionOff();
        reader->Update();

        if (isCancelled(feedback)) {
            *result = cancelledResult();
            return;
        }

        reportProgress(feedback, QStringLiteral("正在转换 DICOM 数据到渲染格式..."), 70);
        auto connector = VolumeConnectorType::New();
        connector->SetInput(reader->GetOutput());
        connector->Update();

        auto *connectorOutput = connector->GetOutput();
        vtkSmartPointer<vtkImageData> persistentImageData;
        if (!buildPersistentImageData(connectorOutput,
                                      dicomPath,
                                      QStringLiteral("DICOM 序列"),
                                      &persistentImageData,
                                      &result->errorMessage)) {
            return;
        }
        applyItkDirectionToVtkImage(reader->GetOutput(), persistentImageData);
        persistentImageData->Modified();

        result->windowLevelPreset = readWindowLevelPreset(VolumeImageIOType::New(), fileNames.front());
        result->imageData = persistentImageData;
        if (lcStudyLoadDiagnostics().isInfoEnabled()) {
            logOrientationDiagnostics(dicomPath,
                                      reader->GetOutput(),
                                      result->imageData,
                                      fileNames,
                                      result->windowLevelPreset);
        }
    } catch (const itk::ExceptionObject &ex) {
        result->errorMessage = QStringLiteral("ITK/GDCM 读取失败: %1").arg(QString::fromLocal8Bit(ex.what()));
    } catch (const gdcm::Exception &ex) {
        result->errorMessage = QStringLiteral("GDCM 读取失败: %1").arg(QString::fromLocal8Bit(ex.what()));
    } catch (const std::exception &ex) {
        result->errorMessage = QStringLiteral("读取失败: %1").arg(QString::fromLocal8Bit(ex.what()));
    }
}

void loadNiftiData(const QString &filePath,
                   StudyLoadResult *result,
                   const StudyLoadFeedback *feedback)
{
    if (result == nullptr || filePath.isEmpty()) {
        return;
    }

    try {
        if (isCancelled(feedback)) {
            *result = cancelledResult();
            return;
        }

        const QString nativeFilePath = QDir::toNativeSeparators(filePath);
        const QByteArray utf8Path = nativeFilePath.toUtf8();
        auto reader = vtkSmartPointer<vtkNIFTIImageReader>::New();
        reader->SetFileName(utf8Path.constData());
        if (reader->CanReadFile(utf8Path.constData()) == 0) {
            result->errorMessage = QStringLiteral("不是可读取的 NIfTI 文件: %1").arg(nativeFilePath);
            return;
        }

        reportProgress(feedback,
                       QStringLiteral("正在读取 NIfTI 体数据...\n%1").arg(nativeFilePath),
                       55);
        reader->TimeAsVectorOff();
        reader->Update();

        if (isCancelled(feedback)) {
            *result = cancelledResult();
            return;
        }

        reportProgress(feedback, QStringLiteral("正在整理 NIfTI 空间方向与标量范围..."), 70);
        vtkSmartPointer<vtkImageData> normalizedImageData = buildNiftiImageData(reader);

        vtkSmartPointer<vtkImageData> persistentImageData;
        if (!buildPersistentImageData(normalizedImageData,
                                      nativeFilePath,
                                      QStringLiteral("NIfTI 文件"),
                                      &persistentImageData,
                                      &result->errorMessage)) {
            return;
        }

        result->windowLevelPreset = readNiftiWindowLevelPreset(reader);
        result->imageData = persistentImageData;
        if (lcStudyLoadDiagnostics().isInfoEnabled()) {
            logNiftiOrientationDiagnostics(nativeFilePath, reader, result->imageData, result->windowLevelPreset);
        }
    } catch (const std::exception &ex) {
        result->errorMessage = QStringLiteral("读取失败: %1").arg(QString::fromLocal8Bit(ex.what()));
    }
}

void loadModelData(const QStringList &modelFiles, StudyLoadResult *result, const StudyLoadFeedback *feedback)
{
    if (result == nullptr) {
        return;
    }

    result->models.clear();
    result->models.reserve(static_cast<std::size_t>(modelFiles.size()));
    const int totalModels = std::max(1, modelFiles.size());
    int modelIndex = 0;
    for (const QString &modelFile : modelFiles) {
        if (isCancelled(feedback)) {
            *result = cancelledResult();
            return;
        }

        const int percent = 80 + static_cast<int>((15.0 * modelIndex) / totalModels);
        reportProgress(feedback,
                       QStringLiteral("正在读取模型文件...\n%1").arg(modelFile),
                       percent);
        LoadedModelData::MaterialData material;
        auto polyData = loadModelPolyData(modelFile, &material);
        if (polyData == nullptr) {
            ++modelIndex;
            continue;
        }

        LoadedModelData modelData;
        modelData.filePath = modelFile;
        modelData.polyData = polyData;
        modelData.material = material;
        result->models.push_back(modelData);
        ++modelIndex;
    }
}
}

StudyLoadResult StudyLoader::loadFromDirectory(const QString &rootPath, const StudyLoadFeedback &feedback)
{
    StudyLoadResult result;
    reportProgress(&feedback,
                   QStringLiteral("正在扫描病例目录结构...\n%1").arg(rootPath),
                   0);

    try {
        CasePackageReader packageReader;
        result.package = packageReader.readFromDirectory(rootPath, &result.errorMessage, &feedback);
        if (isCancelled(&feedback)) {
            return cancelledResult();
        }
        if (!result.package.isValid()) {
            if (result.errorMessage.isEmpty()) {
                result.errorMessage = QStringLiteral("病例包目录缺少可识别的数据。");
            }
            return result;
        }

        reportProgress(&feedback, QStringLiteral("已识别病例包结构，开始读取数据..."), 35);

        if (result.package.hasDicomVolume()) {
            loadDicomData(result.package.dicomPath, result.package.dicomFiles, &result, &feedback);
            if (result.cancelled || !result.errorMessage.isEmpty()) {
                return result;
            }
        } else if (result.package.hasNiftiVolume()) {
            loadNiftiData(result.package.niftiFilePath, &result, &feedback);
            if (result.cancelled || !result.errorMessage.isEmpty()) {
                return result;
            }
        }

        if (!result.package.modelFiles.isEmpty()) {
            loadModelData(result.package.modelFiles, &result, &feedback);
            if (result.cancelled) {
                return result;
            }
        }
        reportProgress(&feedback, QStringLiteral("病例加载完成。"), 100);
    } catch (const itk::ExceptionObject &ex) {
        result.errorMessage = QStringLiteral("ITK/GDCM 读取失败: %1").arg(QString::fromLocal8Bit(ex.what()));
    } catch (const gdcm::Exception &ex) {
        result.errorMessage = QStringLiteral("GDCM 读取失败: %1").arg(QString::fromLocal8Bit(ex.what()));
    } catch (const std::exception &ex) {
        result.errorMessage = QStringLiteral("读取失败: %1").arg(QString::fromLocal8Bit(ex.what()));
    }

    return result;
}

StudyLoadResult StudyLoader::loadFromFile(const QString &filePath, const StudyLoadFeedback &feedback)
{
    StudyLoadResult result;
    const QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        result.errorMessage = QStringLiteral("影像文件不存在: %1").arg(QDir::toNativeSeparators(filePath));
        return result;
    }

    const QString absoluteFilePath = QDir::toNativeSeparators(fileInfo.absoluteFilePath());
    const QString packageRootPath = QDir::toNativeSeparators(fileInfo.absolutePath());
    reportProgress(&feedback,
                   QStringLiteral("正在准备读取影像文件...\n%1").arg(absoluteFilePath),
                   0);

    if (!isNiftiFilePath(absoluteFilePath)) {
        result.errorMessage = QStringLiteral("当前仅支持直接打开 NIfTI 文件（.nii / .nii.gz）: %1").arg(absoluteFilePath);
        return result;
    }

    reportProgress(&feedback,
                   QStringLiteral("正在扫描同目录中的模型和场景文件...\n%1").arg(packageRootPath),
                   15);

    CasePackageReader packageReader;
    QString ignoredPackageError;
    result.package = packageReader.readFromDirectory(packageRootPath, &ignoredPackageError, &feedback);
    if (isCancelled(&feedback)) {
        return cancelledResult();
    }

    result.package.rootPath = packageRootPath;
    result.package.dicomPath.clear();
    result.package.dicomFiles.clear();
    result.package.niftiFilePath = absoluteFilePath;
    loadNiftiData(absoluteFilePath, &result, &feedback);
    if (result.cancelled || !result.errorMessage.isEmpty()) {
        return result;
    }

    if (!result.package.modelFiles.isEmpty()) {
        loadModelData(result.package.modelFiles, &result, &feedback);
        if (result.cancelled) {
            return result;
        }
    }

    reportProgress(&feedback, QStringLiteral("影像文件加载完成。"), 100);
    return result;
}
