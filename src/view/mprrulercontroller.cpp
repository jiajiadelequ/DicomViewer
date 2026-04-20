#include "mprrulercontroller.h"

#include <QPoint>

#include <QVTKOpenGLNativeWidget.h>

#include <vtkActor.h>
#include <vtkCellArray.h>
#include <vtkCellPicker.h>
#include <vtkCoordinate.h>
#include <vtkImageActor.h>
#include <vtkNew.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderer.h>
#include <vtkTextActor.h>
#include <vtkTextProperty.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
constexpr double kOverlayDepth = 0.15;
constexpr double kNodePixelTolerance = 12.0;
constexpr int kTextOffsetX = 12;
constexpr int kTextOffsetY = 12;
constexpr int kTextPadding = 6;
constexpr int kTextSpacing = 8;

struct DisplayRect
{
    int left = 0;
    int bottom = 0;
    int right = 0;
    int top = 0;
};

bool rectsOverlap(const DisplayRect &lhs, const DisplayRect &rhs)
{
    return lhs.left < rhs.right
        && lhs.right > rhs.left
        && lhs.bottom < rhs.top
        && lhs.top > rhs.bottom;
}

vtkSmartPointer<vtkTextActor> createTextActor()
{
    auto textActor = vtkSmartPointer<vtkTextActor>::New();
    textActor->SetInput("");
    textActor->GetTextProperty()->SetFontSize(16);
    textActor->GetTextProperty()->SetBold(true);
    textActor->GetTextProperty()->SetColor(0.20, 0.95, 0.72);
    textActor->GetTextProperty()->SetBackgroundColor(0.05, 0.05, 0.06);
    textActor->GetTextProperty()->SetBackgroundOpacity(0.55);
    textActor->GetTextProperty()->SetFrame(true);
    textActor->GetTextProperty()->SetFrameColor(0.20, 0.95, 0.72);
    textActor->SetVisibility(false);
    return textActor;
}
}

MprRulerController::MprRulerController(vtkImageActor *imageActor)
    : m_imageActor(imageActor)
    , m_actor(vtkSmartPointer<vtkActor>::New())
    , m_polyData(vtkSmartPointer<vtkPolyData>::New())
    , m_points(vtkSmartPointer<vtkPoints>::New())
    , m_imagePicker(vtkSmartPointer<vtkCellPicker>::New())
{
    m_polyData->SetPoints(m_points);
    m_polyData->SetLines(vtkSmartPointer<vtkCellArray>::New());
    m_polyData->SetVerts(vtkSmartPointer<vtkCellArray>::New());

    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputData(m_polyData);
    m_actor->SetMapper(mapper);
    m_actor->GetProperty()->SetColor(0.20, 0.95, 0.72);
    m_actor->GetProperty()->SetLineWidth(2.5f);
    m_actor->GetProperty()->SetPointSize(11.0f);
    m_actor->GetProperty()->RenderPointsAsSpheresOn();
    m_actor->GetProperty()->LightingOff();
    m_actor->PickableOff();
    m_actor->SetVisibility(false);

    m_imagePicker->PickFromListOn();
    if (m_imageActor != nullptr) {
        m_imagePicker->AddPickList(m_imageActor);
    }
    m_imagePicker->SetTolerance(0.0005);
}

vtkActor *MprRulerController::actor() const
{
    return m_actor;
}

void MprRulerController::setEnabled(bool enabled)
{
    m_enabled = enabled;
    if (!m_enabled) {
        clear();
        m_actor->SetVisibility(false);
    }
}

bool MprRulerController::isEnabled() const
{
    return m_enabled;
}

bool MprRulerController::isMeasuring() const
{
    return m_activeMeasurementIndex.has_value() && m_activeSliceIndex >= 0 && m_hoverActive;
}

bool MprRulerController::isDraggingNode() const
{
    return m_draggedNode.has_value();
}

void MprRulerController::setVisible(bool visible)
{
    const bool shouldShow = visible && m_enabled;
    m_actor->SetVisibility(shouldShow);
    for (const auto &textActor : m_textActors) {
        const char *input = textActor->GetInput();
        textActor->SetVisibility(shouldShow && input != nullptr && input[0] != '\0');
    }
}

void MprRulerController::clear()
{
    m_measurements.clear();
    m_hoverWorldPosition = Axis { 0.0, 0.0, 0.0 };
    m_hoverActive = false;
    m_activeSliceIndex = -1;
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

void MprRulerController::finishMeasurement()
{
    m_hoverActive = false;
    m_activeSliceIndex = -1;
    m_activeMeasurementIndex.reset();
}

bool MprRulerController::beginNodeDrag(vtkRenderer *renderer,
                                       QVTKOpenGLNativeWidget *viewport,
                                       const QPoint &position,
                                       const SliceGeometry &sliceGeometry,
                                       int sliderValue)
{
    if (!m_enabled || isMeasuring()) {
        return false;
    }

    const auto node = findNearbyNode(renderer, viewport, sliceGeometry, sliderValue, position);
    if (!node.has_value()) {
        return false;
    }

    Axis worldPosition;
    if (!pickWorldPosition(renderer, viewport, position, sliceGeometry, sliderValue, &worldPosition)) {
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

bool MprRulerController::updateNodeDrag(vtkRenderer *renderer,
                                        QVTKOpenGLNativeWidget *viewport,
                                        const QPoint &position,
                                        const SliceGeometry &sliceGeometry,
                                        int sliderValue)
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
    if (measurement.sliceIndex != sliderValue || pointIndex >= measurement.pointsWorld.size()) {
        m_draggedNode.reset();
        return false;
    }

    Axis worldPosition;
    if (!pickWorldPosition(renderer, viewport, position, sliceGeometry, sliderValue, &worldPosition)) {
        return false;
    }

    measurement.pointsWorld[pointIndex] = worldPosition;
    return true;
}

bool MprRulerController::endNodeDrag()
{
    const bool hadActiveDrag = m_draggedNode.has_value();
    m_draggedNode.reset();
    return hadActiveDrag;
}

bool MprRulerController::recordPoint(vtkRenderer *renderer,
                                     QVTKOpenGLNativeWidget *viewport,
                                     const QPoint &position,
                                     const SliceGeometry &sliceGeometry,
                                     int sliderValue)
{
    if (!m_enabled) {
        return false;
    }

    Axis worldPosition;
    if (!pickWorldPosition(renderer, viewport, position, sliceGeometry, sliderValue, &worldPosition)) {
        return false;
    }

    Measurement *measurement = activeMeasurement();
    if (measurement == nullptr || m_activeSliceIndex != sliderValue) {
        m_measurements.push_back(Measurement { sliderValue, {} });
        m_activeMeasurementIndex = m_measurements.size() - 1;
        measurement = &m_measurements.back();
    }

    if (measurement == nullptr) {
        return false;
    }

    measurement->pointsWorld.push_back(worldPosition);
    m_activeSliceIndex = sliderValue;
    m_hoverWorldPosition = worldPosition;
    m_hoverActive = true;
    return true;
}

bool MprRulerController::updateHoverPoint(vtkRenderer *renderer,
                                          QVTKOpenGLNativeWidget *viewport,
                                          const QPoint &position,
                                          const SliceGeometry &sliceGeometry,
                          int sliderValue)
{
    const Measurement *measurement = activeMeasurement();
    if (!m_enabled
        || measurement == nullptr
        || measurement->pointsWorld.empty()
        || m_activeSliceIndex != sliderValue) {
        return false;
    }

    Axis worldPosition;
    if (!pickWorldPosition(renderer, viewport, position, sliceGeometry, sliderValue, &worldPosition)) {
        return false;
    }

    m_hoverWorldPosition = worldPosition;
    m_hoverActive = true;
    return true;
}

void MprRulerController::updateGeometry(vtkRenderer *renderer, const SliceGeometry &sliceGeometry, int sliderValue)
{
    auto lineCells = vtkSmartPointer<vtkCellArray>::New();
    auto nodeCells = vtkSmartPointer<vtkCellArray>::New();
    m_points->Reset();

    if (!m_enabled || sliceGeometry.sliceCount <= 0) {
        m_polyData->SetLines(lineCells);
        m_polyData->SetVerts(nodeCells);
        m_points->Modified();
        m_polyData->Modified();
        m_actor->SetVisibility(false);
        for (const auto &textActor : m_textActors) {
            textActor->SetInput("");
            textActor->SetVisibility(false);
        }
        return;
    }

    const Axis sliceOrigin = MprSliceMath::sliceOriginForSliderValue(sliceGeometry, sliderValue);
    const auto appendPoint = [this, &sliceOrigin, &sliceGeometry](const Axis &worldPosition) {
        const double xPosition = MprSliceMath::dotProduct(MprSliceMath::subtractAxes(worldPosition, sliceOrigin),
                                                          sliceGeometry.xAxis);
        const double yPosition = MprSliceMath::dotProduct(MprSliceMath::subtractAxes(worldPosition, sliceOrigin),
                                                          sliceGeometry.yAxis);
        return m_points->InsertNextPoint(xPosition, yPosition, kOverlayDepth);
    };

    bool hasVisibleMeasurement = false;
    for (std::size_t measurementIndex = 0; measurementIndex < m_measurements.size(); ++measurementIndex) {
        const Measurement &measurement = m_measurements[measurementIndex];
        if (measurement.sliceIndex != sliderValue || measurement.pointsWorld.empty()) {
            continue;
        }

        hasVisibleMeasurement = true;
        vtkIdType previousPointId = appendPoint(measurement.pointsWorld.front());
        nodeCells->InsertNextCell(1);
        nodeCells->InsertCellPoint(previousPointId);
        for (std::size_t index = 1; index < measurement.pointsWorld.size(); ++index) {
            const vtkIdType currentPointId = appendPoint(measurement.pointsWorld[index]);
            nodeCells->InsertNextCell(1);
            nodeCells->InsertCellPoint(currentPointId);
            lineCells->InsertNextCell(2);
            lineCells->InsertCellPoint(previousPointId);
            lineCells->InsertCellPoint(currentPointId);
            previousPointId = currentPointId;
        }

        if (m_hoverActive
            && m_activeMeasurementIndex.has_value()
            && measurementIndex == *m_activeMeasurementIndex
            && m_activeSliceIndex == sliderValue) {
            const vtkIdType hoverPointId = appendPoint(m_hoverWorldPosition);
            lineCells->InsertNextCell(2);
            lineCells->InsertCellPoint(previousPointId);
            lineCells->InsertCellPoint(hoverPointId);
        }
    }

    if (!hasVisibleMeasurement) {
        m_polyData->SetLines(lineCells);
        m_polyData->SetVerts(nodeCells);
        m_points->Modified();
        m_polyData->Modified();
        m_actor->SetVisibility(false);
        for (const auto &textActor : m_textActors) {
            textActor->SetInput("");
            textActor->SetVisibility(false);
        }
        return;
    }

    m_polyData->SetLines(lineCells);
    m_polyData->SetVerts(nodeCells);
    m_points->Modified();
    m_polyData->Modified();
    m_actor->SetVisibility(true);
    updateTextOverlays(renderer, sliceGeometry, sliderValue);
}

QString MprRulerController::measurementSummary(int sliceIndex) const
{
    const Measurement *measurement = activeMeasurement();
    if (measurement == nullptr || measurement->sliceIndex != sliceIndex) {
        measurement = latestMeasurementForSlice(sliceIndex);
    }

    if (measurement == nullptr || measurement->pointsWorld.empty()) {
        return QStringLiteral("皮尺");
    }

    return QStringLiteral("%1 mm")
        .arg(totalLength(*measurement, m_hoverActive && m_activeSliceIndex == sliceIndex), 0, 'f', 2);
}

bool MprRulerController::pickWorldPosition(vtkRenderer *renderer,
                                           QVTKOpenGLNativeWidget *viewport,
                                           const QPoint &widgetPosition,
                                           const SliceGeometry &sliceGeometry,
                                           int sliderValue,
                                           Axis *worldPosition) const
{
    if (renderer == nullptr
        || viewport == nullptr
        || worldPosition == nullptr
        || m_imageActor == nullptr
        || !m_imageActor->GetVisibility()) {
        return false;
    }

    const double devicePixelRatio = viewport->devicePixelRatioF();
    const int displayX = static_cast<int>(std::lround(widgetPosition.x() * devicePixelRatio));
    const int displayY = static_cast<int>(std::lround((viewport->height() - 1 - widgetPosition.y()) * devicePixelRatio));
    if (m_imagePicker->Pick(displayX, displayY, 0.0, renderer) == 0) {
        return false;
    }

    double pickedPosition[3] { 0.0, 0.0, 0.0 };
    m_imagePicker->GetPickPosition(pickedPosition);
    const Axis sliceOrigin = MprSliceMath::sliceOriginForSliderValue(sliceGeometry, sliderValue);
    *worldPosition = MprSliceMath::addAxes(MprSliceMath::addAxes(sliceOrigin,
                                                                 MprSliceMath::scaleAxis(sliceGeometry.xAxis, pickedPosition[0])),
                                           MprSliceMath::scaleAxis(sliceGeometry.yAxis, pickedPosition[1]));
    return true;
}

MprRulerController::Measurement *MprRulerController::activeMeasurement()
{
    if (!m_activeMeasurementIndex.has_value() || *m_activeMeasurementIndex >= m_measurements.size()) {
        return nullptr;
    }

    return &m_measurements[*m_activeMeasurementIndex];
}

const MprRulerController::Measurement *MprRulerController::activeMeasurement() const
{
    if (!m_activeMeasurementIndex.has_value() || *m_activeMeasurementIndex >= m_measurements.size()) {
        return nullptr;
    }

    return &m_measurements[*m_activeMeasurementIndex];
}

MprRulerController::Measurement *MprRulerController::latestMeasurementForSlice(int sliceIndex)
{
    for (auto it = m_measurements.rbegin(); it != m_measurements.rend(); ++it) {
        if (it->sliceIndex == sliceIndex) {
            return &(*it);
        }
    }

    return nullptr;
}

const MprRulerController::Measurement *MprRulerController::latestMeasurementForSlice(int sliceIndex) const
{
    for (auto it = m_measurements.rbegin(); it != m_measurements.rend(); ++it) {
        if (it->sliceIndex == sliceIndex) {
            return &(*it);
        }
    }

    return nullptr;
}

std::optional<std::pair<std::size_t, std::size_t>> MprRulerController::findNearbyNode(vtkRenderer *renderer,
                                                                                       QVTKOpenGLNativeWidget *viewport,
                                                                                       const SliceGeometry &sliceGeometry,
                                                                                       int sliderValue,
                                                                                       const QPoint &position) const
{
    if (renderer == nullptr || viewport == nullptr) {
        return std::nullopt;
    }

    const Axis sliceOrigin = MprSliceMath::sliceOriginForSliderValue(sliceGeometry, sliderValue);
    const double devicePixelRatio = viewport->devicePixelRatioF();
    const double targetDisplayX = position.x() * devicePixelRatio;
    const double targetDisplayY = (viewport->height() - 1 - position.y()) * devicePixelRatio;
    const double toleranceSquared = kNodePixelTolerance * kNodePixelTolerance;

    double bestDistanceSquared = std::numeric_limits<double>::max();
    std::optional<std::pair<std::size_t, std::size_t>> bestNode;
    for (std::size_t measurementIndex = 0; measurementIndex < m_measurements.size(); ++measurementIndex) {
        const Measurement &measurement = m_measurements[measurementIndex];
        if (measurement.sliceIndex != sliderValue || measurement.pointsWorld.empty()) {
            continue;
        }

        for (std::size_t pointIndex = 0; pointIndex < measurement.pointsWorld.size(); ++pointIndex) {
            const Axis &worldPoint = measurement.pointsWorld[pointIndex];
            const double xPosition = MprSliceMath::dotProduct(MprSliceMath::subtractAxes(worldPoint, sliceOrigin),
                                                              sliceGeometry.xAxis);
            const double yPosition = MprSliceMath::dotProduct(MprSliceMath::subtractAxes(worldPoint, sliceOrigin),
                                                              sliceGeometry.yAxis);

            vtkNew<vtkCoordinate> coordinate;
            coordinate->SetCoordinateSystemToWorld();
            coordinate->SetValue(xPosition, yPosition, kOverlayDepth);
            const double *displayValue = coordinate->GetComputedDoubleDisplayValue(renderer);
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

void MprRulerController::ensureTextActors(vtkRenderer *renderer, std::size_t count)
{
    if (renderer == nullptr) {
        return;
    }

    while (m_textActors.size() < count) {
        auto textActor = createTextActor();
        renderer->AddActor2D(textActor);
        m_textActors.push_back(textActor);
    }
}

double MprRulerController::totalLength(const Measurement &measurement, bool includeHover) const
{
    if (measurement.pointsWorld.empty()) {
        return 0.0;
    }

    double length = 0.0;
    for (std::size_t index = 1; index < measurement.pointsWorld.size(); ++index) {
        length += segmentLength(measurement.pointsWorld[index - 1], measurement.pointsWorld[index]);
    }

    const Measurement *active = activeMeasurement();
    if (includeHover
        && m_hoverActive
        && active != nullptr
        && active == &measurement
        && measurement.sliceIndex == m_activeSliceIndex) {
        length += segmentLength(measurement.pointsWorld.back(), m_hoverWorldPosition);
    }

    return length;
}

double MprRulerController::segmentLength(const Axis &lhs, const Axis &rhs)
{
    const double dx = rhs[0] - lhs[0];
    const double dy = rhs[1] - lhs[1];
    const double dz = rhs[2] - lhs[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

void MprRulerController::updateTextOverlays(vtkRenderer *renderer, const SliceGeometry &sliceGeometry, int sliderValue)
{
    if (renderer == nullptr) {
        for (const auto &textActor : m_textActors) {
            textActor->SetInput("");
            textActor->SetVisibility(false);
        }
        return;
    }

    std::vector<const Measurement *> visibleMeasurements;
    visibleMeasurements.reserve(m_measurements.size());
    for (const Measurement &measurement : m_measurements) {
        if (measurement.sliceIndex == sliderValue && !measurement.pointsWorld.empty()) {
            visibleMeasurements.push_back(&measurement);
        }
    }

    ensureTextActors(renderer, visibleMeasurements.size());
    if (visibleMeasurements.empty()) {
        for (const auto &textActor : m_textActors) {
            textActor->SetInput("");
            textActor->SetVisibility(false);
        }
        return;
    }

    const Measurement *active = activeMeasurement();
    const Axis sliceOrigin = MprSliceMath::sliceOriginForSliderValue(sliceGeometry, sliderValue);
    struct TextPlacement
    {
        const Measurement *measurement = nullptr;
        int anchorDisplayX = 0;
        int anchorDisplayY = 0;
        bool includeHover = false;
    };
    std::vector<TextPlacement> placements;
    placements.reserve(visibleMeasurements.size());
    for (const Measurement *measurement : visibleMeasurements) {
        Axis anchorWorld = measurement->pointsWorld.back();
        const bool includeHover = m_hoverActive
            && active != nullptr
            && active == measurement
            && m_activeSliceIndex == sliderValue;
        if (includeHover) {
            anchorWorld = m_hoverWorldPosition;
        }

        const double xPosition = MprSliceMath::dotProduct(MprSliceMath::subtractAxes(anchorWorld, sliceOrigin),
                                                          sliceGeometry.xAxis);
        const double yPosition = MprSliceMath::dotProduct(MprSliceMath::subtractAxes(anchorWorld, sliceOrigin),
                                                          sliceGeometry.yAxis);

        vtkNew<vtkCoordinate> coordinate;
        coordinate->SetCoordinateSystemToWorld();
        coordinate->SetValue(xPosition, yPosition, kOverlayDepth);
        const int *displayValue = coordinate->GetComputedDisplayValue(renderer);
        placements.push_back(TextPlacement { measurement,
                                             displayValue[0],
                                             displayValue[1],
                                             includeHover });
    }

    std::sort(placements.begin(),
              placements.end(),
              [](const TextPlacement &lhs, const TextPlacement &rhs) {
                  if (lhs.anchorDisplayY != rhs.anchorDisplayY) {
                      return lhs.anchorDisplayY > rhs.anchorDisplayY;
                  }
                  return lhs.anchorDisplayX < rhs.anchorDisplayX;
              });

    const int *rendererSize = renderer->GetSize();
    const int viewportWidth = rendererSize != nullptr ? rendererSize[0] : 0;
    const int viewportHeight = rendererSize != nullptr ? rendererSize[1] : 0;

    std::vector<DisplayRect> occupiedRects;
    occupiedRects.reserve(placements.size());

    std::size_t textActorIndex = 0;
    for (const TextPlacement &placement : placements) {
        auto &textActor = m_textActors[textActorIndex++];
        const QByteArray labelText = QStringLiteral("%1 mm")
                                         .arg(totalLength(*placement.measurement, placement.includeHover), 0, 'f', 2)
                                         .toUtf8();
        textActor->SetInput(labelText.constData());
        textActor->GetTextProperty()->SetJustificationToLeft();
        textActor->GetTextProperty()->SetVerticalJustificationToBottom();

        double textSize[2] { 0.0, 0.0 };
        textActor->GetSize(renderer, textSize);

        const int labelWidth = std::max(1, static_cast<int>(std::ceil(textSize[0]))) + kTextPadding * 2;
        const int labelHeight = std::max(1, static_cast<int>(std::ceil(textSize[1]))) + kTextPadding * 2;

        int displayX = placement.anchorDisplayX + kTextOffsetX;
        int displayY = placement.anchorDisplayY + kTextOffsetY;
        DisplayRect rect { displayX,
                           displayY,
                           displayX + labelWidth,
                           displayY + labelHeight };

        bool moved = true;
        while (moved) {
            moved = false;
            for (const DisplayRect &occupied : occupiedRects) {
                if (!rectsOverlap(rect, occupied)) {
                    continue;
                }

                displayY = occupied.top + kTextSpacing;
                rect = DisplayRect { displayX, displayY, displayX + labelWidth, displayY + labelHeight };
                moved = true;
            }
        }

        if (viewportWidth > 0) {
            displayX = std::clamp(displayX, kTextPadding, std::max(kTextPadding, viewportWidth - labelWidth - kTextPadding));
        }
        if (viewportHeight > 0) {
            displayY = std::clamp(displayY, kTextPadding, std::max(kTextPadding, viewportHeight - labelHeight - kTextPadding));
        }
        rect = DisplayRect { displayX, displayY, displayX + labelWidth, displayY + labelHeight };
        occupiedRects.push_back(rect);

        textActor->SetDisplayPosition(displayX, displayY);
        textActor->SetVisibility(m_enabled);
    }

    for (; textActorIndex < m_textActors.size(); ++textActorIndex) {
        m_textActors[textActorIndex]->SetInput("");
        m_textActors[textActorIndex]->SetVisibility(false);
    }
}
