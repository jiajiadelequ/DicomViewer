#include "mprcrosshaircontroller.h"

#include <QPoint>

#include <QVTKOpenGLNativeWidget.h>

#include <vtkActor.h>
#include <vtkCellArray.h>
#include <vtkCellPicker.h>
#include <vtkImageActor.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderer.h>

#include <algorithm>
#include <cmath>

MprCrosshairController::MprCrosshairController(vtkImageActor *imageActor)
    : m_imageActor(imageActor)
    , m_actor(vtkSmartPointer<vtkActor>::New())
    , m_polyData(vtkSmartPointer<vtkPolyData>::New())
    , m_points(vtkSmartPointer<vtkPoints>::New())
    , m_imagePicker(vtkSmartPointer<vtkCellPicker>::New())
{
    m_points->SetNumberOfPoints(4);
    auto crosshairLines = vtkSmartPointer<vtkCellArray>::New();
    crosshairLines->InsertNextCell(2);
    crosshairLines->InsertCellPoint(0);
    crosshairLines->InsertCellPoint(1);
    crosshairLines->InsertNextCell(2);
    crosshairLines->InsertCellPoint(2);
    crosshairLines->InsertCellPoint(3);
    m_polyData->SetPoints(m_points);
    m_polyData->SetLines(crosshairLines);

    auto crosshairMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    crosshairMapper->SetInputData(m_polyData);
    m_actor->SetMapper(crosshairMapper);
    m_actor->GetProperty()->SetColor(0.95, 0.82, 0.15);
    m_actor->GetProperty()->SetLineWidth(2.0f);
    m_actor->GetProperty()->LightingOff();
    m_actor->PickableOff();
    m_actor->SetVisibility(false);

    m_imagePicker->PickFromListOn();
    if (m_imageActor != nullptr) {
        m_imagePicker->AddPickList(m_imageActor);
    }
    m_imagePicker->SetTolerance(0.0005);
}

vtkActor *MprCrosshairController::actor() const
{
    return m_actor;
}

void MprCrosshairController::setEnabled(bool enabled)
{
    m_enabled = enabled;
    m_dragActive = false;
    if (!m_enabled) {
        m_actor->SetVisibility(false);
    }
}

bool MprCrosshairController::isEnabled() const
{
    return m_enabled;
}

void MprCrosshairController::setVisible(bool visible)
{
    m_actor->SetVisibility(visible && m_enabled);
}

bool MprCrosshairController::beginInteraction(vtkRenderer *renderer,
                                              QVTKOpenGLNativeWidget *viewport,
                                              const QPoint &position,
                                              const SliceGeometry &sliceGeometry,
                                              int sliderValue,
                                              Axis *worldPosition)
{
    if (!m_enabled) {
        return false;
    }

    if (!pickWorldPosition(renderer, viewport, position, sliceGeometry, sliderValue, worldPosition)) {
        return false;
    }

    m_dragActive = true;
    return true;
}

bool MprCrosshairController::updateInteraction(vtkRenderer *renderer,
                                               QVTKOpenGLNativeWidget *viewport,
                                               bool leftButtonDown,
                                               const QPoint &position,
                                               const SliceGeometry &sliceGeometry,
                                               int sliderValue,
                                               Axis *worldPosition)
{
    if (!m_dragActive) {
        return false;
    }

    if (!leftButtonDown) {
        m_dragActive = false;
        return false;
    }

    return pickWorldPosition(renderer, viewport, position, sliceGeometry, sliderValue, worldPosition);
}

bool MprCrosshairController::endInteraction(bool leftButtonReleased)
{
    if (!m_dragActive || !leftButtonReleased) {
        return false;
    }

    m_dragActive = false;
    return true;
}

void MprCrosshairController::updateGeometry(const Axis &cursorWorldPosition,
                                            const SliceGeometry &sliceGeometry,
                                            int sliderValue)
{
    if (!m_actor->GetVisibility() || sliceGeometry.sliceCount <= 0) {
        return;
    }

    const Axis sliceOrigin = MprSliceMath::sliceOriginForSliderValue(sliceGeometry, sliderValue);
    const double xMinimum = sliceGeometry.outputOrigin[0];
    const double xMaximum = MprSliceMath::maxOutputCoordinate(sliceGeometry.outputOrigin[0],
                                                              sliceGeometry.xSpacing,
                                                              sliceGeometry.outputExtent[0],
                                                              sliceGeometry.outputExtent[1]);
    const double yMinimum = sliceGeometry.outputOrigin[1];
    const double yMaximum = MprSliceMath::maxOutputCoordinate(sliceGeometry.outputOrigin[1],
                                                              sliceGeometry.ySpacing,
                                                              sliceGeometry.outputExtent[2],
                                                              sliceGeometry.outputExtent[3]);
    const double xPosition = std::clamp(MprSliceMath::dotProduct(MprSliceMath::subtractAxes(cursorWorldPosition, sliceOrigin),
                                                                 sliceGeometry.xAxis),
                                        xMinimum,
                                        xMaximum);
    const double yPosition = std::clamp(MprSliceMath::dotProduct(MprSliceMath::subtractAxes(cursorWorldPosition, sliceOrigin),
                                                                 sliceGeometry.yAxis),
                                        yMinimum,
                                        yMaximum);
    constexpr double overlayDepth = 0.1;

    m_points->SetPoint(0, xPosition, yMinimum, overlayDepth);
    m_points->SetPoint(1, xPosition, yMaximum, overlayDepth);
    m_points->SetPoint(2, xMinimum, yPosition, overlayDepth);
    m_points->SetPoint(3, xMaximum, yPosition, overlayDepth);
    m_points->Modified();
    m_polyData->Modified();
}

bool MprCrosshairController::pickWorldPosition(vtkRenderer *renderer,
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
