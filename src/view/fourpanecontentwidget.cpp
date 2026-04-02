#include "fourpanecontentwidget.h"

#include "modelviewwidget.h"
#include "mprviewwidget.h"
#include "scenesidebarwidget.h"
#include "splittergridwidget.h"

#include <QSplitter>
#include <QVBoxLayout>

FourPaneContentWidget::FourPaneContentWidget(QWidget *parent)
    : QWidget(parent)
    , m_axialPanel(new MprViewWidget(QStringLiteral("Axial MPR"), MprViewWidget::Orientation::Axial, this))
    , m_coronalPanel(new MprViewWidget(QStringLiteral("Coronal MPR"), MprViewWidget::Orientation::Coronal, this))
    , m_sagittalPanel(new MprViewWidget(QStringLiteral("Sagittal MPR"), MprViewWidget::Orientation::Sagittal, this))
    , m_volumePanel(new ModelViewWidget(this))
    , m_sidebarPanel(new SceneSidebarWidget(this))
{
    auto *viewArea = new SplitterGridWidget(
        m_axialPanel,
        m_coronalPanel,
        m_sagittalPanel,
        m_volumePanel,
        this);

    auto *rootSplitter = new QSplitter(Qt::Horizontal, this);
    rootSplitter->addWidget(viewArea);
    rootSplitter->addWidget(m_sidebarPanel);
    rootSplitter->setStretchFactor(0, 5);
    rootSplitter->setStretchFactor(1, 2);
    rootSplitter->setChildrenCollapsible(false);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(rootSplitter);
}

MprViewWidget *FourPaneContentWidget::axialPanel() const
{
    return m_axialPanel;
}

MprViewWidget *FourPaneContentWidget::coronalPanel() const
{
    return m_coronalPanel;
}

MprViewWidget *FourPaneContentWidget::sagittalPanel() const
{
    return m_sagittalPanel;
}

ModelViewWidget *FourPaneContentWidget::volumePanel() const
{
    return m_volumePanel;
}

SceneSidebarWidget *FourPaneContentWidget::sidebarPanel() const
{
    return m_sidebarPanel;
}
