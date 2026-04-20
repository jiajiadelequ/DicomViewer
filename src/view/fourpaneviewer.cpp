#include "fourpaneviewer.h"

#include "fourpanecontentwidget.h"
#include "modelviewwidget.h"
#include "mprviewwidget.h"
#include "scenesidebarwidget.h"
#include "viewerstatewidget.h"

#include <QStackedLayout>
#include <QStringList>
#include <QTimer>

#include <array>

#include <vtkImageData.h>
#include <vtkPolyData.h>

namespace
{
std::array<MprViewWidget *, 3> mprPanels(FourPaneContentWidget *contentPage)
{
    if (contentPage == nullptr) {
        return { nullptr, nullptr, nullptr };
    }

    return {
        contentPage->axialPanel(),
        contentPage->coronalPanel(),
        contentPage->sagittalPanel()
    };
}

QString buildStudySummaryText(const StudyPackage &package)
{
    QStringList summaryLines;
    summaryLines << QStringLiteral("数据来源: %1").arg(package.rootPath);
    if (package.hasDicomVolume()) {
        summaryLines << QStringLiteral("DICOM: %1").arg(package.dicomFiles.size());
    } else if (package.hasNiftiVolume()) {
        summaryLines << QStringLiteral("NIfTI: %1").arg(package.niftiFilePath);
    } else {
        summaryLines << QStringLiteral("影像: 未发现");
    }
    summaryLines << QStringLiteral("模型: %1").arg(package.modelFiles.size());
    if (!package.sceneFilePath.isEmpty()) {
        summaryLines << QStringLiteral("场景配置: %1").arg(package.sceneFilePath);
    }

    return summaryLines.join(QLatin1Char('\n'));
}
}

FourPaneViewer::FourPaneViewer(QWidget *parent)
    : QWidget(parent)
    , m_rootLayout(new QStackedLayout(this))
    , m_statePage(new ViewerStateWidget(this))
    , m_contentPage(nullptr)
    , m_imageData(vtkSmartPointer<vtkImageData>::New())
{
    m_rootLayout->setContentsMargins(0, 0, 0, 0);
    m_rootLayout->addWidget(m_statePage);

    showEmptyState();
}

bool FourPaneViewer::applyStudyLoadResult(const StudyLoadResult &result, QString *errorMessage)
{
    if (!result.succeeded()) {
        if (errorMessage != nullptr) {
            *errorMessage = result.errorMessage;
        }
        return false;
    }

    ensureContentPage();
    const auto panels = mprPanels(m_contentPage);
    auto *volumePanel = m_contentPage->volumePanel();
    auto *sidebarPanel = m_contentPage->sidebarPanel();

    if (result.imageData != nullptr) {
        m_hasImageData = true;
        setCrosshairEnabled(false);
        setRulerEnabled(false);
        m_imageData = result.imageData;
        if (result.windowLevelPreset.isValid) {
            for (MprViewWidget *panel : panels) {
                panel->setRecommendedWindowLevel(result.windowLevelPreset.window, result.windowLevelPreset.level);
            }
        } else {
            for (MprViewWidget *panel : panels) {
                panel->clearRecommendedWindowLevel();
            }
        }

        for (MprViewWidget *panel : panels) {
            panel->setImageData(m_imageData);
        }
    } else {
        m_hasImageData = false;
        setCrosshairEnabled(false);
        setRulerEnabled(false);
        m_imageData = nullptr;
        const QString imageText = QStringLiteral("未发现可显示的影像数据");
        for (MprViewWidget *panel : panels) {
            panel->clearRecommendedWindowLevel();
            panel->clearView(imageText);
        }
    }

    const QString modelSummaryText = result.models.empty()
        ? QStringLiteral("未发现模型文件")
        : QStringLiteral("已发现 %1 个模型文件").arg(static_cast<int>(result.models.size()));
    m_hasModelData = !result.models.empty();
    sidebarPanel->clearObjects();
    setClippingEnabled(false);
    volumePanel->beginSceneBatch(modelSummaryText);
    volumePanel->setReferenceImageData(result.imageData);
    for (const LoadedModelData &model : result.models) {
        sidebarPanel->addObject(model.filePath);
        volumePanel->addModelData(model.filePath, model.polyData, model.material);
    }
    volumePanel->endSceneBatch(modelSummaryText);
    sidebarPanel->setClippingState(volumePanel->hasModels(), false);
    setRulerEnabled(m_rulerEnabled);

    sidebarPanel->setSummaryText(buildStudySummaryText(result.package));
    m_rootLayout->setCurrentWidget(m_contentPage);

    if (result.imageData != nullptr) {
        QTimer::singleShot(0, this, [this]() {
            for (MprViewWidget *panel : mprPanels(m_contentPage)) {
                panel->refreshView();
            }
        });
    }

    return true;
}

void FourPaneViewer::showEmptyState()
{
    m_statePage->setState(QStringLiteral("未加载数据"),
                          QStringLiteral("请选择病例包目录或 NIfTI 影像文件。加载成功后，这里会显示三视图 MPR 和 3D 模型。"));
    m_rootLayout->setCurrentWidget(m_statePage);
}

void FourPaneViewer::showLoadingState(const QString &message)
{
    m_statePage->setState(QStringLiteral("正在加载"), message);
    m_rootLayout->setCurrentWidget(m_statePage);
}

void FourPaneViewer::showErrorState(const QString &message)
{
    m_statePage->setState(QStringLiteral("加载失败"),
                          message.isEmpty() ? QStringLiteral("数据加载失败。") : message);
    m_rootLayout->setCurrentWidget(m_statePage);
}

void FourPaneViewer::ensureContentPage()
{
    if (m_contentPage != nullptr) {
        return;
    }

    m_contentPage = new FourPaneContentWidget(this);

    for (MprViewWidget *panel : mprPanels(m_contentPage)) {
        connect(panel, &MprViewWidget::cursorWorldPositionChanged, this, &FourPaneViewer::syncCrosshairPosition);
        connect(panel, &MprViewWidget::windowLevelChanged, this, &FourPaneViewer::syncWindowLevel);
    }
    connect(m_contentPage->volumePanel(),
            &ModelViewWidget::cursorWorldPositionChanged,
            this,
            &FourPaneViewer::syncCrosshairPosition);
    connect(m_contentPage->sidebarPanel(),
            &SceneSidebarWidget::crosshairToggled,
            this,
            &FourPaneViewer::handleCrosshairToggle);
    connect(m_contentPage->sidebarPanel(),
            &SceneSidebarWidget::rulerToggled,
            this,
            &FourPaneViewer::handleRulerToggle);
    connect(m_contentPage->sidebarPanel(),
            &SceneSidebarWidget::clippingToggled,
            this,
            &FourPaneViewer::handleClippingToggle);
    connect(m_contentPage->sidebarPanel(),
            &SceneSidebarWidget::objectVisibilityChanged,
            this,
            &FourPaneViewer::handleObjectVisibilityChanged);

    m_rootLayout->addWidget(m_contentPage);
}

void FourPaneViewer::setCrosshairEnabled(bool enabled)
{
    const bool actualEnabled = enabled && m_hasImageData;
    m_crosshairEnabled = actualEnabled;

    if (m_contentPage != nullptr) {
        m_contentPage->sidebarPanel()->setCrosshairState(m_hasImageData, actualEnabled);
    }

    for (MprViewWidget *panel : mprPanels(m_contentPage)) {
        if (panel != nullptr) {
            panel->setCrosshairEnabled(actualEnabled);
        }
    }
    if (m_contentPage != nullptr) {
        m_contentPage->volumePanel()->setCrosshairEnabled(actualEnabled);
    }
}

void FourPaneViewer::setRulerEnabled(bool enabled)
{
    const bool actualEnabled = enabled && (m_hasImageData || m_hasModelData);
    m_rulerEnabled = actualEnabled;

    if (m_contentPage != nullptr) {
        m_contentPage->sidebarPanel()->setRulerState(m_hasImageData || m_hasModelData, actualEnabled);
    }

    for (MprViewWidget *panel : mprPanels(m_contentPage)) {
        if (panel != nullptr) {
            panel->setRulerEnabled(actualEnabled);
        }
    }
    if (m_contentPage != nullptr && m_contentPage->volumePanel() != nullptr) {
        m_contentPage->volumePanel()->setRulerEnabled(actualEnabled);
    }
}

void FourPaneViewer::setClippingEnabled(bool enabled)
{
    m_clippingEnabled = enabled;
    if (m_contentPage == nullptr) {
        return;
    }

    m_contentPage->volumePanel()->setClippingEnabled(enabled);
    m_contentPage->sidebarPanel()->setClippingState(m_contentPage->volumePanel()->hasModels(), enabled);
}

void FourPaneViewer::handleCrosshairToggle(bool checked)
{
    setCrosshairEnabled(checked);
    if (!m_crosshairEnabled || m_contentPage == nullptr) {
        return;
    }

    const auto initialCursor = m_contentPage->axialPanel()->cursorWorldPosition();
    syncCrosshairPosition(initialCursor[0], initialCursor[1], initialCursor[2]);
}

void FourPaneViewer::handleRulerToggle(bool checked)
{
    setRulerEnabled(checked);
}

void FourPaneViewer::handleClippingToggle(bool checked)
{
    setClippingEnabled(checked);
}

void FourPaneViewer::handleObjectVisibilityChanged(int index, bool visible)
{
    if (m_contentPage == nullptr) {
        return;
    }

    m_contentPage->volumePanel()->setModelVisibility(index, visible);
}

void FourPaneViewer::syncCrosshairPosition(double x, double y, double z)
{
    if (!m_crosshairEnabled
        || m_syncingCrosshair
        || m_contentPage == nullptr) {
        return;
    }

    m_syncingCrosshair = true;
    for (MprViewWidget *panel : mprPanels(m_contentPage)) {
        panel->setCursorWorldPosition(x, y, z);
    }
    m_contentPage->volumePanel()->setCursorWorldPosition(x, y, z);
    m_syncingCrosshair = false;
}

void FourPaneViewer::syncWindowLevel(double window, double level)
{
    if (m_syncingWindowLevel || m_contentPage == nullptr) {
        return;
    }

    m_syncingWindowLevel = true;
    for (MprViewWidget *panel : mprPanels(m_contentPage)) {
        panel->setWindowLevel(window, level);
    }
    m_syncingWindowLevel = false;
}
