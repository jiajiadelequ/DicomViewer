#include "splittergridwidget.h"

#include <QResizeEvent>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>

SplitterGridWidget::SplitterGridWidget(QWidget *topLeft,
                                       QWidget *topRight,
                                       QWidget *bottomLeft,
                                       QWidget *bottomRight,
                                       QWidget *parent)
    : QWidget(parent)
    , m_topLeftPane(topLeft)
    , m_topRightPane(topRight)
    , m_bottomLeftPane(bottomLeft)
    , m_bottomRightPane(bottomRight)
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

void SplitterGridWidget::toggleMaximizedPane(QWidget *pane)
{
    if (pane == nullptr) {
        return;
    }

    if (m_maximizedPane == pane) {
        m_maximizedPane = nullptr;
        restoreGridLayout();
        return;
    }

    m_maximizedPane = pane;
    applyMaximizedLayout();
}

bool SplitterGridWidget::isPaneMaximized(QWidget *pane) const
{
    return pane != nullptr && m_maximizedPane == pane;
}

void SplitterGridWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_maximizedPane != nullptr) {
        applyMaximizedLayout();
        return;
    }
    applyRowRatio();
    applyColumnRatio(nullptr);
}

void SplitterGridWidget::configureSplitter(QSplitter *splitter)
{
    splitter->setChildrenCollapsible(false);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
}

void SplitterGridWidget::updateColumnRatio(QSplitter *source)
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

void SplitterGridWidget::applyColumnRatio(QSplitter *sourceToSkip)
{
    if (m_syncingColumns) {
        return;
    }

    m_syncingColumns = true;
    // 上下两排必须共用同一组左右比例，否则四宫格会出现错位。
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

void SplitterGridWidget::updateRowRatio()
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

void SplitterGridWidget::applyRowRatio()
{
    if (m_syncingRows) {
        return;
    }

    m_syncingRows = true;
    const int top = static_cast<int>(m_rowRatio * 1000.0);
    m_rowSplitter->setSizes(QList<int> { top, 1000 - top });
    m_syncingRows = false;
}

void SplitterGridWidget::applyMaximizedLayout()
{
    const bool topLeft = m_maximizedPane == m_topLeftPane;
    const bool topRight = m_maximizedPane == m_topRightPane;
    const bool bottomLeft = m_maximizedPane == m_bottomLeftPane;
    const bool bottomRight = m_maximizedPane == m_bottomRightPane;

    m_topLeftPane->setVisible(topLeft);
    m_topRightPane->setVisible(topRight);
    m_bottomLeftPane->setVisible(bottomLeft);
    m_bottomRightPane->setVisible(bottomRight);

    m_topRowSplitter->setVisible(topLeft || topRight);
    m_bottomRowSplitter->setVisible(bottomLeft || bottomRight);

    if (topLeft || topRight) {
        m_topRowSplitter->setSizes(QList<int> { topLeft ? 1000 : 0, topRight ? 1000 : 0 });
        m_rowSplitter->setSizes(QList<int> { 1000, 0 });
    } else if (bottomLeft || bottomRight) {
        m_bottomRowSplitter->setSizes(QList<int> { bottomLeft ? 1000 : 0, bottomRight ? 1000 : 0 });
        m_rowSplitter->setSizes(QList<int> { 0, 1000 });
    }
}

void SplitterGridWidget::restoreGridLayout()
{
    m_topLeftPane->show();
    m_topRightPane->show();
    m_bottomLeftPane->show();
    m_bottomRightPane->show();
    m_topRowSplitter->show();
    m_bottomRowSplitter->show();
    applyRowRatio();
    applyColumnRatio(nullptr);
}
