#include "fourpaneviewer.h"

#include "modelviewwidget.h"
#include "mprviewwidget.h"

#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QResizeEvent>
#include <QSplitter>
#include <QStackedLayout>
#include <QTimer>
#include <QVBoxLayout>

#include <vtkImageData.h>
#include <vtkPolyData.h>

namespace
{
class SplitterGridWidget final : public QWidget
{
public:
    SplitterGridWidget(QWidget *topLeft,
                       QWidget *topRight,
                       QWidget *bottomLeft,
                       QWidget *bottomRight,
                       QWidget *parent = nullptr)
        : QWidget(parent)
        , m_topRowSplitter(new QSplitter(Qt::Horizontal, this))
        , m_bottomRowSplitter(new QSplitter(Qt::Horizontal, this))
        , m_rowSplitter(new QSplitter(Qt::Vertical, this))
    {
        configureSplitter(m_topRowSplitter);
        configureSplitter(m_bottomRowSplitter);
        configureSplitter(m_rowSplitter);

        m_topRowSplitter->addWidget(topLeft);
        m_topRowSplitter->addWidget(topRight);
        m_bottomRowSplitter->addWidget(bottomLeft);
        m_bottomRowSplitter->addWidget(bottomRight);

        m_rowSplitter->addWidget(m_topRowSplitter);
        m_rowSplitter->addWidget(m_bottomRowSplitter);

        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        layout->addWidget(m_rowSplitter);

        connect(m_topRowSplitter,
                &QSplitter::splitterMoved,
                this,
                [this](int, int) {
                    updateColumnRatio(m_topRowSplitter);
                    applyColumnRatio(m_topRowSplitter);
                });
        connect(m_bottomRowSplitter,
                &QSplitter::splitterMoved,
                this,
                [this](int, int) {
                    updateColumnRatio(m_bottomRowSplitter);
                    applyColumnRatio(m_bottomRowSplitter);
                });
        connect(m_rowSplitter,
                &QSplitter::splitterMoved,
                this,
                [this](int, int) {
                    updateRowRatio();
                });

        QTimer::singleShot(0, this, [this]() {
            applyRowRatio();
            applyColumnRatio(nullptr);
        });
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        applyRowRatio();
        applyColumnRatio(nullptr);
    }

private:
    void configureSplitter(QSplitter *splitter)
    {
        splitter->setChildrenCollapsible(false);
        splitter->setStretchFactor(0, 1);
        splitter->setStretchFactor(1, 1);
    }

    void updateColumnRatio(QSplitter *source)
    {
        const QList<int> sizes = source->sizes();
        if (sizes.size() != 2) {
            return;
        }

        const int total = sizes[0] + sizes[1];
        if (total <= 0) {
            return;
        }

        m_columnRatio = qBound(0.05, static_cast<double>(sizes[0]) / static_cast<double>(total), 0.95);
    }

    void applyColumnRatio(QSplitter *sourceToSkip)
    {
        if (m_syncingColumns) {
            return;
        }

        m_syncingColumns = true;
        const int left = static_cast<int>(m_columnRatio * 1000.0);
        const QList<int> sizes { left, 1000 - left };
        if (sourceToSkip != m_topRowSplitter) {
            m_topRowSplitter->setSizes(sizes);
        }
        if (sourceToSkip != m_bottomRowSplitter) {
            m_bottomRowSplitter->setSizes(sizes);
        }
        m_syncingColumns = false;
    }

    void updateRowRatio()
    {
        const QList<int> sizes = m_rowSplitter->sizes();
        if (sizes.size() != 2) {
            return;
        }

        const int total = sizes[0] + sizes[1];
        if (total <= 0) {
            return;
        }

        m_rowRatio = qBound(0.05, static_cast<double>(sizes[0]) / static_cast<double>(total), 0.95);
    }

    void applyRowRatio()
    {
        if (m_syncingRows) {
            return;
        }

        m_syncingRows = true;
        const int top = static_cast<int>(m_rowRatio * 1000.0);
        m_rowSplitter->setSizes(QList<int> { top, 1000 - top });
        m_syncingRows = false;
    }

    QSplitter *m_topRowSplitter;
    QSplitter *m_bottomRowSplitter;
    QSplitter *m_rowSplitter;
    double m_columnRatio = 0.5;
    double m_rowRatio = 0.5;
    bool m_syncingColumns = false;
    bool m_syncingRows = false;
};
}

FourPaneViewer::FourPaneViewer(QWidget *parent)
    : QWidget(parent)
    , m_rootLayout(new QStackedLayout(this))
    , m_statePage(new QWidget(this))
    , m_stateTitleLabel(new QLabel(m_statePage))
    , m_stateMessageLabel(new QLabel(m_statePage))
    , m_contentPage(nullptr)
    , m_axialPanel(nullptr)
    , m_coronalPanel(nullptr)
    , m_sagittalPanel(nullptr)
    , m_volumePanel(nullptr)
    , m_objectList(nullptr)
    , m_summaryLabel(nullptr)
    , m_crosshairToggleButton(nullptr)
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

bool FourPaneViewer::applyStudyLoadResult(const StudyLoadResult &result, QString *errorMessage)
{
    if (!result.succeeded()) {
        if (errorMessage != nullptr) {
            *errorMessage = result.errorMessage;
        }
        return false;
    }

    ensureContentPage();

    if (result.imageData != nullptr) {
        m_hasDicomImage = true;
        setCrosshairEnabled(false);
        m_imageData = result.imageData;
        if (result.windowLevelPreset.isValid) {
            m_axialPanel->setRecommendedWindowLevel(result.windowLevelPreset.window, result.windowLevelPreset.level);
            m_coronalPanel->setRecommendedWindowLevel(result.windowLevelPreset.window, result.windowLevelPreset.level);
            m_sagittalPanel->setRecommendedWindowLevel(result.windowLevelPreset.window, result.windowLevelPreset.level);
        } else {
            m_axialPanel->clearRecommendedWindowLevel();
            m_coronalPanel->clearRecommendedWindowLevel();
            m_sagittalPanel->clearRecommendedWindowLevel();
        }

        m_axialPanel->setImageData(m_imageData);
        m_coronalPanel->setImageData(m_imageData);
        m_sagittalPanel->setImageData(m_imageData);
    } else {
        m_hasDicomImage = false;
        setCrosshairEnabled(false);
        const QString dicomText = QStringLiteral("未发现 DICOM 序列");
        m_axialPanel->clearRecommendedWindowLevel();
        m_coronalPanel->clearRecommendedWindowLevel();
        m_sagittalPanel->clearRecommendedWindowLevel();
        m_axialPanel->clearView(dicomText);
        m_coronalPanel->clearView(dicomText);
        m_sagittalPanel->clearView(dicomText);
    }

    m_objectList->clear();
    m_volumePanel->clearScene(result.models.empty()
                                  ? QStringLiteral("未发现模型文件")
                                  : QStringLiteral("已发现 %1 个模型文件").arg(static_cast<int>(result.models.size())));
    for (const LoadedModelData &model : result.models) {
        auto *item = new QListWidgetItem(model.filePath, m_objectList);
        item->setCheckState(Qt::Checked);
        m_volumePanel->addModelData(model.filePath, model.polyData);
    }

    updateSummary(result.package);
    m_rootLayout->setCurrentWidget(m_contentPage);

    if (result.imageData != nullptr) {
        QTimer::singleShot(0, this, [this]() {
            m_axialPanel->refreshView();
            m_coronalPanel->refreshView();
            m_sagittalPanel->refreshView();
        });
    }

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

    auto *rightPanel = new QWidget(m_contentPage);
    m_objectList = new QListWidget(rightPanel);
    m_summaryLabel = new QLabel(QStringLiteral("尚未加载病例包"), rightPanel);
    m_crosshairToggleButton = new QPushButton(QStringLiteral("十字线定位: 关"), rightPanel);
    m_crosshairToggleButton->setCheckable(true);
    m_crosshairToggleButton->setChecked(false);
    m_crosshairToggleButton->setEnabled(false);

    connect(m_axialPanel, &MprViewWidget::cursorWorldPositionChanged, this, &FourPaneViewer::syncMprCursor);
    connect(m_coronalPanel, &MprViewWidget::cursorWorldPositionChanged, this, &FourPaneViewer::syncMprCursor);
    connect(m_sagittalPanel, &MprViewWidget::cursorWorldPositionChanged, this, &FourPaneViewer::syncMprCursor);
    connect(m_crosshairToggleButton, &QPushButton::toggled, this, &FourPaneViewer::handleCrosshairToggle);

    auto *viewArea = new SplitterGridWidget(
        m_axialPanel,
        m_coronalPanel,
        m_sagittalPanel,
        m_volumePanel,
        m_contentPage);

    m_objectList->setAlternatingRowColors(true);

    auto *rightPanelTitle = new QLabel(QStringLiteral("场景对象"), rightPanel);
    rightPanelTitle->setStyleSheet(QStringLiteral("font-size: 12pt; font-weight: 700;"));

    auto *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(12, 12, 12, 12);
    rightLayout->setSpacing(8);
    rightLayout->addWidget(rightPanelTitle);
    rightLayout->addWidget(m_crosshairToggleButton);
    rightLayout->addWidget(m_objectList, 1);
    rightLayout->addWidget(m_summaryLabel);

    auto *rootSplitter = new QSplitter(Qt::Horizontal, m_contentPage);
    rootSplitter->addWidget(viewArea);
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

void FourPaneViewer::setCrosshairEnabled(bool enabled)
{
    const bool actualEnabled = enabled && m_hasDicomImage;
    m_crosshairEnabled = actualEnabled;

    if (m_crosshairToggleButton != nullptr) {
        m_crosshairToggleButton->blockSignals(true);
        m_crosshairToggleButton->setEnabled(m_hasDicomImage);
        m_crosshairToggleButton->setChecked(actualEnabled);
        m_crosshairToggleButton->setText(actualEnabled
                                             ? QStringLiteral("十字线定位: 开")
                                             : QStringLiteral("十字线定位: 关"));
        m_crosshairToggleButton->blockSignals(false);
    }

    if (m_axialPanel != nullptr) {
        m_axialPanel->setCrosshairEnabled(actualEnabled);
    }
    if (m_coronalPanel != nullptr) {
        m_coronalPanel->setCrosshairEnabled(actualEnabled);
    }
    if (m_sagittalPanel != nullptr) {
        m_sagittalPanel->setCrosshairEnabled(actualEnabled);
    }
}

void FourPaneViewer::handleCrosshairToggle(bool checked)
{
    setCrosshairEnabled(checked);
    if (!m_crosshairEnabled || m_axialPanel == nullptr) {
        return;
    }

    const auto initialCursor = m_axialPanel->cursorWorldPosition();
    syncMprCursor(initialCursor[0], initialCursor[1], initialCursor[2]);
}

void FourPaneViewer::syncMprCursor(double x, double y, double z)
{
    if (!m_crosshairEnabled
        || m_syncingMprCursor
        || m_axialPanel == nullptr
        || m_coronalPanel == nullptr
        || m_sagittalPanel == nullptr) {
        return;
    }

    m_syncingMprCursor = true;
    m_axialPanel->setCursorWorldPosition(x, y, z);
    m_coronalPanel->setCursorWorldPosition(x, y, z);
    m_sagittalPanel->setCursorWorldPosition(x, y, z);
    m_syncingMprCursor = false;
}
