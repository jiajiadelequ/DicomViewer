#pragma once

#include "mprslicemath.h"

#include <QString>
#include <cstddef>
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
class vtkPolyDataMapper;
class vtkRenderer;
class vtkTextActor;

class MprRulerController final
{
public:
    using Axis = MprSliceMath::Axis;
    using SliceGeometry = MprSliceMath::SliceGeometry;

    explicit MprRulerController(vtkImageActor *imageActor);

    [[nodiscard]] vtkActor *actor() const;
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
    [[nodiscard]] bool beginNodeDrag(vtkRenderer *renderer,
                                     QVTKOpenGLNativeWidget *viewport,
                                     const QPoint &position,
                                     const SliceGeometry &sliceGeometry,
                                     int sliderValue);
    bool updateNodeDrag(vtkRenderer *renderer,
                        QVTKOpenGLNativeWidget *viewport,
                        const QPoint &position,
                        const SliceGeometry &sliceGeometry,
                        int sliderValue);
    [[nodiscard]] bool endNodeDrag();
    [[nodiscard]] bool isDraggingNode() const;
    void updateGeometry(vtkRenderer *renderer, const SliceGeometry &sliceGeometry, int sliderValue);
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
    [[nodiscard]] Measurement *activeMeasurement();
    [[nodiscard]] const Measurement *activeMeasurement() const;
    [[nodiscard]] Measurement *latestMeasurementForSlice(int sliceIndex);
    [[nodiscard]] const Measurement *latestMeasurementForSlice(int sliceIndex) const;
    [[nodiscard]] std::optional<std::pair<std::size_t, std::size_t>> findNearbyNode(vtkRenderer *renderer,
                                                                                    QVTKOpenGLNativeWidget *viewport,
                                                                                    const SliceGeometry &sliceGeometry,
                                                                                    int sliderValue,
                                                                                    const QPoint &position) const;
    void ensureTextActors(vtkRenderer *renderer, std::size_t count);
    [[nodiscard]] double totalLength(const Measurement &measurement, bool includeHover) const;
    [[nodiscard]] static double segmentLength(const Axis &lhs, const Axis &rhs);
    void updateTextOverlays(vtkRenderer *renderer, const SliceGeometry &sliceGeometry, int sliderValue);

    vtkImageActor *m_imageActor;
    vtkSmartPointer<vtkActor> m_actor;
    vtkSmartPointer<vtkActor> m_nodeActor;
    vtkSmartPointer<vtkPolyData> m_polyData;
    vtkSmartPointer<vtkPolyData> m_nodePolyData;
    vtkSmartPointer<vtkPoints> m_points;
    vtkSmartPointer<vtkPoints> m_nodePoints;
    vtkSmartPointer<vtkCellPicker> m_imagePicker;
    std::vector<vtkSmartPointer<vtkTextActor>> m_textActors;
    std::vector<Measurement> m_measurements;
    Axis m_hoverWorldPosition { 0.0, 0.0, 0.0 };
    bool m_enabled = false;
    bool m_hoverActive = false;
    int m_activeSliceIndex = -1;
    std::optional<std::size_t> m_activeMeasurementIndex;
    std::optional<std::pair<std::size_t, std::size_t>> m_draggedNode;
};
