#include "modelviewwidget.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QToolButton>
#include <QVBoxLayout>

#include <QVTKOpenGLNativeWidget.h>

#include <vtkActor.h>
#include <vtkAxesActor.h>
#include <vtkCaptionActor2D.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkNew.h>
#include <vtkOrientationMarkerWidget.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderer.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkTextProperty.h>
#include <vtkCamera.h>
#include <vtkMath.h>

ModelViewWidget::ModelViewWidget(QWidget *parent)
    : QWidget(parent)
    , m_viewButton(new QToolButton(this))
    , m_statusLabel(new QLabel(QStringLiteral("等待加载模型"), this))
    , m_vtkWidget(new QVTKOpenGLNativeWidget(this))
    , m_renderer(vtkSmartPointer<vtkRenderer>::New())
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
    renderWindow->AddRenderer(m_renderer);
    m_renderer->SetBackground(0.95, 0.95, 0.97);

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
    m_renderer->RemoveAllViewProps();
    m_statusLabel->setText(message);
    m_vtkWidget->renderWindow()->Render();
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
    m_renderer->ComputeVisiblePropBounds(bounds);
    if (!vtkMath::AreBoundsInitialized(bounds)) {
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

void ModelViewWidget::addModelData(const QString &filePath, vtkPolyData *polyData)
{
    if (polyData == nullptr || (polyData->GetNumberOfPoints() <= 0 && polyData->GetNumberOfCells() <= 0)) {
        return;
    }

    const QFileInfo info(filePath);

    vtkNew<vtkPolyDataMapper> mapper;
    mapper->SetInputData(polyData);

    vtkNew<vtkActor> actor;
    actor->SetMapper(mapper);
    actor->GetProperty()->SetOpacity(1.0);

    m_renderer->AddActor(actor);
    resetCameraToAnatomicalView();
    m_statusLabel->setText(QStringLiteral("已加载模型: %1").arg(info.fileName()));
    m_vtkWidget->renderWindow()->Render();
}
