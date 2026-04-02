#pragma once

#include <array>
#include <functional>

#include <vtkSmartPointer.h>

class QToolButton;
class QWidget;
class vtkAxesActor;
class vtkOrientationMarkerWidget;
class vtkRenderer;
class vtkRenderWindowInteractor;

// 负责 3D 标准视角切换，以及右上角方向轴标记。
class ModelViewCameraController final
{
public:
    using BoundsArray = std::array<double, 6>;

    ModelViewCameraController(QWidget *buttonParent,
                              vtkRenderer *renderer,
                              vtkRenderWindowInteractor *interactor);
    ~ModelViewCameraController();

    [[nodiscard]] QToolButton *viewButton() const;
    void setBoundsProvider(std::function<bool(BoundsArray &bounds)> provider);
    void resetToAnatomicalView();
    void refreshCurrentView();

private:
    enum class StandardView
    {
        Coronal,
        Sagittal,
        Axial
    };

    void updateViewButtonText();
    bool applyStandardView(StandardView view);

    QToolButton *m_viewButton;
    vtkRenderer *m_renderer;
    vtkSmartPointer<vtkAxesActor> m_orientationAxes;
    vtkSmartPointer<vtkOrientationMarkerWidget> m_orientationMarkerWidget;
    std::function<bool(BoundsArray &bounds)> m_boundsProvider;
    StandardView m_currentStandardView = StandardView::Coronal;
};
