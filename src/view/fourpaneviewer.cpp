#include "fourpaneviewer.h"

#include "modelviewwidget.h"
#include "mprviewwidget.h"

#include <QDebug>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QSplitter>
#include <QStackedLayout>
#include <QVBoxLayout>

#include <vtkPointData.h>
#include <vtkMatrix3x3.h>

#include <itkGDCMImageIO.h>
#include <itkGDCMSeriesFileNames.h>
#include <itkImage.h>
#include <itkImageSeriesReader.h>
#include <itkImageToVTKImageFilter.h>
#include <itkMetaDataObject.h>

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

struct WindowLevelPreset
{
    double window = 0.0;
    double level = 0.0;
    QString explanation;
    bool isValid = false;
};

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

WindowLevelPreset readWindowLevelPreset(VolumeImageIOType *imageIO, const std::string &fileName)
{
    WindowLevelPreset preset;
    if (imageIO == nullptr || fileName.empty()) {
        return preset;
    }

    try {
        imageIO->SetFileName(fileName);
        imageIO->ReadImageInformation();
    } catch (const itk::ExceptionObject &) {
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
    }

    std::string value;
    if (itk::ExposeMetaData<std::string>(imageIO->GetMetaDataDictionary(), tag, value)) {
        return QString::fromStdString(value);
    }

    return QStringLiteral("<missing>");
}

void logOrientationDiagnostics(const QString &dicomPath,
                               VolumeImageType *itkImage,
                               vtkImageData *vtkImage,
                               const std::vector<std::string> &fileNames)
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

    qInfo().noquote() << QStringLiteral("[DICOM] path=%1").arg(dicomPath);
    qInfo().noquote()
        << QStringLiteral("[DICOM] file-count=%1 first=%2 last=%3")
               .arg(static_cast<qlonglong>(fileNames.size()))
               .arg(fileNames.empty() ? QStringLiteral("<none>") : QString::fromStdString(fileNames.front()))
               .arg(fileNames.empty() ? QStringLiteral("<none>") : QString::fromStdString(fileNames.back()));
    qInfo().noquote()
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
    qInfo().noquote() << QStringLiteral("[DICOM] itk-direction=%1").arg(formatItkDirection(itkDirection));
    qInfo().noquote()
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
    qInfo().noquote() << QStringLiteral("[DICOM] vtk-direction=%1").arg(formatVtkDirection(vtkImage));

    auto metaImageIO = VolumeImageIOType::New();
    qInfo().noquote()
        << QStringLiteral("[DICOM] first-instance=%1 first-position=%2 first-orientation=%3")
               .arg(fileNames.empty() ? QStringLiteral("<none>") : readMetaDataTag(metaImageIO, fileNames.front(), "0020|0013"))
               .arg(fileNames.empty() ? QStringLiteral("<none>") : readMetaDataTag(metaImageIO, fileNames.front(), "0020|0032"))
               .arg(fileNames.empty() ? QStringLiteral("<none>") : readMetaDataTag(metaImageIO, fileNames.front(), "0020|0037"));
    qInfo().noquote()
        << QStringLiteral("[DICOM] last-instance=%1 last-position=%2 last-orientation=%3")
               .arg(fileNames.empty() ? QStringLiteral("<none>") : readMetaDataTag(metaImageIO, fileNames.back(), "0020|0013"))
               .arg(fileNames.empty() ? QStringLiteral("<none>") : readMetaDataTag(metaImageIO, fileNames.back(), "0020|0032"))
               .arg(fileNames.empty() ? QStringLiteral("<none>") : readMetaDataTag(metaImageIO, fileNames.back(), "0020|0037"));

    const WindowLevelPreset preset = fileNames.empty() ? WindowLevelPreset {} : readWindowLevelPreset(metaImageIO, fileNames.front());
    qInfo().noquote()
        << QStringLiteral("[DICOM] voi-window=%1 voi-level=%2 explanation=%3")
               .arg(preset.isValid ? QString::number(preset.window, 'f', 3) : QStringLiteral("<missing>"))
               .arg(preset.isValid ? QString::number(preset.level, 'f', 3) : QStringLiteral("<missing>"))
               .arg(preset.explanation.isEmpty() ? QStringLiteral("<none>") : preset.explanation);
}
}

FourPaneViewer::FourPaneViewer(QWidget *parent)
    : QWidget(parent)
    , m_rootLayout(new QStackedLayout(this))
    , m_statePage(new QWidget(this))
    , m_stateTitleLabel(new QLabel(this))
    , m_stateMessageLabel(new QLabel(this))
    , m_contentPage(nullptr)
    , m_axialPanel(nullptr)
    , m_coronalPanel(nullptr)
    , m_sagittalPanel(nullptr)
    , m_volumePanel(nullptr)
    , m_objectList(nullptr)
    , m_summaryLabel(nullptr)
    , m_imageData(vtkSmartPointer<vtkImageData>::New())
{
    auto *stateLayout = new QVBoxLayout(m_statePage);
    stateLayout->setContentsMargins(48, 48, 48, 48);
    stateLayout->addStretch();

    m_stateTitleLabel->setAlignment(Qt::AlignCenter);
    m_stateTitleLabel->setStyleSheet(QStringLiteral("font-size: 18pt; font-weight: 700;"));

    m_stateMessageLabel->setAlignment(Qt::AlignCenter);
    m_stateMessageLabel->setWordWrap(true);
    m_stateMessageLabel->setStyleSheet(QStringLiteral("font-size: 11pt; color: #6b7280;"));

    stateLayout->addWidget(m_stateTitleLabel);
    stateLayout->addSpacing(12);
    stateLayout->addWidget(m_stateMessageLabel);
    stateLayout->addStretch();

    m_rootLayout->setContentsMargins(0, 0, 0, 0);
    m_rootLayout->addWidget(m_statePage);

    showEmptyState();
}

bool FourPaneViewer::loadStudyPackage(const StudyPackage &package, QString *errorMessage)
{
    ensureContentPage();

    if (!package.dicomPath.isEmpty()) {
        if (!loadDicomSeries(package.dicomPath, errorMessage)) {
            return false;
        }
    } else {
        const QString dicomText = QStringLiteral("未发现 DICOM 序列");
        m_axialPanel->clearView(dicomText);
        m_coronalPanel->clearView(dicomText);
        m_sagittalPanel->clearView(dicomText);
    }

    m_objectList->clear();
    m_volumePanel->clearScene(package.modelFiles.isEmpty()
                                  ? QStringLiteral("未发现模型文件")
                                  : QStringLiteral("已发现 %1 个模型文件").arg(package.modelFiles.size()));
    for (const QString &modelFile : package.modelFiles) {
        auto *item = new QListWidgetItem(modelFile, m_objectList);
        item->setCheckState(Qt::Checked);
        m_volumePanel->addModelFile(modelFile);
    }

    updateSummary(package);
    m_rootLayout->setCurrentWidget(m_contentPage);
    return true;
}

void FourPaneViewer::showEmptyState()
{
    m_stateTitleLabel->setText(QStringLiteral("未加载病例"));
    m_stateMessageLabel->setText(QStringLiteral("请选择一个病例包目录。加载成功后，这里会显示三视图 MPR 和 3D 模型。"));
    m_rootLayout->setCurrentWidget(m_statePage);
}

void FourPaneViewer::showLoadingState(const QString &message)
{
    m_stateTitleLabel->setText(QStringLiteral("正在加载"));
    m_stateMessageLabel->setText(message);
    m_rootLayout->setCurrentWidget(m_statePage);
}

void FourPaneViewer::showErrorState(const QString &message)
{
    m_stateTitleLabel->setText(QStringLiteral("加载失败"));
    m_stateMessageLabel->setText(message.isEmpty() ? QStringLiteral("病例包加载失败。") : message);
    m_rootLayout->setCurrentWidget(m_statePage);
}

bool FourPaneViewer::loadDicomSeries(const QString &dicomPath, QString *errorMessage)
{
    try {
        auto namesGenerator = VolumeNamesGeneratorType::New();
        namesGenerator->SetUseSeriesDetails(true);
        namesGenerator->SetDirectory(dicomPath.toStdString());

        const auto &seriesUIDs = namesGenerator->GetSeriesUIDs();
        if (seriesUIDs.empty()) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("未在目录中识别到有效的 DICOM 序列: %1").arg(dicomPath);
            }
            return false;
        }

        const auto &fileNames = namesGenerator->GetFileNames(seriesUIDs.front());
        if (fileNames.empty()) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("DICOM 序列文件列表为空: %1").arg(dicomPath);
            }
            return false;
        }

        auto imageIO = VolumeImageIOType::New();
        auto reader = VolumeReaderType::New();
        reader->SetImageIO(imageIO);
        reader->SetFileNames(fileNames);
        reader->ForceOrthogonalDirectionOff();
        reader->Update();

        auto connector = VolumeConnectorType::New();
        connector->SetInput(reader->GetOutput());
        connector->Update();

        auto *connectorOutput = connector->GetOutput();
        if (connectorOutput == nullptr) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("VTK 转换失败，未生成影像数据: %1").arg(dicomPath);
            }
            return false;
        }

        int extent[6] { 0, -1, 0, -1, 0, -1 };
        connectorOutput->GetExtent(extent);
        if (extent[0] > extent[1] || extent[2] > extent[3] || extent[4] > extent[5]) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("DICOM 序列维度无效，无法显示: %1").arg(dicomPath);
            }
            return false;
        }

        auto *scalars = connectorOutput->GetPointData() != nullptr
            ? connectorOutput->GetPointData()->GetScalars()
            : nullptr;
        if (scalars == nullptr) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("DICOM 序列没有可显示的像素标量数据: %1").arg(dicomPath);
            }
            return false;
        }

        auto persistentImageData = vtkSmartPointer<vtkImageData>::New();
        persistentImageData->DeepCopy(connectorOutput);
        applyItkDirectionToVtkImage(reader->GetOutput(), persistentImageData);
        persistentImageData->Modified();

        m_imageData = persistentImageData;
        logOrientationDiagnostics(dicomPath, reader->GetOutput(), m_imageData, fileNames);

        const WindowLevelPreset preset = readWindowLevelPreset(VolumeImageIOType::New(), fileNames.front());
        if (preset.isValid) {
            m_axialPanel->setRecommendedWindowLevel(preset.window, preset.level);
            m_coronalPanel->setRecommendedWindowLevel(preset.window, preset.level);
            m_sagittalPanel->setRecommendedWindowLevel(preset.window, preset.level);
        } else {
            m_axialPanel->clearRecommendedWindowLevel();
            m_coronalPanel->clearRecommendedWindowLevel();
            m_sagittalPanel->clearRecommendedWindowLevel();
        }

        m_axialPanel->setImageData(m_imageData);
        m_coronalPanel->setImageData(m_imageData);
        m_sagittalPanel->setImageData(m_imageData);
        return true;
    } catch (const itk::ExceptionObject &ex) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("ITK/GDCM 读取失败: %1").arg(QString::fromLocal8Bit(ex.what()));
        }
        return false;
    } catch (const std::exception &ex) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("读取失败: %1").arg(QString::fromLocal8Bit(ex.what()));
        }
        return false;
    }
}

void FourPaneViewer::ensureContentPage()
{
    if (m_contentPage != nullptr) {
        return;
    }

    m_contentPage = new QWidget(this);
    m_axialPanel = new MprViewWidget(QStringLiteral("Axial MPR"), MprViewWidget::Orientation::Axial, m_contentPage);
    m_coronalPanel = new MprViewWidget(QStringLiteral("Coronal MPR"), MprViewWidget::Orientation::Coronal, m_contentPage);
    m_sagittalPanel = new MprViewWidget(QStringLiteral("Sagittal MPR"), MprViewWidget::Orientation::Sagittal, m_contentPage);
    m_volumePanel = new ModelViewWidget(m_contentPage);
    m_objectList = new QListWidget(m_contentPage);
    m_summaryLabel = new QLabel(QStringLiteral("尚未加载病例包"), m_contentPage);

    auto *gridHorizontalTop = new QSplitter(Qt::Horizontal, m_contentPage);
    gridHorizontalTop->addWidget(m_axialPanel);
    gridHorizontalTop->addWidget(m_coronalPanel);
    gridHorizontalTop->setChildrenCollapsible(false);

    auto *gridHorizontalBottom = new QSplitter(Qt::Horizontal, m_contentPage);
    gridHorizontalBottom->addWidget(m_sagittalPanel);
    gridHorizontalBottom->addWidget(m_volumePanel);
    gridHorizontalBottom->setChildrenCollapsible(false);

    auto *gridVertical = new QSplitter(Qt::Vertical, m_contentPage);
    gridVertical->addWidget(gridHorizontalTop);
    gridVertical->addWidget(gridHorizontalBottom);
    gridVertical->setChildrenCollapsible(false);
    gridVertical->setStretchFactor(0, 1);
    gridVertical->setStretchFactor(1, 1);

    m_objectList->setAlternatingRowColors(true);

    auto *rightPanelTitle = new QLabel(QStringLiteral("场景对象"), m_contentPage);
    rightPanelTitle->setStyleSheet(QStringLiteral("font-size: 12pt; font-weight: 700;"));

    auto *rightPanel = new QWidget(m_contentPage);
    auto *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(12, 12, 12, 12);
    rightLayout->setSpacing(8);
    rightLayout->addWidget(rightPanelTitle);
    rightLayout->addWidget(m_objectList, 1);
    rightLayout->addWidget(m_summaryLabel);

    auto *rootSplitter = new QSplitter(Qt::Horizontal, m_contentPage);
    rootSplitter->addWidget(gridVertical);
    rootSplitter->addWidget(rightPanel);
    rootSplitter->setStretchFactor(0, 5);
    rootSplitter->setStretchFactor(1, 2);
    rootSplitter->setChildrenCollapsible(false);

    auto *layout = new QVBoxLayout(m_contentPage);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(rootSplitter);

    m_rootLayout->addWidget(m_contentPage);
}

void FourPaneViewer::updateSummary(const StudyPackage &package)
{
    QStringList summaryLines;
    summaryLines << QStringLiteral("病例目录: %1").arg(package.rootPath);
    summaryLines << QStringLiteral("DICOM: %1").arg(package.dicomFiles.size());
    summaryLines << QStringLiteral("模型: %1").arg(package.modelFiles.size());
    if (!package.sceneFilePath.isEmpty()) {
        summaryLines << QStringLiteral("场景配置: %1").arg(package.sceneFilePath);
    }
    m_summaryLabel->setText(summaryLines.join(QLatin1Char('\n')));
}



