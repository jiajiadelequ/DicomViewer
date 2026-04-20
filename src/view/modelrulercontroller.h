#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <vector>

#include <vtkSmartPointer.h>

class QPoint;
class QVTKOpenGLNativeWidget;
class vtkActor;
class vtkBillboardTextActor3D;
class vtkCellPicker;
class vtkPolyData;
class vtkPoints;
class vtkRenderer;

class ModelRulerController final
{
public:
    using Axis = std::array<double, 3>;
    struct ProjectedSegment
    {
        std::array<float, 2> startDisplay { 0.0f, 0.0f };
        std::array<float, 2> endDisplay { 0.0f, 0.0f };
        float startDepth = 1.0f;
        float endDepth = 1.0f;
    };
    struct ProjectedNode
    {
        std::array<float, 2> centerDisplay { 0.0f, 0.0f };
        float depth = 1.0f;
        float radiusPixels = 0.0f;
    };
    struct ProjectedTextRect
    {
        std::array<float, 2> minDisplay { 0.0f, 0.0f };
        std::array<float, 2> maxDisplay { 0.0f, 0.0f };
        float depth = 1.0f;
    };

    explicit ModelRulerController(vtkRenderer *renderer);

    [[nodiscard]] vtkActor *actor() const;
    void addModelActor(vtkSmartPointer<vtkActor> actor);
    void clearScene();
    void setEnabled(bool enabled);
    [[nodiscard]] bool isEnabled() const;
    [[nodiscard]] bool isMeasuring() const;
    [[nodiscard]] bool isDraggingNode() const;
    void setVisible(bool visible);
    void clear();
    void finishMeasurement();

    bool recordPoint(QVTKOpenGLNativeWidget *viewport, const QPoint &position);
    bool updateHoverPoint(QVTKOpenGLNativeWidget *viewport, const QPoint &position);
    [[nodiscard]] bool beginNodeDrag(QVTKOpenGLNativeWidget *viewport, const QPoint &position);
    bool updateNodeDrag(QVTKOpenGLNativeWidget *viewport, const QPoint &position);
    [[nodiscard]] bool endNodeDrag();
    void updateGeometry();
    void refreshProjectedOverlays();
    [[nodiscard]] const std::vector<ProjectedSegment> &projectedSegments() const;
    [[nodiscard]] const std::vector<ProjectedNode> &projectedNodes() const;
    [[nodiscard]] const std::vector<ProjectedTextRect> &projectedTextRects() const;

private:
    struct Measurement
    {
        std::vector<Axis> pointsWorld;
    };

    [[nodiscard]] bool pickWorldPosition(QVTKOpenGLNativeWidget *viewport,
                                         const QPoint &widgetPosition,
                                         Axis *worldPosition) const;
    [[nodiscard]] std::optional<std::pair<std::size_t, std::size_t>> findNearbyNode(QVTKOpenGLNativeWidget *viewport,
                                                                                    const QPoint &position) const;
    void ensureTextActors(std::size_t count);
    [[nodiscard]] double totalLength(const Measurement &measurement, bool includeHover) const;
    [[nodiscard]] static double segmentLength(const Axis &lhs, const Axis &rhs);
    [[nodiscard]] bool worldToDisplay(const Axis &worldPosition, std::array<float, 2> *displayPosition, float *depth) const;
    void updateProjectedOverlays();
    void updateTextOverlays();

    vtkRenderer *m_renderer;
    vtkSmartPointer<vtkActor> m_actor;
    vtkSmartPointer<vtkPolyData> m_polyData;
    vtkSmartPointer<vtkPoints> m_points;
    vtkSmartPointer<vtkCellPicker> m_modelPicker;
    std::vector<vtkSmartPointer<vtkActor>> m_modelActors;
    std::vector<vtkSmartPointer<vtkBillboardTextActor3D>> m_textActors;
    std::vector<Measurement> m_measurements;
    std::vector<ProjectedSegment> m_projectedSegments;
    std::vector<ProjectedNode> m_projectedNodes;
    std::vector<ProjectedTextRect> m_projectedTextRects;
    Axis m_hoverWorldPosition { 0.0, 0.0, 0.0 };
    bool m_enabled = false;
    bool m_hoverActive = false;
    std::optional<std::size_t> m_activeMeasurementIndex;
    std::optional<std::pair<std::size_t, std::size_t>> m_draggedNode;
};
