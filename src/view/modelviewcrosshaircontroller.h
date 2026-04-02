#pragma once

#include <array>
#include <vector>

#include <vtkSmartPointer.h>

class QPoint;
class QVTKOpenGLNativeWidget;
class vtkActor;
class vtkCellPicker;
class vtkCursor3D;
class vtkImageData;
class vtkPolyData;
class vtkRenderer;

class ModelViewCrosshairController final
{
public:
    using Axis = std::array<double, 3>;

    explicit ModelViewCrosshairController(vtkRenderer *renderer);

    void clearScene();
    void addModelActor(vtkSmartPointer<vtkActor> actor, vtkPolyData *polyData);
    void setReferenceImageData(vtkImageData *imageData);
    void setEnabled(bool enabled);
    void setCursorWorldPosition(const Axis &worldPosition);
    [[nodiscard]] Axis cursorWorldPosition() const;
    void updateGeometry();
    [[nodiscard]] bool cameraBounds(double bounds[6]) const;

    bool beginInteraction(QVTKOpenGLNativeWidget *viewport, const QPoint &position, Axis *worldPosition);
    bool updateInteraction(QVTKOpenGLNativeWidget *viewport, bool leftButtonDown, const QPoint &position, Axis *worldPosition);
    bool endInteraction(bool leftButtonReleased);

private:
    [[nodiscard]] bool pickWorldPosition(QVTKOpenGLNativeWidget *viewport,
                                         const QPoint &widgetPosition,
                                         Axis *worldPosition) const;
    [[nodiscard]] bool crosshairBounds(double bounds[6]) const;

    vtkRenderer *m_renderer;
    vtkSmartPointer<vtkActor> m_crosshairActor;
    vtkSmartPointer<vtkCursor3D> m_crosshairCursor;
    vtkSmartPointer<vtkCellPicker> m_modelPicker;
    std::vector<vtkSmartPointer<vtkActor>> m_modelActors;
    Axis m_cursorWorldPosition { 0.0, 0.0, 0.0 };
    std::array<double, 6> m_referenceBounds { 0.0, -1.0, 0.0, -1.0, 0.0, -1.0 };
    std::array<double, 6> m_modelBounds { 0.0, -1.0, 0.0, -1.0, 0.0, -1.0 };
    bool m_hasReferenceBounds = false;
    bool m_hasModelBounds = false;
    bool m_enabled = false;
    bool m_dragActive = false;
};
