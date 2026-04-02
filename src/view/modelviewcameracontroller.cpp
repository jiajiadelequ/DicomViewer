#include "modelviewcameracontroller.h"

#include <QAction>
#include <QMenu>
#include <QToolButton>
#include <QWidget>

#include <vtkAxesActor.h>
#include <vtkCamera.h>
#include <vtkCaptionActor2D.h>
#include <vtkOrientationMarkerWidget.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>
#include <vtkTextProperty.h>

#include <algorithm>

ModelViewCameraController::ModelViewCameraController(QWidget *buttonParent,
                                                     vtkRenderer *renderer,
                                                     vtkRenderWindowInteractor *interactor)
    : m_viewButton(new QToolButton(buttonParent))
    , m_renderer(renderer)
    , m_orientationAxes(vtkSmartPointer<vtkAxesActor>::New())
    , m_orientationMarkerWidget(vtkSmartPointer<vtkOrientationMarkerWidget>::New())
{
    m_viewButton->setPopupMode(QToolButton::InstantPopup);

    auto *viewMenu = new QMenu(m_viewButton);
    auto *coronalAction = viewMenu->addAction(QStringLiteral("冠状面"));
    auto *sagittalAction = viewMenu->addAction(QStringLiteral("矢状面"));
    auto *axialAction = viewMenu->addAction(QStringLiteral("横断面"));

    QObject::connect(coronalAction, &QAction::triggered, m_viewButton, [this]() {
        applyStandardView(StandardView::Coronal);
    });
    QObject::connect(sagittalAction, &QAction::triggered, m_viewButton, [this]() {
        applyStandardView(StandardView::Sagittal);
    });
    QObject::connect(axialAction, &QAction::triggered, m_viewButton, [this]() {
        applyStandardView(StandardView::Axial);
    });

    m_viewButton->setMenu(viewMenu);
    updateViewButtonText();

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
    m_orientationMarkerWidget->SetInteractor(interactor);
    m_orientationMarkerWidget->SetViewport(0.80, 0.80, 0.98, 0.98);
    m_orientationMarkerWidget->SetOutlineColor(0.78, 0.81, 0.86);
    m_orientationMarkerWidget->SetEnabled(1);
    m_orientationMarkerWidget->InteractiveOff();
}

ModelViewCameraController::~ModelViewCameraController() = default;

QToolButton *ModelViewCameraController::viewButton() const
{
    return m_viewButton;
}

void ModelViewCameraController::setBoundsProvider(std::function<bool(BoundsArray &bounds)> provider)
{
    m_boundsProvider = std::move(provider);
}

void ModelViewCameraController::resetToAnatomicalView()
{
    applyStandardView(StandardView::Coronal);
}

void ModelViewCameraController::refreshCurrentView()
{
    applyStandardView(m_currentStandardView);
}

void ModelViewCameraController::updateViewButtonText()
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

bool ModelViewCameraController::applyStandardView(StandardView view)
{
    if (m_renderer == nullptr || !m_boundsProvider) {
        return false;
    }

    BoundsArray bounds { 0.0, -1.0, 0.0, -1.0, 0.0, -1.0 };
    if (!m_boundsProvider(bounds)) {
        return false;
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
        camera->SetPosition(center[0], center[1] - distance, center[2]);
        camera->SetViewUp(0.0, 0.0, 1.0);
        break;
    case StandardView::Sagittal:
        camera->SetPosition(center[0] + distance, center[1], center[2]);
        camera->SetViewUp(0.0, 0.0, 1.0);
        break;
    case StandardView::Axial:
        camera->SetPosition(center[0], center[1], center[2] - distance);
        camera->SetViewUp(0.0, -1.0, 0.0);
        break;
    }
    camera->OrthogonalizeViewUp();

    m_renderer->ResetCamera(bounds.data());
    m_renderer->ResetCameraClippingRange(bounds.data());
    if (auto *renderWindow = m_renderer->GetRenderWindow(); renderWindow != nullptr) {
        renderWindow->Render();
    }
    return true;
}
