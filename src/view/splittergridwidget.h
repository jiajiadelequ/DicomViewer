#pragma once

#include <QWidget>

class QResizeEvent;
class QSplitter;

// 通过共享列比例和行比例，保证可拖拽的 2x2 视图区始终保持对齐。
class SplitterGridWidget final : public QWidget
{
    Q_OBJECT

public:
    SplitterGridWidget(QWidget *topLeft,
                       QWidget *topRight,
                       QWidget *bottomLeft,
                       QWidget *bottomRight,
                       QWidget *parent = nullptr);
    void toggleMaximizedPane(QWidget *pane);
    [[nodiscard]] bool isPaneMaximized(QWidget *pane) const;

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void configureSplitter(QSplitter *splitter);
    void updateColumnRatio(QSplitter *source);
    void applyColumnRatio(QSplitter *sourceToSkip);
    void updateRowRatio();
    void applyRowRatio();
    void applyMaximizedLayout();
    void restoreGridLayout();

    QWidget *m_topLeftPane;
    QWidget *m_topRightPane;
    QWidget *m_bottomLeftPane;
    QWidget *m_bottomRightPane;
    QSplitter *m_topRowSplitter;
    QSplitter *m_bottomRowSplitter;
    QSplitter *m_rowSplitter;
    double m_columnRatio = 0.5;
    double m_rowRatio = 0.5;
    bool m_syncingColumns = false;
    bool m_syncingRows = false;
    QWidget *m_maximizedPane = nullptr;
};
