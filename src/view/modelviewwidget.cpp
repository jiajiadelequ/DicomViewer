#include "modelviewwidget.h"

#include <QEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QToolButton>
#include <QVBoxLayout>

#include <QVTKOpenGLNativeWidget.h>

#include <vtkActor.h>
#include <vtkAxesActor.h>
#include <vtkCamera.h>
#include <vtkCaptionActor2D.h>
#include <vtkCellPicker.h>
#include <vtkCursor3D.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkImageData.h>
#include <vtkInteractorStyleTrackballCamera.h>
#include <vtkMath.h>
#include <vtkNew.h>
#include <vtkOrientationMarkerWidget.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderer.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkTextProperty.h>

#include <algorithm>
#include <cmath>

namespace
{
using Axis = std::array<double, 3>;
using BoundsArray = std::array<double, 6>;

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

ModelViewWidget::ModelViewWidget(QWidget *parent)
    : QWidget(parent)
    , m_viewButton(new QToolButton(this))
    , m_statusLabel(new QLabel(QStringLiteral("等待加载模型"), this))
    , m_vtkWidget(new QVTKOpenGLNativeWidget(this))
    , m_renderer(vtkSmartPointer<vtkRenderer>::New())
    , m_crosshairActor(vtkSmartPointer<vtkActor>::New())
    , m_crosshairCursor(vtkSmartPointer<vtkCursor3D>::New())
    , m_modelPicker(vtkSmartPointer<vtkCellPicker>::New())
    , m_orientationAxes(vtkSmartPointer<vtkAxesActor>::New())
    , m_orientationMarkerWidget(vtkSmartPointer<vtkOrientationMarkerWidget>::New())
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    auto *headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(6);

    m_statusLabel->setStyleSheet(QStringLiteral("color: #6b7280;"));
    m_viewButton->setPopupMode(QToolButton::InstantPopup);

    auto *viewMenu = new QMenu(m_viewButton);
    auto *coronalAction = viewMenu->addAction(QStringLiteral("冠状面"));
    auto *sagittalAction = viewMenu->addAction(QStringLiteral("矢状面"));
    auto *axialAction = viewMenu->addAction(QStringLiteral("横断面"));

    connect(coronalAction, &QAction::triggered, this, [this]() {
        applyStandardView(StandardView::Coronal);
    });
    connect(sagittalAction, &QAction::triggered, this, [this]() {
        applyStandardView(StandardView::Sagittal);
    });
    connect(axialAction, &QAction::triggered, this, [this]() {
        applyStandardView(StandardView::Axial);
    });

    m_viewButton->setMenu(viewMenu);
    updateViewButtonText();

    auto renderWindow = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
    m_vtkWidget->setRenderWindow(renderWindow);
    m_vtkWidget->interactor()->SetInteractorStyle(vtkSmartPointer<vtkInteractorStyleTrackballCamera>::New());
    m_vtkWidget->installEventFilter(this);
    m_vtkWidget->setMouseTracking(true);
    renderWindow->AddRenderer(m_renderer);
    m_renderer->SetBackground(0.12, 0.16, 0.22);

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
    m_renderer->AddActor(m_crosshairActor);

    m_modelPicker->PickFromListOn();
    m_modelPicker->SetTolerance(0.0005);

    // Follow the official VTK OrientationMarkerWidget pattern, but use vtkAxesActor
    // to get a Unity-like XYZ gizmo in the top-right corner.
    m_orientationAxes->SetTotalLength(1.2, 1.2, 1.2);
    m_orientationAxes->SetShaftTypeToCylinder();
    m_orientationAxes->SetCylinderRadius(0.5 * m_orientationAxes->GetCylinderRadius());
    m_orientationAxes->SetConeRadius(1.15 * m_orientationAxes->GetConeRadius());
    m_orientationAxes->SetSphereRadius(1.1 * m_orientationAxes->GetSphereRadius());

    for (vtkCaptionActor2D *caption : { m_orientationAxes->GetXAxisCaptionActor2D(),
                                        m_orientationAxes->GetYAxisCaptionActor2D(),
                                        m_orientationAxes->GetZAxisCaptionActor2D() }) {
        caption->GetCaptionTextProperty()->BoldOn();
        caption->GetCaptionTextProperty()->ItalicOff();
        caption->GetCaptionTextProperty()->ShadowOff();
    }

    m_orientationMarkerWidget->SetOrientationMarker(m_orientationAxes);
    m_orientationMarkerWidget->SetInteractor(m_vtkWidget->interactor());
    m_orientationMarkerWidget->SetViewport(0.80, 0.80, 0.98, 0.98);
    m_orientationMarkerWidget->SetOutlineColor(0.78, 0.81, 0.86);
    m_orientationMarkerWidget->SetEnabled(1);
    m_orientationMarkerWidget->InteractiveOff();

    headerLayout->addStretch(1);
    headerLayout->addWidget(m_viewButton);

    layout->addLayout(headerLayout);
    layout->addWidget(m_vtkWidget, 1);
    layout->addWidget(m_statusLabel);
}

void ModelViewWidget::clearScene(const QString &message)
{
    m_crosshairDragActive = false;
    m_cursorWorldPosition = Axis { 0.0, 0.0, 0.0 };
    m_modelActors.clear();
    m_modelPicker->InitializePickList();
    m_hasReferenceBounds = false;
    m_hasModelBounds = false;
    m_referenceBounds = BoundsArray { 0.0, -1.0, 0.0, -1.0, 0.0, -1.0 };
    m_modelBounds = BoundsArray { 0.0, -1.0, 0.0, -1.0, 0.0, -1.0 };

    m_renderer->RemoveAllViewProps();
    m_renderer->AddActor(m_crosshairActor);
    m_crosshairActor->SetVisibility(false);

    m_statusLabel->setText(message);
    m_vtkWidget->renderWindow()->Render();
}

void ModelViewWidget::addModelData(const QString &filePath, vtkPolyData *polyData)
{
    if (polyData == nullptr || (polyData->GetNumberOfPoints() <= 0 && polyData->GetNumberOfCells() <= 0)) {
        return;
    }

    const QFileInfo info(filePath);

    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputData(polyData);

    auto actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);
    actor->GetProperty()->SetOpacity(1.0);

    double polyBounds[6] { 0.0, -1.0, 0.0, -1.0, 0.0, -1.0 };
    polyData->GetBounds(polyBounds);
    m_hasModelBounds = mergeBounds(polyBounds, m_modelBounds.data(), m_hasModelBounds);

    m_modelActors.push_back(actor);
    m_renderer->AddActor(actor);
    m_modelPicker->AddPickList(actor);

    updateCrosshairGeometry();
    resetCameraToAnatomicalView();
    m_statusLabel->setText(QStringLiteral("已加载模型: %1").arg(info.fileName()));
    m_vtkWidget->renderWindow()->Render();
}

void ModelViewWidget::setReferenceImageData(vtkImageData *imageData)
{
    m_hasReferenceBounds = false;
    m_referenceBounds = BoundsArray { 0.0, -1.0, 0.0, -1.0, 0.0, -1.0 };

    if (imageData != nullptr) {
        double bounds[6] { 0.0, -1.0, 0.0, -1.0, 0.0, -1.0 };
        imageData->GetBounds(bounds);
        if (vtkMath::AreBoundsInitialized(bounds)) {
            std::copy(bounds, bounds + 6, m_referenceBounds.begin());
            m_hasReferenceBounds = true;
        }
    }

    updateCrosshairGeometry();
    m_vtkWidget->renderWindow()->Render();
}

void ModelViewWidget::setCrosshairEnabled(bool enabled)
{
    m_crosshairEnabled = enabled;
    m_crosshairDragActive = false;
    updateCrosshairGeometry();
    m_vtkWidget->renderWindow()->Render();
}

void ModelViewWidget::setCursorWorldPosition(double x, double y, double z)
{
    setCursorWorldPositionInternal(Axis { x, y, z }, false);
}

std::array<double, 3> ModelViewWidget::cursorWorldPosition() const
{
    return m_cursorWorldPosition;
}

bool ModelViewWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_vtkWidget || !m_crosshairEnabled || m_modelActors.empty()) {
        return QWidget::eventFilter(watched, event);
    }

    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() != Qt::LeftButton) {
            break;
        }

        Axis worldPosition;
        if (!pickWorldPosition(mouseEvent->pos(), &worldPosition)) {
            break;
        }

        m_crosshairDragActive = true;
        setCursorWorldPositionInternal(worldPosition, true);
        return true;
    }
    case QEvent::MouseMove: {
        if (!m_crosshairDragActive) {
            break;
        }

        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if ((mouseEvent->buttons() & Qt::LeftButton) == 0) {
            m_crosshairDragActive = false;
            break;
        }

        Axis worldPosition;
        if (pickWorldPosition(mouseEvent->pos(), &worldPosition)) {
            setCursorWorldPositionInternal(worldPosition, true);
        }
        return true;
    }
    case QEvent::MouseButtonRelease: {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (m_crosshairDragActive && mouseEvent->button() == Qt::LeftButton) {
            m_crosshairDragActive = false;
            return true;
        }
        break;
    }
    default:
        break;
    }

    return QWidget::eventFilter(watched, event);
}

void ModelViewWidget::updateViewButtonText()
{
    QString viewName;
    switch (m_currentStandardView) {
    case StandardView::Coronal:
        viewName = QStringLiteral("冠状面");
        break;
    case StandardView::Sagittal:
        viewName = QStringLiteral("矢状面");
        break;
    case StandardView::Axial:
        viewName = QStringLiteral("横断面");
        break;
    }

    m_viewButton->setText(QStringLiteral("标准视角: %1").arg(viewName));
}

void ModelViewWidget::resetCameraToAnatomicalView()
{
    applyStandardView(StandardView::Coronal);
}

void ModelViewWidget::applyStandardView(StandardView view)
{
    double bounds[6] { 0.0, -1.0, 0.0, -1.0, 0.0, -1.0 };
    if (!cameraBounds(bounds)) {
        return;
    }

    m_currentStandardView = view;
    updateViewButtonText();

    auto *camera = m_renderer->GetActiveCamera();
    const double center[3] {
        0.5 * (bounds[0] + bounds[1]),
        0.5 * (bounds[2] + bounds[3]),
        0.5 * (bounds[4] + bounds[5])
    };
    const double spanX = std::max(1.0, bounds[1] - bounds[0]);
    const double spanY = std::max(1.0, bounds[3] - bounds[2]);
    const double spanZ = std::max(1.0, bounds[5] - bounds[4]);
    const double distance = 2.5 * std::max({ spanX, spanY, spanZ });

    camera->SetFocalPoint(center);
    switch (view) {
    case StandardView::Coronal:
        // Front view in DICOM LPS: from anterior (-Y) looking posterior (+Y).
        camera->SetPosition(center[0], center[1] - distance, center[2]);
        camera->SetViewUp(0.0, 0.0, 1.0);
        break;
    case StandardView::Sagittal:
        // Left lateral view in DICOM LPS: from patient left (+X) looking right (-X).
        camera->SetPosition(center[0] + distance, center[1], center[2]);
        camera->SetViewUp(0.0, 0.0, 1.0);
        break;
    case StandardView::Axial:
        // Inferior view in DICOM LPS: from feet (-Z) looking superior (+Z).
        camera->SetPosition(center[0], center[1], center[2] - distance);
        camera->SetViewUp(0.0, -1.0, 0.0);
        break;
    }
    camera->OrthogonalizeViewUp();

    m_renderer->ResetCamera(bounds);
    m_renderer->ResetCameraClippingRange(bounds);
    m_vtkWidget->renderWindow()->Render();
}

void ModelViewWidget::setCursorWorldPositionInternal(const Axis &worldPosition, bool emitSignal)
{
    m_cursorWorldPosition = worldPosition;
    updateCrosshairGeometry();
    m_vtkWidget->renderWindow()->Render();

    if (emitSignal) {
        emit cursorWorldPositionChanged(m_cursorWorldPosition[0],
                                        m_cursorWorldPosition[1],
                                        m_cursorWorldPosition[2]);
    }
}

void ModelViewWidget::updateCrosshairGeometry()
{
    double bounds[6] { 0.0, -1.0, 0.0, -1.0, 0.0, -1.0 };
    if (!m_crosshairEnabled || !crosshairBounds(bounds)) {
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

bool ModelViewWidget::pickWorldPosition(const QPoint &widgetPosition, Axis *worldPosition) const
{
    if (worldPosition == nullptr || m_modelActors.empty()) {
        return false;
    }

    const double devicePixelRatio = m_vtkWidget->devicePixelRatioF();
    const int displayX = static_cast<int>(std::lround(widgetPosition.x() * devicePixelRatio));
    const int displayY = static_cast<int>(std::lround((m_vtkWidget->height() - 1 - widgetPosition.y()) * devicePixelRatio));
    if (m_modelPicker->Pick(displayX, displayY, 0.0, m_renderer) == 0) {
        return false;
    }

    double pickedPosition[3] { 0.0, 0.0, 0.0 };
    m_modelPicker->GetPickPosition(pickedPosition);
    *worldPosition = Axis { pickedPosition[0], pickedPosition[1], pickedPosition[2] };
    return true;
}

bool ModelViewWidget::crosshairBounds(double bounds[6]) const
{
    bool hasBounds = false;
    hasBounds = m_hasModelBounds && mergeBounds(m_modelBounds.data(), bounds, hasBounds);
    hasBounds = m_hasReferenceBounds && mergeBounds(m_referenceBounds.data(), bounds, hasBounds);

    if (hasBounds) {
        return true;
    }

    double visibleBounds[6] { 0.0, -1.0, 0.0, -1.0, 0.0, -1.0 };
    m_renderer->ComputeVisiblePropBounds(visibleBounds);
    return mergeBounds(visibleBounds, bounds, false);
}

bool ModelViewWidget::cameraBounds(double bounds[6]) const
{
    if (m_hasModelBounds && mergeBounds(m_modelBounds.data(), bounds, false)) {
        return true;
    }

    if (m_hasReferenceBounds && mergeBounds(m_referenceBounds.data(), bounds, false)) {
        return true;
    }

    double visibleBounds[6] { 0.0, -1.0, 0.0, -1.0, 0.0, -1.0 };
    m_renderer->ComputeVisiblePropBounds(visibleBounds);
    return mergeBounds(visibleBounds, bounds, false);
}
