#include "studyloader.h"

#include "casepackagereader.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <gdcmException.h>
#include <QStringList>

#include <vtkImageData.h>
#include <vtkImageShiftScale.h>
#include <vtkMatrix3x3.h>
#include <vtkMatrix4x4.h>
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

void applyNiftiOrientation(vtkNIFTIImageReader *reader, vtkImageData *vtkImage)
{
    if (reader == nullptr || vtkImage == nullptr) {
        return;
    }

    vtkMatrix4x4 *orientationMatrix = reader->GetSFormMatrix();
    if (orientationMatrix == nullptr) {
        orientationMatrix = reader->GetQFormMatrix();
    }
    if (orientationMatrix == nullptr) {
        return;
    }

    auto directionMatrix = vtkSmartPointer<vtkMatrix3x3>::New();
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            directionMatrix->SetElement(row, column, orientationMatrix->GetElement(row, column));
        }
    }

    vtkImage->SetDirectionMatrix(directionMatrix);
    vtkImage->SetOrigin(orientationMatrix->GetElement(0, 3),
                        orientationMatrix->GetElement(1, 3),
                        orientationMatrix->GetElement(2, 3));
}

vtkSmartPointer<vtkImageData> buildNiftiImageData(vtkNIFTIImageReader *reader)
{
    if (reader == nullptr || reader->GetOutput() == nullptr) {
        return nullptr;
    }

    auto orientedImage = vtkSmartPointer<vtkImageData>::New();
    orientedImage->DeepCopy(reader->GetOutput());
    applyNiftiOrientation(reader, orientedImage);

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
    applyNiftiOrientation(reader, rescaledImage);
    rescaledImage->Modified();
    return rescaledImage;
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
                                    vtkImageData *vtkImage)
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
}

vtkSmartPointer<vtkPolyData> loadModelPolyData(const QString &filePath)
{
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    vtkSmartPointer<vtkPolyData> polyData = vtkSmartPointer<vtkPolyData>::New();

    if (suffix == QStringLiteral("stl")) {
        auto reader = vtkSmartPointer<vtkSTLReader>::New();
        reader->SetFileName(filePath.toStdString().c_str());
        reader->Update();
        polyData->DeepCopy(reader->GetOutput());
    } else if (suffix == QStringLiteral("obj")) {
        auto reader = vtkSmartPointer<vtkOBJReader>::New();
        reader->SetFileName(filePath.toStdString().c_str());
        reader->Update();
        polyData->DeepCopy(reader->GetOutput());
    } else if (suffix == QStringLiteral("ply")) {
        auto reader = vtkSmartPointer<vtkPLYReader>::New();
        reader->SetFileName(filePath.toStdString().c_str());
        reader->Update();
        polyData->DeepCopy(reader->GetOutput());
    } else if (suffix == QStringLiteral("vtp")) {
        auto reader = vtkSmartPointer<vtkXMLPolyDataReader>::New();
        reader->SetFileName(filePath.toStdString().c_str());
        reader->Update();
        polyData->DeepCopy(reader->GetOutput());
    } else if (suffix == QStringLiteral("vtk")) {
        auto reader = vtkSmartPointer<vtkPolyDataReader>::New();
        reader->SetFileName(filePath.toStdString().c_str());
        reader->Update();
        polyData->DeepCopy(reader->GetOutput());
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

        result->windowLevelPreset = WindowLevelPresetData {};
        result->imageData = persistentImageData;
        if (lcStudyLoadDiagnostics().isInfoEnabled()) {
            logNiftiOrientationDiagnostics(nativeFilePath, reader, result->imageData);
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
        auto polyData = loadModelPolyData(modelFile);
        if (polyData == nullptr) {
            ++modelIndex;
            continue;
        }

        LoadedModelData modelData;
        modelData.filePath = modelFile;
        modelData.polyData = polyData;
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
    reportProgress(&feedback,
                   QStringLiteral("正在准备读取影像文件...\n%1").arg(absoluteFilePath),
                   0);

    if (!isNiftiFilePath(absoluteFilePath)) {
        result.errorMessage = QStringLiteral("当前仅支持直接打开 NIfTI 文件（.nii / .nii.gz）: %1").arg(absoluteFilePath);
        return result;
    }

    result.package.rootPath = absoluteFilePath;
    result.package.niftiFilePath = absoluteFilePath;
    loadNiftiData(absoluteFilePath, &result, &feedback);
    if (result.cancelled || !result.errorMessage.isEmpty()) {
        return result;
    }

    reportProgress(&feedback, QStringLiteral("影像文件加载完成。"), 100);
    return result;
}
