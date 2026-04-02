#pragma once

#include <QWidget>

class QResizeEvent;
class QSplitter;

class SplitterGridWidget final : public QWidget
{
public:
    SplitterGridWidget(QWidget *topLeft,
                       QWidget *topRight,
                       QWidget *bottomLeft,
                       QWidget *bottomRight,
                       QWidget *parent = nullptr);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void configureSplitter(QSplitter *splitter);
    void updateColumnRatio(QSplitter *source);
    void applyColumnRatio(QSplitter *sourceToSkip);
    void updateRowRatio();
    void applyRowRatio();

    QSplitter *m_topRowSplitter;
    QSplitter *m_bottomRowSplitter;
    QSplitter *m_rowSplitter;
    double m_columnRatio = 0.5;
    double m_rowRatio = 0.5;
    bool m_syncingColumns = false;
    bool m_syncingRows = false;
};
