#include "studyloader.h"

#include "casepackagereader.h"

#include <QDebug>
#include <QFileInfo>
#include <QLoggingCategory>
#include <gdcmException.h>
#include <QStringList>

#include <vtkImageData.h>
#include <vtkMatrix3x3.h>
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

void loadDicomData(const QString &dicomPath, StudyLoadResult *result, const StudyLoadFeedback *feedback)
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
        auto namesGenerator = VolumeNamesGeneratorType::New();
        namesGenerator->SetUseSeriesDetails(true);
        namesGenerator->SetDirectory(dicomPath.toStdString());

        const auto fileNames = selectLargestSeriesFiles(namesGenerator);
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
        if (connectorOutput == nullptr) {
            result->errorMessage = QStringLiteral("VTK 转换失败，未生成影像数据: %1").arg(dicomPath);
            return;
        }

        int extent[6] { 0, -1, 0, -1, 0, -1 };
        connectorOutput->GetExtent(extent);
        if (extent[0] > extent[1] || extent[2] > extent[3] || extent[4] > extent[5]) {
            result->errorMessage = QStringLiteral("DICOM 序列维度无效，无法显示: %1").arg(dicomPath);
            return;
        }

        auto *scalars = connectorOutput->GetPointData() != nullptr
            ? connectorOutput->GetPointData()->GetScalars()
            : nullptr;
        if (scalars == nullptr) {
            result->errorMessage = QStringLiteral("DICOM 序列没有可显示的像素标量数据: %1").arg(dicomPath);
            return;
        }

        auto persistentImageData = vtkSmartPointer<vtkImageData>::New();
        persistentImageData->DeepCopy(connectorOutput);
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

        if (!result.package.dicomPath.isEmpty()) {
            loadDicomData(result.package.dicomPath, &result, &feedback);
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
