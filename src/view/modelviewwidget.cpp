#include "modelviewwidget.h"

#include "modelviewcameracontroller.h"
#include "modelviewcrosshaircontroller.h"

#include <QEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QToolButton>
#include <QVBoxLayout>

#include <QVTKOpenGLNativeWidget.h>

#include <vtkActor.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkImageData.h>
#include <vtkInteractorStyleTrackballCamera.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderer.h>
#include <vtkRenderWindowInteractor.h>

ModelViewWidget::ModelViewWidget(QWidget *parent)
    : QWidget(parent)
    , m_statusLabel(new QLabel(QStringLiteral("等待加载模型"), this))
    , m_vtkWidget(new QVTKOpenGLNativeWidget(this))
    , m_cameraController(nullptr)
    , m_crosshairController(nullptr)
    , m_renderer(vtkSmartPointer<vtkRenderer>::New())
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    auto *headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(6);

    m_statusLabel->setStyleSheet(QStringLiteral("color: #6b7280;"));

    auto renderWindow = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
    m_vtkWidget->setRenderWindow(renderWindow);
    m_vtkWidget->interactor()->SetInteractorStyle(vtkSmartPointer<vtkInteractorStyleTrackballCamera>::New());
    m_vtkWidget->installEventFilter(this);
    m_vtkWidget->setMouseTracking(true);
    renderWindow->AddRenderer(m_renderer);
    m_renderer->SetBackground(0.12, 0.16, 0.22);

    m_cameraController = std::make_unique<ModelViewCameraController>(this, m_renderer, m_vtkWidget->interactor());
    m_crosshairController = std::make_unique<ModelViewCrosshairController>(m_renderer);
    m_cameraController->setBoundsProvider([this](ModelViewCameraController::BoundsArray &bounds) {
        return m_crosshairController != nullptr && m_crosshairController->cameraBounds(bounds.data());
    });

    headerLayout->addStretch(1);
    headerLayout->addWidget(m_cameraController->viewButton());

    layout->addLayout(headerLayout);
    layout->addWidget(m_vtkWidget, 1);
    layout->addWidget(m_statusLabel);
}

void ModelViewWidget::beginSceneBatch(const QString &message)
{
    m_sceneBatchActive = true;
    m_sceneNeedsRender = false;
    m_sceneNeedsCameraReset = false;
    clearScene(message);
}

void ModelViewWidget::endSceneBatch(const QString &message)
{
    m_statusLabel->setText(message);
    m_sceneBatchActive = false;
    flushQueuedSceneUpdate();
}

void ModelViewWidget::clearScene(const QString &message)
{
    m_crosshairController->clearScene();
    m_statusLabel->setText(message);
    queueSceneUpdate(false);
}

void ModelViewWidget::addModelData(const QString &filePath,
                                   vtkPolyData *polyData,
                                   const LoadedModelData::MaterialData &material)
{
    if (polyData == nullptr || (polyData->GetNumberOfPoints() <= 0 && polyData->GetNumberOfCells() <= 0)) {
        return;
    }

    const QFileInfo info(filePath);

    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputData(polyData);

    auto actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);
    actor->GetProperty()->SetOpacity(material.hasMaterial ? material.opacity : 1.0);
    if (material.hasMaterial) {
        actor->GetProperty()->SetColor(material.color[0], material.color[1], material.color[2]);
        actor->GetProperty()->SetSpecularColor(material.specularColor[0],
                                               material.specularColor[1],
                                               material.specularColor[2]);
        actor->GetProperty()->SetSpecular(material.specularStrength);
        actor->GetProperty()->SetSpecularPower(material.specularPower);
    }

    m_crosshairController->addModelActor(actor, polyData);
    m_crosshairController->updateGeometry();
    if (!m_sceneBatchActive) {
        m_statusLabel->setText(QStringLiteral("已加载模型: %1").arg(info.fileName()));
    }
    queueSceneUpdate(true);
}

void ModelViewWidget::setModelVisibility(int index, bool visible)
{
    m_crosshairController->setModelVisibility(index, visible);
    m_crosshairController->updateGeometry();
    queueSceneUpdate(false);
}

void ModelViewWidget::setReferenceImageData(vtkImageData *imageData)
{
    m_crosshairController->setReferenceImageData(imageData);
    m_crosshairController->updateGeometry();
    queueSceneUpdate(false);
}

void ModelViewWidget::setCrosshairEnabled(bool enabled)
{
    m_crosshairController->setEnabled(enabled);
    m_crosshairController->updateGeometry();
    queueSceneUpdate(false);
}

void ModelViewWidget::setCursorWorldPosition(double x, double y, double z)
{
    setCursorWorldPositionInternal(ModelViewCrosshairController::Axis { x, y, z }, false);
}

std::array<double, 3> ModelViewWidget::cursorWorldPosition() const
{
    return m_crosshairController->cursorWorldPosition();
}

bool ModelViewWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_vtkWidget) {
        return QWidget::eventFilter(watched, event);
    }

    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() != Qt::LeftButton) {
            break;
        }

        ModelViewCrosshairController::Axis worldPosition;
        if (!m_crosshairController->beginInteraction(m_vtkWidget, mouseEvent->pos(), &worldPosition)) {
            break;
        }

        setCursorWorldPositionInternal(worldPosition, true);
        return true;
    }
    case QEvent::MouseMove: {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        ModelViewCrosshairController::Axis worldPosition;
        if (!m_crosshairController->updateInteraction(m_vtkWidget,
                                                      (mouseEvent->buttons() & Qt::LeftButton) != 0,
                                                      mouseEvent->pos(),
                                                      &worldPosition)) {
            break;
        }

        setCursorWorldPositionInternal(worldPosition, true);
        return true;
    }
    case QEvent::MouseButtonRelease: {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (m_crosshairController->endInteraction(mouseEvent->button() == Qt::LeftButton)) {
            return true;
        }
        break;
    }
    default:
        break;
    }

    return QWidget::eventFilter(watched, event);
}

void ModelViewWidget::setCursorWorldPositionInternal(const std::array<double, 3> &worldPosition, bool emitSignal)
{
    m_crosshairController->setCursorWorldPosition(ModelViewCrosshairController::Axis { worldPosition[0],
                                                                                       worldPosition[1],
                                                                                       worldPosition[2] });
    m_crosshairController->updateGeometry();
    m_vtkWidget->renderWindow()->Render();

    if (emitSignal) {
        const auto currentPosition = m_crosshairController->cursorWorldPosition();
        emit cursorWorldPositionChanged(currentPosition[0],
                                        currentPosition[1],
                                        currentPosition[2]);
    }
}

void ModelViewWidget::queueSceneUpdate(bool resetCamera)
{
    m_sceneNeedsRender = true;
    m_sceneNeedsCameraReset = m_sceneNeedsCameraReset || resetCamera;
    if (!m_sceneBatchActive) {
        flushQueuedSceneUpdate();
    }
}

void ModelViewWidget::flushQueuedSceneUpdate()
{
    if (m_sceneNeedsCameraReset) {
        m_cameraController->resetToAnatomicalView();
    }
    if (m_sceneNeedsRender) {
        m_vtkWidget->renderWindow()->Render();
    }

    m_sceneNeedsRender = false;
    m_sceneNeedsCameraReset = false;
}
