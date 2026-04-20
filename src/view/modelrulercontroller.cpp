#include "modelrulercontroller.h"

#include <QPoint>

#include <QVTKOpenGLNativeWidget.h>

#include <vtkActor.h>
#include <vtkBillboardTextActor3D.h>
#include <vtkCellArray.h>
#include <vtkCellPicker.h>
#include <vtkCoordinate.h>
#include <vtkNew.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderer.h>
#include <vtkTextProperty.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
constexpr double kNodePixelTolerance = 12.0;
constexpr int kTextOffsetX = 12;
constexpr int kTextOffsetY = 12;
constexpr float kProjectedNodeRadiusPixels = 8.0f;
constexpr float kEstimatedTextCharWidthPixels = 10.0f;
constexpr float kEstimatedTextHeightPixels = 24.0f;

vtkSmartPointer<vtkBillboardTextActor3D> createTextActor()
{
    auto textActor = vtkSmartPointer<vtkBillboardTextActor3D>::New();
    textActor->SetInput("");
    textActor->GetTextProperty()->SetFontSize(18);
    textActor->GetTextProperty()->SetBold(true);
    textActor->GetTextProperty()->SetColor(0.20, 0.95, 0.72);
    textActor->GetTextProperty()->SetBackgroundColor(0.05, 0.05, 0.06);
    textActor->GetTextProperty()->SetBackgroundOpacity(0.55);
    textActor->GetTextProperty()->SetFrame(true);
    textActor->GetTextProperty()->SetFrameColor(0.20, 0.95, 0.72);
    textActor->SetDisplayOffset(kTextOffsetX, kTextOffsetY);
    textActor->SetVisibility(false);
    return textActor;
}
}

ModelRulerController::ModelRulerController(vtkRenderer *renderer)
    : m_renderer(renderer)
    , m_actor(vtkSmartPointer<vtkActor>::New())
    , m_polyData(vtkSmartPointer<vtkPolyData>::New())
    , m_points(vtkSmartPointer<vtkPoints>::New())
    , m_modelPicker(vtkSmartPointer<vtkCellPicker>::New())
{
    m_polyData->SetPoints(m_points);
    m_polyData->SetLines(vtkSmartPointer<vtkCellArray>::New());
    m_polyData->SetVerts(vtkSmartPointer<vtkCellArray>::New());

    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputData(m_polyData);
    m_actor->SetMapper(mapper);
    m_actor->GetProperty()->SetColor(0.20, 0.95, 0.72);
    m_actor->GetProperty()->SetLineWidth(3.0f);
    m_actor->GetProperty()->SetPointSize(12.0f);
    m_actor->GetProperty()->RenderLinesAsTubesOn();
    m_actor->GetProperty()->RenderPointsAsSpheresOn();
    m_actor->GetProperty()->LightingOff();
    m_actor->PickableOff();
    m_actor->ForceOpaqueOn();
    m_actor->SetVisibility(false);

    m_modelPicker->PickFromListOn();
    m_modelPicker->SetTolerance(0.0005);
}

vtkActor *ModelRulerController::actor() const
{
    return m_actor;
}

void ModelRulerController::addModelActor(vtkSmartPointer<vtkActor> actor)
{
    if (actor == nullptr) {
        return;
    }

    m_modelActors.push_back(actor);
    m_modelPicker->AddPickList(actor);
}

void ModelRulerController::clearScene()
{
    clear();
    m_modelActors.clear();
    m_modelPicker->InitializePickList();
    m_textActors.clear();
}

void ModelRulerController::setEnabled(bool enabled)
{
    m_enabled = enabled;
    if (!m_enabled) {
        clear();
        m_actor->SetVisibility(false);
    }
}

bool ModelRulerController::isEnabled() const
{
    return m_enabled;
}

bool ModelRulerController::isMeasuring() const
{
    return m_activeMeasurementIndex.has_value() && m_hoverActive;
}

bool ModelRulerController::isDraggingNode() const
{
    return m_draggedNode.has_value();
}

void ModelRulerController::setVisible(bool visible)
{
    const bool shouldShow = visible && m_enabled;
    m_actor->SetVisibility(shouldShow);
    for (const auto &textActor : m_textActors) {
        const char *input = textActor->GetInput();
        textActor->SetVisibility(shouldShow && input != nullptr && input[0] != '\0');
    }
}

void ModelRulerController::clear()
{
    m_measurements.clear();
    m_projectedSegments.clear();
    m_projectedNodes.clear();
    m_projectedTextRects.clear();
    m_hoverWorldPosition = Axis { 0.0, 0.0, 0.0 };
    m_hoverActive = false;
    m_activeMeasurementIndex.reset();
    m_draggedNode.reset();
    m_points->Reset();
    m_polyData->SetLines(vtkSmartPointer<vtkCellArray>::New());
    m_polyData->SetVerts(vtkSmartPointer<vtkCellArray>::New());
    m_points->Modified();
    m_polyData->Modified();
    for (const auto &textActor : m_textActors) {
        textActor->SetInput("");
        textActor->SetVisibility(false);
    }
}

void ModelRulerController::finishMeasurement()
{
    m_hoverActive = false;
    m_activeMeasurementIndex.reset();
}

bool ModelRulerController::recordPoint(QVTKOpenGLNativeWidget *viewport, const QPoint &position)
{
    if (!m_enabled) {
        return false;
    }

    Axis worldPosition;
    if (!pickWorldPosition(viewport, position, &worldPosition)) {
        return false;
    }

    Measurement *measurement = nullptr;
    if (m_activeMeasurementIndex.has_value() && *m_activeMeasurementIndex < m_measurements.size()) {
        measurement = &m_measurements[*m_activeMeasurementIndex];
    } else {
        m_measurements.push_back(Measurement {});
        m_activeMeasurementIndex = m_measurements.size() - 1;
        measurement = &m_measurements.back();
    }

    measurement->pointsWorld.push_back(worldPosition);
    m_hoverWorldPosition = worldPosition;
    m_hoverActive = true;
    return true;
}

bool ModelRulerController::updateHoverPoint(QVTKOpenGLNativeWidget *viewport, const QPoint &position)
{
    if (!m_enabled || !isMeasuring()) {
        return false;
    }

    Axis worldPosition;
    if (!pickWorldPosition(viewport, position, &worldPosition)) {
        return false;
    }

    m_hoverWorldPosition = worldPosition;
    m_hoverActive = true;
    return true;
}

bool ModelRulerController::beginNodeDrag(QVTKOpenGLNativeWidget *viewport, const QPoint &position)
{
    if (!m_enabled || isMeasuring()) {
        return false;
    }

    const auto node = findNearbyNode(viewport, position);
    if (!node.has_value()) {
        return false;
    }

    Axis worldPosition;
    if (!pickWorldPosition(viewport, position, &worldPosition)) {
        return false;
    }

    const auto [measurementIndex, pointIndex] = *node;
    if (measurementIndex >= m_measurements.size() || pointIndex >= m_measurements[measurementIndex].pointsWorld.size()) {
        return false;
    }

    m_measurements[measurementIndex].pointsWorld[pointIndex] = worldPosition;
    m_draggedNode = node;
    return true;
}

bool ModelRulerController::updateNodeDrag(QVTKOpenGLNativeWidget *viewport, const QPoint &position)
{
    if (!m_draggedNode.has_value()) {
        return false;
    }

    const auto [measurementIndex, pointIndex] = *m_draggedNode;
    if (measurementIndex >= m_measurements.size()) {
        m_draggedNode.reset();
        return false;
    }

    Measurement &measurement = m_measurements[measurementIndex];
    if (pointIndex >= measurement.pointsWorld.size()) {
        m_draggedNode.reset();
        return false;
    }

    Axis worldPosition;
    if (!pickWorldPosition(viewport, position, &worldPosition)) {
        return false;
    }

    measurement.pointsWorld[pointIndex] = worldPosition;
    return true;
}

bool ModelRulerController::endNodeDrag()
{
    const bool hadDrag = m_draggedNode.has_value();
    m_draggedNode.reset();
    return hadDrag;
}

void ModelRulerController::updateGeometry()
{
    auto lineCells = vtkSmartPointer<vtkCellArray>::New();
    auto nodeCells = vtkSmartPointer<vtkCellArray>::New();
    m_points->Reset();

    if (!m_enabled) {
        m_polyData->SetLines(lineCells);
        m_polyData->SetVerts(nodeCells);
        m_points->Modified();
        m_polyData->Modified();
        m_actor->SetVisibility(false);
        for (const auto &textActor : m_textActors) {
            textActor->SetInput("");
            textActor->SetVisibility(false);
        }
        m_projectedSegments.clear();
        m_projectedNodes.clear();
        m_projectedTextRects.clear();
        return;
    }

    bool hasVisibleMeasurement = false;
    const auto appendPoint = [this](const Axis &worldPosition) {
        return m_points->InsertNextPoint(worldPosition[0], worldPosition[1], worldPosition[2]);
    };

    for (std::size_t measurementIndex = 0; measurementIndex < m_measurements.size(); ++measurementIndex) {
        const Measurement &measurement = m_measurements[measurementIndex];
        if (measurement.pointsWorld.empty()) {
            continue;
        }

        hasVisibleMeasurement = true;
        vtkIdType previousPointId = appendPoint(measurement.pointsWorld.front());
        nodeCells->InsertNextCell(1);
        nodeCells->InsertCellPoint(previousPointId);
        for (std::size_t pointIndex = 1; pointIndex < measurement.pointsWorld.size(); ++pointIndex) {
            const vtkIdType currentPointId = appendPoint(measurement.pointsWorld[pointIndex]);
            nodeCells->InsertNextCell(1);
            nodeCells->InsertCellPoint(currentPointId);
            lineCells->InsertNextCell(2);
            lineCells->InsertCellPoint(previousPointId);
            lineCells->InsertCellPoint(currentPointId);
            previousPointId = currentPointId;
        }

        if (m_hoverActive
            && m_activeMeasurementIndex.has_value()
            && measurementIndex == *m_activeMeasurementIndex) {
            const vtkIdType hoverPointId = appendPoint(m_hoverWorldPosition);
            lineCells->InsertNextCell(2);
            lineCells->InsertCellPoint(previousPointId);
            lineCells->InsertCellPoint(hoverPointId);
        }
    }

    m_polyData->SetLines(lineCells);
    m_polyData->SetVerts(nodeCells);
    m_points->Modified();
    m_polyData->Modified();
    m_actor->SetVisibility(hasVisibleMeasurement);
    updateProjectedOverlays();
    updateTextOverlays();
}

void ModelRulerController::refreshProjectedOverlays()
{
    if (m_renderer == nullptr) {
        m_projectedSegments.clear();
        m_projectedNodes.clear();
        m_projectedTextRects.clear();
        return;
    }

    updateProjectedOverlays();

    // The shader-side occlusion test needs screen-space text bounds that stay in sync
    // with camera motion, even when the ruler geometry itself did not change.
    m_projectedTextRects.clear();
    for (const Measurement &measurement : m_measurements) {
        if (measurement.pointsWorld.empty()) {
            continue;
        }

        Axis anchorWorld = measurement.pointsWorld.back();
        const bool includeHover = m_hoverActive
            && m_activeMeasurementIndex.has_value()
            && *m_activeMeasurementIndex < m_measurements.size()
            && &m_measurements[*m_activeMeasurementIndex] == &measurement;
        if (includeHover) {
            anchorWorld = m_hoverWorldPosition;
        }

        std::array<float, 2> anchorDisplay;
        float anchorDepth = 1.0f;
        if (!worldToDisplay(anchorWorld, &anchorDisplay, &anchorDepth)) {
            continue;
        }

        const QByteArray labelText = QStringLiteral("%1 mm")
                                         .arg(totalLength(measurement, includeHover), 0, 'f', 2)
                                         .toUtf8();
        const float estimatedWidth = std::max(60.0f,
                                              static_cast<float>(labelText.size()) * kEstimatedTextCharWidthPixels);
        const float estimatedHeight = kEstimatedTextHeightPixels;
        const float minX = anchorDisplay[0] + static_cast<float>(kTextOffsetX);
        const float minY = anchorDisplay[1] + static_cast<float>(kTextOffsetY);
        m_projectedTextRects.push_back(ProjectedTextRect { { minX, minY },
                                                           { minX + estimatedWidth, minY + estimatedHeight },
                                                           anchorDepth });
    }
}

const std::vector<ModelRulerController::ProjectedSegment> &ModelRulerController::projectedSegments() const
{
    return m_projectedSegments;
}

const std::vector<ModelRulerController::ProjectedNode> &ModelRulerController::projectedNodes() const
{
    return m_projectedNodes;
}

const std::vector<ModelRulerController::ProjectedTextRect> &ModelRulerController::projectedTextRects() const
{
    return m_projectedTextRects;
}

bool ModelRulerController::pickWorldPosition(QVTKOpenGLNativeWidget *viewport,
                                             const QPoint &widgetPosition,
                                             Axis *worldPosition) const
{
    if (viewport == nullptr || worldPosition == nullptr || m_modelActors.empty() || m_renderer == nullptr) {
        return false;
    }

    const double devicePixelRatio = viewport->devicePixelRatioF();
    const int displayX = static_cast<int>(std::lround(widgetPosition.x() * devicePixelRatio));
    const int displayY = static_cast<int>(std::lround((viewport->height() - 1 - widgetPosition.y()) * devicePixelRatio));
    if (m_modelPicker->Pick(displayX, displayY, 0.0, m_renderer) == 0) {
        return false;
    }

    double pickedPosition[3] { 0.0, 0.0, 0.0 };
    m_modelPicker->GetPickPosition(pickedPosition);
    *worldPosition = Axis { pickedPosition[0], pickedPosition[1], pickedPosition[2] };
    return true;
}

std::optional<std::pair<std::size_t, std::size_t>> ModelRulerController::findNearbyNode(QVTKOpenGLNativeWidget *viewport,
                                                                                         const QPoint &position) const
{
    if (viewport == nullptr || m_renderer == nullptr) {
        return std::nullopt;
    }

    const double devicePixelRatio = viewport->devicePixelRatioF();
    const double targetDisplayX = position.x() * devicePixelRatio;
    const double targetDisplayY = (viewport->height() - 1 - position.y()) * devicePixelRatio;
    const double toleranceSquared = kNodePixelTolerance * kNodePixelTolerance;

    double bestDistanceSquared = std::numeric_limits<double>::max();
    std::optional<std::pair<std::size_t, std::size_t>> bestNode;
    for (std::size_t measurementIndex = 0; measurementIndex < m_measurements.size(); ++measurementIndex) {
        const Measurement &measurement = m_measurements[measurementIndex];
        for (std::size_t pointIndex = 0; pointIndex < measurement.pointsWorld.size(); ++pointIndex) {
            const Axis &worldPoint = measurement.pointsWorld[pointIndex];
            vtkNew<vtkCoordinate> coordinate;
            coordinate->SetCoordinateSystemToWorld();
            coordinate->SetValue(worldPoint[0], worldPoint[1], worldPoint[2]);
            const double *displayValue = coordinate->GetComputedDoubleDisplayValue(m_renderer);

            const double dx = displayValue[0] - targetDisplayX;
            const double dy = displayValue[1] - targetDisplayY;
            const double distanceSquared = dx * dx + dy * dy;
            if (distanceSquared <= toleranceSquared && distanceSquared < bestDistanceSquared) {
                bestDistanceSquared = distanceSquared;
                bestNode = std::make_pair(measurementIndex, pointIndex);
            }
        }
    }

    return bestNode;
}

void ModelRulerController::ensureTextActors(std::size_t count)
{
    if (m_renderer == nullptr) {
        return;
    }

    while (m_textActors.size() < count) {
        auto textActor = createTextActor();
        textActor->ForceOpaqueOn();
        m_renderer->AddActor(textActor);
        m_textActors.push_back(textActor);
    }
}

double ModelRulerController::totalLength(const Measurement &measurement, bool includeHover) const
{
    if (measurement.pointsWorld.empty()) {
        return 0.0;
    }

    double length = 0.0;
    for (std::size_t index = 1; index < measurement.pointsWorld.size(); ++index) {
        length += segmentLength(measurement.pointsWorld[index - 1], measurement.pointsWorld[index]);
    }

    if (includeHover
        && m_hoverActive
        && m_activeMeasurementIndex.has_value()
        && *m_activeMeasurementIndex < m_measurements.size()
        && &m_measurements[*m_activeMeasurementIndex] == &measurement) {
        length += segmentLength(measurement.pointsWorld.back(), m_hoverWorldPosition);
    }

    return length;
}

double ModelRulerController::segmentLength(const Axis &lhs, const Axis &rhs)
{
    const double dx = rhs[0] - lhs[0];
    const double dy = rhs[1] - lhs[1];
    const double dz = rhs[2] - lhs[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

bool ModelRulerController::worldToDisplay(const Axis &worldPosition,
                                          std::array<float, 2> *displayPosition,
                                          float *depth) const
{
    if (m_renderer == nullptr || displayPosition == nullptr || depth == nullptr) {
        return false;
    }

    m_renderer->SetWorldPoint(worldPosition[0], worldPosition[1], worldPosition[2], 1.0);
    m_renderer->WorldToDisplay();
    const double *displayValue = m_renderer->GetDisplayPoint();
    displayPosition->at(0) = static_cast<float>(displayValue[0]);
    displayPosition->at(1) = static_cast<float>(displayValue[1]);
    *depth = static_cast<float>(displayValue[2]);
    return true;
}

void ModelRulerController::updateProjectedOverlays()
{
    m_projectedSegments.clear();
    m_projectedNodes.clear();
    m_projectedTextRects.clear();

    for (std::size_t measurementIndex = 0; measurementIndex < m_measurements.size(); ++measurementIndex) {
        const Measurement &measurement = m_measurements[measurementIndex];
        if (measurement.pointsWorld.empty()) {
            continue;
        }

        std::vector<std::array<float, 2>> displayPoints;
        std::vector<float> depths;
        displayPoints.reserve(measurement.pointsWorld.size() + 1);
        depths.reserve(measurement.pointsWorld.size() + 1);

        for (const Axis &worldPoint : measurement.pointsWorld) {
            std::array<float, 2> displayPoint;
            float depth = 1.0f;
            if (!worldToDisplay(worldPoint, &displayPoint, &depth)) {
                continue;
            }

            displayPoints.push_back(displayPoint);
            depths.push_back(depth);
            m_projectedNodes.push_back(ProjectedNode { displayPoint, depth, kProjectedNodeRadiusPixels });
        }

        for (std::size_t pointIndex = 1; pointIndex < displayPoints.size(); ++pointIndex) {
            m_projectedSegments.push_back(ProjectedSegment { displayPoints[pointIndex - 1],
                                                             displayPoints[pointIndex],
                                                             depths[pointIndex - 1],
                                                             depths[pointIndex] });
        }

        if (m_hoverActive
            && m_activeMeasurementIndex.has_value()
            && measurementIndex == *m_activeMeasurementIndex
            && !measurement.pointsWorld.empty()) {
            std::array<float, 2> hoverDisplay;
            float hoverDepth = 1.0f;
            if (worldToDisplay(m_hoverWorldPosition, &hoverDisplay, &hoverDepth)) {
                m_projectedSegments.push_back(ProjectedSegment { displayPoints.back(),
                                                                 hoverDisplay,
                                                                 depths.back(),
                                                                 hoverDepth });
            }
        }
    }
}

void ModelRulerController::updateTextOverlays()
{
    m_projectedTextRects.clear();
    std::vector<const Measurement *> visibleMeasurements;
    visibleMeasurements.reserve(m_measurements.size());
    for (const Measurement &measurement : m_measurements) {
        if (!measurement.pointsWorld.empty()) {
            visibleMeasurements.push_back(&measurement);
        }
    }

    ensureTextActors(visibleMeasurements.size());
    if (visibleMeasurements.empty()) {
        for (const auto &textActor : m_textActors) {
            textActor->SetInput("");
            textActor->SetVisibility(false);
        }
        return;
    }

    std::size_t textActorIndex = 0;
    for (const Measurement *measurement : visibleMeasurements) {
        Axis anchorWorld = measurement->pointsWorld.back();
        const bool includeHover = m_hoverActive
            && m_activeMeasurementIndex.has_value()
            && *m_activeMeasurementIndex < m_measurements.size()
            && &m_measurements[*m_activeMeasurementIndex] == measurement;
        if (includeHover) {
            anchorWorld = m_hoverWorldPosition;
        }

        auto &textActor = m_textActors[textActorIndex++];
        const QByteArray labelText = QStringLiteral("%1 mm")
                                         .arg(totalLength(*measurement, includeHover), 0, 'f', 2)
                                         .toUtf8();
        textActor->SetInput(labelText.constData());
        textActor->SetPosition(anchorWorld[0], anchorWorld[1], anchorWorld[2]);
        textActor->SetVisibility(m_enabled);

        std::array<float, 2> anchorDisplay;
        float anchorDepth = 1.0f;
        if (worldToDisplay(anchorWorld, &anchorDisplay, &anchorDepth)) {
            const float estimatedWidth = std::max(60.0f,
                                                  static_cast<float>(labelText.size()) * kEstimatedTextCharWidthPixels);
            const float estimatedHeight = kEstimatedTextHeightPixels;
            const float minX = anchorDisplay[0] + static_cast<float>(kTextOffsetX);
            const float minY = anchorDisplay[1] + static_cast<float>(kTextOffsetY);
            m_projectedTextRects.push_back(ProjectedTextRect { { minX, minY },
                                                               { minX + estimatedWidth, minY + estimatedHeight },
                                                               anchorDepth });
        }
    }

    for (; textActorIndex < m_textActors.size(); ++textActorIndex) {
        m_textActors[textActorIndex]->SetInput("");
        m_textActors[textActorIndex]->SetVisibility(false);
    }
}
