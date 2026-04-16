#include "mprrulercontroller.h"

#include <QPoint>

#include <QVTKOpenGLNativeWidget.h>

#include <vtkActor.h>
#include <vtkCellArray.h>
#include <vtkCellPicker.h>
#include <vtkCoordinate.h>
#include <vtkImageActor.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderer.h>
#include <vtkTextActor.h>
#include <vtkTextProperty.h>

#include <cmath>

namespace
{
constexpr double kOverlayDepth = 0.15;
constexpr int kTextOffsetX = 12;
constexpr int kTextOffsetY = 12;
}

MprRulerController::MprRulerController(vtkImageActor *imageActor)
    : m_imageActor(imageActor)
    , m_actor(vtkSmartPointer<vtkActor>::New())
    , m_textActor(vtkSmartPointer<vtkTextActor>::New())
    , m_polyData(vtkSmartPointer<vtkPolyData>::New())
    , m_points(vtkSmartPointer<vtkPoints>::New())
    , m_imagePicker(vtkSmartPointer<vtkCellPicker>::New())
{
    m_polyData->SetPoints(m_points);
    m_polyData->SetLines(vtkSmartPointer<vtkCellArray>::New());

    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputData(m_polyData);
    m_actor->SetMapper(mapper);
    m_actor->GetProperty()->SetColor(0.20, 0.95, 0.72);
    m_actor->GetProperty()->SetLineWidth(2.5f);
    m_actor->GetProperty()->LightingOff();
    m_actor->PickableOff();
    m_actor->SetVisibility(false);

    m_textActor->SetInput("");
    m_textActor->GetTextProperty()->SetFontSize(16);
    m_textActor->GetTextProperty()->SetBold(true);
    m_textActor->GetTextProperty()->SetColor(0.20, 0.95, 0.72);
    m_textActor->GetTextProperty()->SetBackgroundColor(0.05, 0.05, 0.06);
    m_textActor->GetTextProperty()->SetBackgroundOpacity(0.55);
    m_textActor->GetTextProperty()->SetFrame(true);
    m_textActor->GetTextProperty()->SetFrameColor(0.20, 0.95, 0.72);
    m_textActor->SetVisibility(false);

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

vtkTextActor *MprRulerController::textActor() const
{
    return m_textActor;
}

void MprRulerController::setEnabled(bool enabled)
{
    m_enabled = enabled;
    if (!m_enabled) {
        clear();
        m_actor->SetVisibility(false);
        m_textActor->SetVisibility(false);
    }
}

bool MprRulerController::isEnabled() const
{
    return m_enabled;
}

bool MprRulerController::isMeasuring() const
{
    return m_activeSliceIndex >= 0 && m_hoverActive;
}

void MprRulerController::setVisible(bool visible)
{
    const bool shouldShow = visible && m_enabled;
    m_actor->SetVisibility(shouldShow);
    m_textActor->SetVisibility(shouldShow);
}

void MprRulerController::clear()
{
    m_measurements.clear();
    m_hoverWorldPosition = Axis { 0.0, 0.0, 0.0 };
    m_hoverActive = false;
    m_activeSliceIndex = -1;
    m_points->Reset();
    m_polyData->SetLines(vtkSmartPointer<vtkCellArray>::New());
    m_points->Modified();
    m_polyData->Modified();
    m_textActor->SetInput("");
    m_textActor->SetVisibility(false);
}

void MprRulerController::finishMeasurement()
{
    m_hoverActive = false;
    m_activeSliceIndex = -1;
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

    Measurement *measurement = measurementForSlice(sliderValue);
    if (measurement == nullptr) {
        m_measurements.push_back(Measurement { sliderValue, {} });
        measurement = &m_measurements.back();
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
    const Measurement *measurement = measurementForSlice(sliderValue);
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

void MprRulerController::updateGeometry(const SliceGeometry &sliceGeometry, int sliderValue)
{
    auto lineCells = vtkSmartPointer<vtkCellArray>::New();
    m_points->Reset();

    const Measurement *measurement = measurementForSlice(sliderValue);
    if (!m_enabled
        || sliceGeometry.sliceCount <= 0
        || measurement == nullptr
        || measurement->pointsWorld.empty()) {
        m_polyData->SetLines(lineCells);
        m_points->Modified();
        m_polyData->Modified();
        m_actor->SetVisibility(false);
        m_textActor->SetInput("");
        m_textActor->SetVisibility(false);
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

    vtkIdType previousPointId = appendPoint(measurement->pointsWorld.front());
    for (std::size_t index = 1; index < measurement->pointsWorld.size(); ++index) {
        const vtkIdType currentPointId = appendPoint(measurement->pointsWorld[index]);
        lineCells->InsertNextCell(2);
        lineCells->InsertCellPoint(previousPointId);
        lineCells->InsertCellPoint(currentPointId);
        previousPointId = currentPointId;
    }

    if (m_hoverActive && m_activeSliceIndex == sliderValue) {
        const vtkIdType hoverPointId = appendPoint(m_hoverWorldPosition);
        lineCells->InsertNextCell(2);
        lineCells->InsertCellPoint(previousPointId);
        lineCells->InsertCellPoint(hoverPointId);
    }

    m_polyData->SetLines(lineCells);
    m_points->Modified();
    m_polyData->Modified();
    m_actor->SetVisibility(true);
    updateTextOverlay(sliceGeometry, sliderValue);
}

QString MprRulerController::measurementSummary(int sliceIndex) const
{
    const Measurement *measurement = measurementForSlice(sliceIndex);
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

MprRulerController::Measurement *MprRulerController::measurementForSlice(int sliceIndex)
{
    for (Measurement &measurement : m_measurements) {
        if (measurement.sliceIndex == sliceIndex) {
            return &measurement;
        }
    }

    return nullptr;
}

const MprRulerController::Measurement *MprRulerController::measurementForSlice(int sliceIndex) const
{
    for (const Measurement &measurement : m_measurements) {
        if (measurement.sliceIndex == sliceIndex) {
            return &measurement;
        }
    }

    return nullptr;
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

    if (includeHover && m_hoverActive && measurement.sliceIndex == m_activeSliceIndex) {
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

void MprRulerController::updateTextOverlay(const SliceGeometry &sliceGeometry, int sliderValue)
{
    const Measurement *measurement = measurementForSlice(sliderValue);
    if (measurement == nullptr || measurement->pointsWorld.empty()) {
        m_textActor->SetInput("");
        m_textActor->SetVisibility(false);
        return;
    }

    Axis anchorWorld = measurement->pointsWorld.back();
    if (m_hoverActive && m_activeSliceIndex == sliderValue) {
        anchorWorld = m_hoverWorldPosition;
    }

    const Axis sliceOrigin = MprSliceMath::sliceOriginForSliderValue(sliceGeometry, sliderValue);
    const double xPosition = MprSliceMath::dotProduct(MprSliceMath::subtractAxes(anchorWorld, sliceOrigin),
                                                      sliceGeometry.xAxis);
    const double yPosition = MprSliceMath::dotProduct(MprSliceMath::subtractAxes(anchorWorld, sliceOrigin),
                                                      sliceGeometry.yAxis);

    auto *coordinate = m_textActor->GetPositionCoordinate();
    coordinate->SetCoordinateSystemToWorld();
    coordinate->SetValue(xPosition, yPosition, kOverlayDepth);
    m_textActor->SetPosition(kTextOffsetX, kTextOffsetY);
    m_textActor->SetInput(measurementSummary(sliderValue).toUtf8().constData());
    m_textActor->SetVisibility(m_enabled);
}
