#pragma once

#include "mprslicemath.h"

#include <vtkSmartPointer.h>

class QPoint;
class QVTKOpenGLNativeWidget;
class vtkActor;
class vtkCellPicker;
class vtkImageActor;
class vtkPoints;
class vtkPolyData;
class vtkRenderer;

// 负责 MPR 十字线显示、切片平面拾取，以及拖拽定位状态。
class MprCrosshairController final
{
public:
    using Axis = MprSliceMath::Axis;
    using SliceGeometry = MprSliceMath::SliceGeometry;

    explicit MprCrosshairController(vtkImageActor *imageActor);

    [[nodiscard]] vtkActor *actor() const;
    void setEnabled(bool enabled);
    [[nodiscard]] bool isEnabled() const;
    void setVisible(bool visible);

    bool beginInteraction(vtkRenderer *renderer,
                          QVTKOpenGLNativeWidget *viewport,
                          const QPoint &position,
                          const SliceGeometry &sliceGeometry,
                          int sliderValue,
                          Axis *worldPosition);
    bool updateInteraction(vtkRenderer *renderer,
                           QVTKOpenGLNativeWidget *viewport,
                           bool leftButtonDown,
                           const QPoint &position,
                           const SliceGeometry &sliceGeometry,
                           int sliderValue,
                           Axis *worldPosition);
    bool endInteraction(bool leftButtonReleased);

    void updateGeometry(const Axis &cursorWorldPosition,
                        const SliceGeometry &sliceGeometry,
                        int sliderValue);

private:
    [[nodiscard]] bool pickWorldPosition(vtkRenderer *renderer,
                                         QVTKOpenGLNativeWidget *viewport,
                                         const QPoint &widgetPosition,
                                         const SliceGeometry &sliceGeometry,
                                         int sliderValue,
                                         Axis *worldPosition) const;

    vtkImageActor *m_imageActor;
    vtkSmartPointer<vtkActor> m_actor;
    vtkSmartPointer<vtkPolyData> m_polyData;
    vtkSmartPointer<vtkPoints> m_points;
    vtkSmartPointer<vtkCellPicker> m_imagePicker;
    bool m_enabled = false;
    bool m_dragActive = false;
};
