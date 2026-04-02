#include "scenesidebarwidget.h"

#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QVBoxLayout>

SceneSidebarWidget::SceneSidebarWidget(QWidget *parent)
    : QWidget(parent)
    , m_objectList(new QListWidget(this))
    , m_summaryLabel(new QLabel(QStringLiteral("尚未加载病例包"), this))
    , m_crosshairToggleButton(new QPushButton(QStringLiteral("十字线定位: 关"), this))
{
    // 侧栏内部自管显示细节，外层只负责给它喂数据和接收信号。
    auto *titleLabel = new QLabel(QStringLiteral("场景对象"), this);
    titleLabel->setStyleSheet(QStringLiteral("font-size: 12pt; font-weight: 700;"));

    m_objectList->setAlternatingRowColors(true);

    m_crosshairToggleButton->setCheckable(true);
    m_crosshairToggleButton->setChecked(false);
    m_crosshairToggleButton->setEnabled(false);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);
    layout->addWidget(titleLabel);
    layout->addWidget(m_crosshairToggleButton);
    layout->addWidget(m_objectList, 1);
    layout->addWidget(m_summaryLabel);

    connect(m_crosshairToggleButton, &QPushButton::toggled, this, &SceneSidebarWidget::crosshairToggled);
    connect(m_objectList, &QListWidget::itemChanged, this, &SceneSidebarWidget::handleItemChanged);
}

void SceneSidebarWidget::clearObjects()
{
    m_objectList->clear();
}

void SceneSidebarWidget::addObject(const QString &filePath)
{
    m_objectList->blockSignals(true);
    auto *item = new QListWidgetItem(filePath, m_objectList);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(Qt::Checked);
    m_objectList->blockSignals(false);
}

void SceneSidebarWidget::setSummaryText(const QString &text)
{
    m_summaryLabel->setText(text);
}

void SceneSidebarWidget::setCrosshairState(bool available, bool enabled)
{
    m_crosshairToggleButton->blockSignals(true);
    m_crosshairToggleButton->setEnabled(available);
    m_crosshairToggleButton->setChecked(enabled);
    m_crosshairToggleButton->setText(enabled
                                         ? QStringLiteral("十字线定位: 开")
                                         : QStringLiteral("十字线定位: 关"));
    m_crosshairToggleButton->blockSignals(false);
}

void SceneSidebarWidget::handleItemChanged(QListWidgetItem *item)
{
    if (item == nullptr) {
        return;
    }

    emit objectVisibilityChanged(m_objectList->row(item), item->checkState() == Qt::Checked);
}
