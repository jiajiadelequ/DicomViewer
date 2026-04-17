#include "modelclippingcontroller.h"

#include <QEvent>
#include <QMouseEvent>
#include <QPoint>

#include <QVTKOpenGLNativeWidget.h>

#include <vtkBoxRepresentation.h>
#include <vtkBoxWidget2.h>
#include <vtkCallbackCommand.h>
#include <vtkCellPicker.h>
#include <vtkCommand.h>
#include <vtkPlane.h>
#include <vtkPlanes.h>
#include <vtkProp.h>
#include <vtkPropCollection.h>
#include <vtkProperty.h>
#include <vtkRenderWindow.h>
#include <vtkRenderer.h>

#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace
{
std::array<double, 2> widgetPositionToDisplayPosition(QVTKOpenGLNativeWidget *widget, const QPoint &position)
{
    const double devicePixelRatio = widget != nullptr ? widget->devicePixelRatioF() : 1.0;
    const double displayX = std::lround(position.x() * devicePixelRatio);
    const double displayY = std::lround((widget->height() - 1 - position.y()) * devicePixelRatio);
    return { displayX, displayY };
}

int faceInteractionStateFromDisplayPosition(vtkBoxRepresentation *representation,
                                            vtkRenderer *renderer,
                                            double displayX,
                                            double displayY)
{
    if (representation == nullptr || renderer == nullptr) {
        return vtkBoxRepresentation::Outside;
    }

    auto picker = vtkSmartPointer<vtkCellPicker>::New();
    picker->SetTolerance(0.0005);
    picker->PickFromListOn();

    auto props = vtkSmartPointer<vtkPropCollection>::New();
    representation->GetActors(props);
    props->InitTraversal();
    while (auto *prop = props->GetNextProp()) {
        picker->AddPickList(prop);
    }

    if (picker->Pick(displayX, displayY, 0.0, renderer) == 0) {
        return vtkBoxRepresentation::Outside;
    }

    auto pickedPosition = picker->GetPickPosition();
    int closestFaceState = vtkBoxRepresentation::Outside;
    double closestDistance = std::numeric_limits<double>::max();
    for (int faceIndex = 0; faceIndex < 6; ++faceIndex) {
        vtkPlane *plane = representation->GetUnderlyingPlane(faceIndex);
        if (plane == nullptr) {
            continue;
        }
        const double distance = std::abs(plane->DistanceToPlane(pickedPosition));
        if (distance < closestDistance) {
            closestDistance = distance;
            closestFaceState = vtkBoxRepresentation::MoveF0 + faceIndex;
        }
    }

    return closestFaceState;
}
}

ModelClippingController::ModelClippingController(QVTKOpenGLNativeWidget *viewport, vtkRenderer *renderer)
    : m_viewport(viewport)
    , m_renderer(renderer)
    , m_widget(vtkSmartPointer<vtkBoxWidget2>::New())
    , m_callback(vtkSmartPointer<vtkCallbackCommand>::New())
{
    auto representation = vtkSmartPointer<vtkBoxRepresentation>::New();
    representation->SetPlaceFactor(1.0);
    representation->HandlesOff();
    representation->OutlineFaceWiresOff();
    representation->OutlineCursorWiresOff();
    representation->GetOutlineProperty()->SetColor(0.86, 0.25, 0.18);
    representation->GetOutlineProperty()->SetLineWidth(2.0f);
    representation->GetSelectedOutlineProperty()->SetColor(1.0, 0.54, 0.24);
    representation->GetSelectedOutlineProperty()->SetLineWidth(3.0f);
    representation->GetFaceProperty()->SetColor(0.86, 0.25, 0.18);
    representation->GetFaceProperty()->SetOpacity(0.04);
    representation->GetSelectedFaceProperty()->SetColor(1.0, 0.54, 0.24);
    representation->GetSelectedFaceProperty()->SetOpacity(0.18);

    m_widget->SetRepresentation(representation);
    m_widget->SetInteractor(m_viewport != nullptr ? m_viewport->interactor() : nullptr);
    m_widget->SetCurrentRenderer(m_renderer);
    m_widget->RotationEnabledOn();
    m_widget->ScalingEnabledOn();
    m_widget->MoveFacesEnabledOn();
    m_widget->SetPriority(1.0f);

    m_callback->SetClientData(this);
    m_callback->SetCallback(&ModelClippingController::handleWidgetInteraction);
    m_widget->AddObserver(vtkCommand::InteractionEvent, m_callback);
    m_widget->AddObserver(vtkCommand::EndInteractionEvent, m_callback);
}

void ModelClippingController::setStatusHandler(StatusHandler handler)
{
    m_statusHandler = std::move(handler);
}

void ModelClippingController::setCommitHandler(CommitHandler handler)
{
    m_commitHandler = std::move(handler);
}

void ModelClippingController::activate(const BoundsProvider &boundsProvider)
{
    m_enabled = true;
    resetInteractionState();

    if (!boundsProvider || m_widget == nullptr) {
        return;
    }

    double bounds[6] { 0.0, -1.0, 0.0, -1.0, 0.0, -1.0 };
    if (!boundsProvider(bounds)) {
        return;
    }

    auto *representation = vtkBoxRepresentation::SafeDownCast(m_widget->GetRepresentation());
    if (representation == nullptr) {
        return;
    }

    representation->PlaceWidget(bounds);
    m_widget->On();
}

void ModelClippingController::deactivate()
{
    m_enabled = false;
    resetInteractionState();
    if (m_widget != nullptr) {
        m_widget->Off();
    }
}

bool ModelClippingController::isEnabled() const
{
    return m_enabled;
}

bool ModelClippingController::handleViewportEvent(QEvent *event)
{
    if (!m_enabled || event == nullptr) {
        return false;
    }

    auto *representation = vtkBoxRepresentation::SafeDownCast(m_widget->GetRepresentation());
    if (representation == nullptr || m_viewport == nullptr) {
        return false;
    }

    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() != Qt::LeftButton) {
            return false;
        }

        const int interactionState = resolvedInteractionState(mouseEvent->pos());
        if (interactionState < vtkBoxRepresentation::MoveF0
            || interactionState > vtkBoxRepresentation::MoveF5) {
            return false;
        }

        const auto displayPosition = widgetPositionToDisplayPosition(m_viewport, mouseEvent->pos());
        double eventPosition[2] { displayPosition[0], displayPosition[1] };
        m_manualFaceDragActive = true;
        m_manualFaceInteractionState = interactionState;
        m_hoveredInteractionState = interactionState;
        m_previewDirty = true;
        representation->SetInteractionState(interactionState);
        representation->StartWidgetInteraction(eventPosition);
        updateStatus(QStringLiteral("正在拖拽裁剪框的面，松手后更新裁剪结果"));
        m_viewport->renderWindow()->Render();
        return true;
    }
    case QEvent::MouseMove: {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (m_manualFaceDragActive) {
            const auto displayPosition = widgetPositionToDisplayPosition(m_viewport, mouseEvent->pos());
            double eventPosition[2] { displayPosition[0], displayPosition[1] };
            representation->SetInteractionState(m_manualFaceInteractionState);
            representation->WidgetInteraction(eventPosition);
            m_viewport->renderWindow()->Render();
            return true;
        }

        refreshHoverState(mouseEvent->pos());
        return false;
    }
    case QEvent::MouseButtonRelease: {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (!m_manualFaceDragActive || mouseEvent->button() != Qt::LeftButton) {
            return false;
        }

        representation->SetInteractionState(vtkBoxRepresentation::Outside);
        commitPreview();
        resetInteractionState();
        updateStatus(QStringLiteral("模型裁剪已更新，可继续拖拽面伸缩裁剪框，空白处左键可旋转视角"));
        m_viewport->renderWindow()->Render();
        return true;
    }
    case QEvent::Leave:
        if (!m_manualFaceDragActive && m_hoveredInteractionState != vtkBoxRepresentation::Outside) {
            m_hoveredInteractionState = vtkBoxRepresentation::Outside;
            representation->SetInteractionState(vtkBoxRepresentation::Outside);
            m_viewport->renderWindow()->Render();
        }
        return false;
    default:
        return false;
    }
}

bool ModelClippingController::copyPlanesTo(vtkPlanes *planes) const
{
    if (!m_enabled || planes == nullptr || m_widget == nullptr) {
        return false;
    }

    auto *representation = vtkBoxRepresentation::SafeDownCast(m_widget->GetRepresentation());
    if (representation == nullptr) {
        return false;
    }

    representation->GetPlanes(planes);
    return true;
}

void ModelClippingController::handleWidgetInteraction(vtkObject *caller,
                                                      unsigned long eventId,
                                                      void *clientData,
                                                      void *callData)
{
    Q_UNUSED(caller);
    Q_UNUSED(callData);

    auto *self = static_cast<ModelClippingController *>(clientData);
    if (self == nullptr || !self->m_enabled) {
        return;
    }

    if (eventId == vtkCommand::InteractionEvent) {
        self->m_previewDirty = true;
        self->updateStatus(QStringLiteral("正在调整裁剪框，松手后更新裁剪结果"));
        return;
    }

    if (eventId == vtkCommand::EndInteractionEvent && !self->m_manualFaceDragActive) {
        self->commitPreview();
        self->updateStatus(QStringLiteral("模型裁剪已更新，可继续拖拽面伸缩裁剪框，空白处左键可旋转视角"));
    }
}

void ModelClippingController::resetInteractionState()
{
    m_previewDirty = false;
    m_manualFaceDragActive = false;
    m_manualFaceInteractionState = vtkBoxRepresentation::Outside;
    m_hoveredInteractionState = vtkBoxRepresentation::Outside;
}

void ModelClippingController::updateStatus(const QString &message) const
{
    if (m_statusHandler) {
        m_statusHandler(message);
    }
}

void ModelClippingController::commitPreview()
{
    if (!m_previewDirty) {
        return;
    }

    m_previewDirty = false;
    if (m_commitHandler) {
        m_commitHandler();
    }
}

void ModelClippingController::refreshHoverState(const QPoint &position)
{
    auto *representation = vtkBoxRepresentation::SafeDownCast(m_widget->GetRepresentation());
    if (representation == nullptr || m_viewport == nullptr) {
        return;
    }

    const int interactionState = resolvedInteractionState(position);
    if (interactionState == m_hoveredInteractionState) {
        return;
    }

    m_hoveredInteractionState = interactionState;
    representation->SetInteractionState(interactionState);
    m_viewport->renderWindow()->Render();
}

int ModelClippingController::resolvedInteractionState(const QPoint &position) const
{
    auto *representation = vtkBoxRepresentation::SafeDownCast(m_widget->GetRepresentation());
    if (representation == nullptr || m_viewport == nullptr) {
        return vtkBoxRepresentation::Outside;
    }

    const auto displayPosition = widgetPositionToDisplayPosition(m_viewport, position);
    int interactionState = representation->ComputeInteractionState(static_cast<int>(displayPosition[0]),
                                                                   static_cast<int>(displayPosition[1]));
    if (interactionState == vtkBoxRepresentation::Rotating) {
        interactionState = faceInteractionStateFromDisplayPosition(representation,
                                                                   m_renderer,
                                                                   displayPosition[0],
                                                                   displayPosition[1]);
    }
    return interactionState;
}
