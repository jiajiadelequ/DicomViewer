#pragma once

#include "mprslicemath.h"

#include <QString>
#include <optional>
#include <vector>

#include <vtkSmartPointer.h>

class QPoint;
class QVTKOpenGLNativeWidget;
class vtkActor;
class vtkCellPicker;
class vtkImageActor;
class vtkPoints;
class vtkPolyData;
class vtkRenderer;
class vtkTextActor;

class MprRulerController final
{
public:
    using Axis = MprSliceMath::Axis;
    using SliceGeometry = MprSliceMath::SliceGeometry;

    explicit MprRulerController(vtkImageActor *imageActor);

    [[nodiscard]] vtkActor *actor() const;
    [[nodiscard]] vtkTextActor *textActor() const;
    void setEnabled(bool enabled);
    [[nodiscard]] bool isEnabled() const;
    [[nodiscard]] bool isMeasuring() const;
    void setVisible(bool visible);
    void clear();
    void finishMeasurement();

    bool recordPoint(vtkRenderer *renderer,
                     QVTKOpenGLNativeWidget *viewport,
                     const QPoint &position,
                     const SliceGeometry &sliceGeometry,
                     int sliderValue);
    bool updateHoverPoint(vtkRenderer *renderer,
                          QVTKOpenGLNativeWidget *viewport,
                          const QPoint &position,
                          const SliceGeometry &sliceGeometry,
                          int sliderValue);
    void updateGeometry(const SliceGeometry &sliceGeometry, int sliderValue);
    [[nodiscard]] QString measurementSummary(int sliceIndex) const;

private:
    struct Measurement
    {
        int sliceIndex = -1;
        std::vector<Axis> pointsWorld;
    };

    [[nodiscard]] bool pickWorldPosition(vtkRenderer *renderer,
                                         QVTKOpenGLNativeWidget *viewport,
                                         const QPoint &widgetPosition,
                                         const SliceGeometry &sliceGeometry,
                                         int sliderValue,
                                         Axis *worldPosition) const;
    [[nodiscard]] Measurement *measurementForSlice(int sliceIndex);
    [[nodiscard]] const Measurement *measurementForSlice(int sliceIndex) const;
    [[nodiscard]] double totalLength(const Measurement &measurement, bool includeHover) const;
    [[nodiscard]] static double segmentLength(const Axis &lhs, const Axis &rhs);
    void updateTextOverlay(const SliceGeometry &sliceGeometry, int sliderValue);

    vtkImageActor *m_imageActor;
    vtkSmartPointer<vtkActor> m_actor;
    vtkSmartPointer<vtkTextActor> m_textActor;
    vtkSmartPointer<vtkPolyData> m_polyData;
    vtkSmartPointer<vtkPoints> m_points;
    vtkSmartPointer<vtkCellPicker> m_imagePicker;
    std::vector<Measurement> m_measurements;
    Axis m_hoverWorldPosition { 0.0, 0.0, 0.0 };
    bool m_enabled = false;
    bool m_hoverActive = false;
    int m_activeSliceIndex = -1;
};
