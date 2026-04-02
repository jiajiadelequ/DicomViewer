#include "modelviewcrosshaircontroller.h"

#include <QPoint>

#include <QVTKOpenGLNativeWidget.h>

#include <vtkActor.h>
#include <vtkCellPicker.h>
#include <vtkCursor3D.h>
#include <vtkImageData.h>
#include <vtkMath.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderer.h>

#include <algorithm>
#include <cmath>

namespace
{
bool mergeBounds(const double source[6], double target[6], bool hasTarget)
{
    if (!vtkMath::AreBoundsInitialized(source)) {
        return hasTarget;
    }

    if (!hasTarget) {
        std::copy(source, source + 6, target);
        return true;
    }

    target[0] = std::min(target[0], source[0]);
    target[1] = std::max(target[1], source[1]);
    target[2] = std::min(target[2], source[2]);
    target[3] = std::max(target[3], source[3]);
    target[4] = std::min(target[4], source[4]);
    target[5] = std::max(target[5], source[5]);
    return true;
}
}

ModelViewCrosshairController::ModelViewCrosshairController(vtkRenderer *renderer)
    : m_renderer(renderer)
    , m_crosshairActor(vtkSmartPointer<vtkActor>::New())
    , m_crosshairCursor(vtkSmartPointer<vtkCursor3D>::New())
    , m_modelPicker(vtkSmartPointer<vtkCellPicker>::New())
{
    m_crosshairCursor->AllOff();
    m_crosshairCursor->AxesOn();
    m_crosshairCursor->XShadowsOn();
    m_crosshairCursor->YShadowsOn();
    m_crosshairCursor->ZShadowsOn();
    m_crosshairCursor->WrapOff();
    m_crosshairCursor->TranslationModeOff();

    auto crosshairMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    crosshairMapper->SetInputConnection(m_crosshairCursor->GetOutputPort());
    m_crosshairActor->SetMapper(crosshairMapper);
    m_crosshairActor->GetProperty()->SetColor(0.95, 0.82, 0.15);
    m_crosshairActor->GetProperty()->SetLineWidth(2.5f);
    m_crosshairActor->GetProperty()->RenderLinesAsTubesOn();
    m_crosshairActor->GetProperty()->LightingOff();
    m_crosshairActor->PickableOff();
    m_crosshairActor->SetVisibility(false);

    m_modelPicker->PickFromListOn();
    m_modelPicker->SetTolerance(0.0005);

    if (m_renderer != nullptr) {
        m_renderer->AddActor(m_crosshairActor);
    }
}

void ModelViewCrosshairController::clearScene()
{
    m_dragActive = false;
    m_cursorWorldPosition = Axis { 0.0, 0.0, 0.0 };
    m_modelActors.clear();
    m_modelPicker->InitializePickList();
    m_hasReferenceBounds = false;
    m_hasModelBounds = false;
    m_referenceBounds = { 0.0, -1.0, 0.0, -1.0, 0.0, -1.0 };
    m_modelBounds = { 0.0, -1.0, 0.0, -1.0, 0.0, -1.0 };

    if (m_renderer != nullptr) {
        m_renderer->RemoveAllViewProps();
        m_renderer->AddActor(m_crosshairActor);
    }
    m_crosshairActor->SetVisibility(false);
}

void ModelViewCrosshairController::addModelActor(vtkSmartPointer<vtkActor> actor, vtkPolyData *polyData)
{
    if (actor == nullptr || polyData == nullptr) {
        return;
    }

    double polyBounds[6] { 0.0, -1.0, 0.0, -1.0, 0.0, -1.0 };
    polyData->GetBounds(polyBounds);
    m_hasModelBounds = mergeBounds(polyBounds, m_modelBounds.data(), m_hasModelBounds);

    m_modelActors.push_back(actor);
    if (m_renderer != nullptr) {
        m_renderer->AddActor(actor);
    }
    m_modelPicker->AddPickList(actor);
}

void ModelViewCrosshairController::setReferenceImageData(vtkImageData *imageData)
{
    m_hasReferenceBounds = false;
    m_referenceBounds = { 0.0, -1.0, 0.0, -1.0, 0.0, -1.0 };

    if (imageData != nullptr) {
        double bounds[6] { 0.0, -1.0, 0.0, -1.0, 0.0, -1.0 };
        imageData->GetBounds(bounds);
        if (vtkMath::AreBoundsInitialized(bounds)) {
            std::copy(bounds, bounds + 6, m_referenceBounds.begin());
            m_hasReferenceBounds = true;
        }
    }
}

void ModelViewCrosshairController::setEnabled(bool enabled)
{
    m_enabled = enabled;
    m_dragActive = false;
}

void ModelViewCrosshairController::setCursorWorldPosition(const Axis &worldPosition)
{
    m_cursorWorldPosition = worldPosition;
}

ModelViewCrosshairController::Axis ModelViewCrosshairController::cursorWorldPosition() const
{
    return m_cursorWorldPosition;
}

void ModelViewCrosshairController::updateGeometry()
{
    double bounds[6] { 0.0, -1.0, 0.0, -1.0, 0.0, -1.0 };
    if (!m_enabled || !crosshairBounds(bounds)) {
        m_crosshairActor->SetVisibility(false);
        return;
    }

    m_crosshairCursor->SetModelBounds(bounds);
    m_crosshairCursor->SetFocalPoint(m_cursorWorldPosition[0],
                                     m_cursorWorldPosition[1],
                                     m_cursorWorldPosition[2]);
    m_crosshairCursor->Update();
    m_crosshairActor->SetVisibility(true);
}

bool ModelViewCrosshairController::cameraBounds(double bounds[6]) const
{
    if (m_hasModelBounds && mergeBounds(m_modelBounds.data(), bounds, false)) {
        return true;
    }

    if (m_hasReferenceBounds && mergeBounds(m_referenceBounds.data(), bounds, false)) {
        return true;
    }

    if (m_renderer == nullptr) {
        return false;
    }

    double visibleBounds[6] { 0.0, -1.0, 0.0, -1.0, 0.0, -1.0 };
    m_renderer->ComputeVisiblePropBounds(visibleBounds);
    return mergeBounds(visibleBounds, bounds, false);
}

bool ModelViewCrosshairController::beginInteraction(QVTKOpenGLNativeWidget *viewport,
                                                    const QPoint &position,
                                                    Axis *worldPosition)
{
    if (!m_enabled) {
        return false;
    }

    if (!pickWorldPosition(viewport, position, worldPosition)) {
        return false;
    }

    m_dragActive = true;
    return true;
}

bool ModelViewCrosshairController::updateInteraction(QVTKOpenGLNativeWidget *viewport,
                                                     bool leftButtonDown,
                                                     const QPoint &position,
                                                     Axis *worldPosition)
{
    if (!m_dragActive) {
        return false;
    }

    if (!leftButtonDown) {
        m_dragActive = false;
        return false;
    }

    return pickWorldPosition(viewport, position, worldPosition);
}

bool ModelViewCrosshairController::endInteraction(bool leftButtonReleased)
{
    if (!m_dragActive || !leftButtonReleased) {
        return false;
    }

    m_dragActive = false;
    return true;
}

bool ModelViewCrosshairController::pickWorldPosition(QVTKOpenGLNativeWidget *viewport,
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

bool ModelViewCrosshairController::crosshairBounds(double bounds[6]) const
{
    bool hasBounds = false;
    hasBounds = m_hasModelBounds && mergeBounds(m_modelBounds.data(), bounds, hasBounds);
    hasBounds = m_hasReferenceBounds && mergeBounds(m_referenceBounds.data(), bounds, hasBounds);

    if (hasBounds) {
        return true;
    }

    if (m_renderer == nullptr) {
        return false;
    }

    double visibleBounds[6] { 0.0, -1.0, 0.0, -1.0, 0.0, -1.0 };
    m_renderer->ComputeVisiblePropBounds(visibleBounds);
    return mergeBounds(visibleBounds, bounds, false);
}
