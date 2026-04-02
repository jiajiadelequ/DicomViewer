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

void SplitterGridWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
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
