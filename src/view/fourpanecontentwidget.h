#pragma once

#include <QWidget>

class ModelViewWidget;
class MprViewWidget;
class SceneSidebarWidget;
class SplitterGridWidget;

// 负责稳定的 2x2 视图区布局，以及右侧场景侧栏的装配。
class FourPaneContentWidget final : public QWidget
{
public:
    explicit FourPaneContentWidget(QWidget *parent = nullptr);

    [[nodiscard]] MprViewWidget *axialPanel() const;
    [[nodiscard]] MprViewWidget *coronalPanel() const;
    [[nodiscard]] MprViewWidget *sagittalPanel() const;
    [[nodiscard]] ModelViewWidget *volumePanel() const;
    [[nodiscard]] SceneSidebarWidget *sidebarPanel() const;
    void toggleMaximizedPane(QWidget *pane);

private:
    void updatePaneMaximizeButtons();

    MprViewWidget *m_axialPanel;
    MprViewWidget *m_coronalPanel;
    MprViewWidget *m_sagittalPanel;
    ModelViewWidget *m_volumePanel;
    SceneSidebarWidget *m_sidebarPanel;
    SplitterGridWidget *m_viewArea;
};
