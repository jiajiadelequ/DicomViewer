#pragma once

#include <QWidget>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;

// 把对象列表、病例摘要和十字线开关收敛到一个侧栏部件里。
class SceneSidebarWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit SceneSidebarWidget(QWidget *parent = nullptr);

    void clearObjects();
    void addObject(const QString &filePath);
    void setSummaryText(const QString &text);
    void setCrosshairState(bool available, bool enabled);
    void setRulerState(bool available, bool enabled);
    void setClippingState(bool available, bool enabled);

signals:
    void crosshairToggled(bool checked);
    void rulerToggled(bool checked);
    void clippingToggled(bool checked);
    void objectVisibilityChanged(int index, bool visible);

private:
    void handleItemChanged(QListWidgetItem *item);

    QListWidget *m_objectList;
    QLabel *m_summaryLabel;
    QPushButton *m_crosshairToggleButton;
    QPushButton *m_rulerToggleButton;
    QPushButton *m_clippingToggleButton;
};
