#include "fourpaneviewer.h"

#include "modelviewwidget.h"
#include "mprviewwidget.h"

#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QSplitter>
#include <QStackedLayout>
#include <QVBoxLayout>

#include <vtkImageData.h>

#include <itkGDCMImageIO.h>
#include <itkGDCMSeriesFileNames.h>
#include <itkImage.h>
#include <itkImageSeriesReader.h>
#include <itkImageToVTKImageFilter.h>

namespace
{
using VolumePixelType = short;
constexpr unsigned int VolumeDimension = 3;
using VolumeImageType = itk::Image<VolumePixelType, VolumeDimension>;
using VolumeReaderType = itk::ImageSeriesReader<VolumeImageType>;
using VolumeNamesGeneratorType = itk::GDCMSeriesFileNames;
using VolumeImageIOType = itk::GDCMImageIO;
using VolumeConnectorType = itk::ImageToVTKImageFilter<VolumeImageType>;
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

        m_imageData->DeepCopy(connector->GetOutput());
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
