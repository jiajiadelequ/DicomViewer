#pragma once

#include "src/core/runtime/studyloadresult.h"

#include <QWidget>
#include <vtkSmartPointer.h>

class FourPaneContentWidget;
class QStackedLayout;
class ViewerStateWidget;
class vtkImageData;

class FourPaneViewer final : public QWidget
{
    Q_OBJECT

public:
    explicit FourPaneViewer(QWidget *parent = nullptr);

    bool applyStudyLoadResult(const StudyLoadResult &result, QString *errorMessage = nullptr);
    void showEmptyState();
    void showLoadingState(const QString &message);
    void showErrorState(const QString &message);

private:
    void ensureContentPage();
    void setCrosshairEnabled(bool enabled);
    void setRulerEnabled(bool enabled);
    void setClippingEnabled(bool enabled);
    void handleCrosshairToggle(bool checked);
    void handleRulerToggle(bool checked);
    void handleClippingToggle(bool checked);
    void handleObjectVisibilityChanged(int index, bool visible);
    void syncCrosshairPosition(double x, double y, double z);
    void syncWindowLevel(double window, double level);

    QStackedLayout *m_rootLayout;
    ViewerStateWidget *m_statePage;
    FourPaneContentWidget *m_contentPage;
    vtkSmartPointer<vtkImageData> m_imageData;
    bool m_hasImageData = false;
    bool m_hasModelData = false;
    bool m_crosshairEnabled = false;
    bool m_rulerEnabled = false;
    bool m_clippingEnabled = false;
    bool m_syncingCrosshair = false;
    bool m_syncingWindowLevel = false;
};
