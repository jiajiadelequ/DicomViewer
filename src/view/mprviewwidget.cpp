#include "mprviewwidget.h"

#include "mprcrosshaircontroller.h"
#include "mprwindowlevelcontroller.h"

#include <QEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QSlider>
#include <QVBoxLayout>

#include <QVTKOpenGLNativeWidget.h>

#include <vtkCamera.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkImageActor.h>
#include <vtkImageData.h>
#include <vtkImageMapToWindowLevelColors.h>
#include <vtkImageMapper3D.h>
#include <vtkImageReslice.h>
#include <vtkInteractorStyleImage.h>
#include <vtkMath.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace
{
using Axis = MprSliceMath::Axis;
}

MprViewWidget::MprViewWidget(const QString &title, Orientation orientation, QWidget *parent)
    : QWidget(parent)
    , m_orientation(orientation)
    , m_titleLabel(new QLabel(title, this))
    , m_sliceLabel(new QLabel(QStringLiteral("未加载"), this))
    , m_slider(new QSlider(Qt::Horizontal, this))
    , m_vtkWidget(new QVTKOpenGLNativeWidget(this))
    , m_windowLevelController(std::make_unique<MprWindowLevelController>(m_vtkWidget))
    , m_renderer(vtkSmartPointer<vtkRenderer>::New())
    , m_reslice(vtkSmartPointer<vtkImageReslice>::New())
    , m_windowLevel(vtkSmartPointer<vtkImageMapToWindowLevelColors>::New())
    , m_imageActor(vtkSmartPointer<vtkImageActor>::New())
    , m_crosshairController(std::make_unique<MprCrosshairController>(m_imageActor))
    , m_imageData(vtkSmartPointer<vtkImageData>::New())
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    m_titleLabel->setStyleSheet(QStringLiteral("font-size: 12pt; font-weight: 700;"));
    m_sliceLabel->setStyleSheet(QStringLiteral("color: #6b7280;"));
    m_slider->setEnabled(false);

    auto renderWindow = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
    m_vtkWidget->setRenderWindow(renderWindow);
    auto imageStyle = vtkSmartPointer<vtkInteractorStyleImage>::New();
    imageStyle->SetInteractionModeToImage2D();
    m_vtkWidget->interactor()->SetInteractorStyle(imageStyle);
    m_vtkWidget->installEventFilter(this);
    m_vtkWidget->setMouseTracking(true);

    m_reslice->SetOutputDimensionality(2);
    m_reslice->SetInterpolationModeToLinear();
    m_windowLevel->SetInputConnection(m_reslice->GetOutputPort());
    m_windowLevel->SetOutputFormatToLuminance();
    m_imageActor->GetMapper()->SetInputConnection(m_windowLevel->GetOutputPort());
    m_imageActor->InterpolateOff();
    m_imageActor->SetVisibility(false);

    m_renderer->SetBackground(0.05, 0.05, 0.06);
    m_renderer->AddActor(m_imageActor);
    m_renderer->AddActor(m_crosshairController->actor());
    m_vtkWidget->renderWindow()->AddRenderer(m_renderer);
    resetCamera();

    layout->addWidget(m_titleLabel);
    layout->addWidget(m_vtkWidget, 1);
    layout->addWidget(m_sliceLabel);
    layout->addWidget(m_slider);

    connect(m_slider, &QSlider::valueChanged, this, &MprViewWidget::onSliceChanged);
    m_windowLevelController->updateOverlayPosition();
}

MprViewWidget::~MprViewWidget() = default;

void MprViewWidget::setRecommendedWindowLevel(double window, double level)
{
    m_windowLevelController->setRecommendedWindowLevel(window, level);
}

void MprViewWidget::setWindowLevel(double window, double level)
{
    setWindowLevelInternal(window, level, false);
}

void MprViewWidget::clearRecommendedWindowLevel()
{
    m_windowLevelController->clearRecommendedWindowLevel();
}

void MprViewWidget::setImageData(vtkImageData *imageData)
{
    if (imageData == nullptr) {
        clearView(QStringLiteral("未加载"));
        return;
    }

    m_imageData = imageData;
    m_hasImage = true;

    m_reslice->SetInputData(m_imageData);
    configureSliceGeometry(m_imageData);
    if (m_sliceGeometry.sliceCount <= 0) {
        clearView(QStringLiteral("影像切片无效"));
        return;
    }

    m_cursorWorldPosition = m_sliceGeometry.center;
    m_windowLevelController->ensureInitialized(m_imageData);
    m_windowLevelController->applyTo(m_windowLevel);
    m_imageActor->SetVisibility(true);
    m_crosshairController->setVisible(true);
    updateSliceControls();
    renderCurrentState(true);
}

void MprViewWidget::setCrosshairEnabled(bool enabled)
{
    m_crosshairController->setEnabled(enabled);

    if (m_hasImage) {
        renderCurrentState(false);
    }
}

void MprViewWidget::setCursorWorldPosition(double x, double y, double z)
{
    setCursorWorldPositionInternal(Axis { x, y, z }, false, false);
}

std::array<double, 3> MprViewWidget::cursorWorldPosition() const
{
    return m_cursorWorldPosition;
}

void MprViewWidget::refreshView()
{
    renderCurrentState(true);
}

void MprViewWidget::clearView(const QString &message)
{
    m_hasImage = false;
    m_sliceGeometry = SliceGeometry {};
    m_cursorWorldPosition = Axis { 0.0, 0.0, 0.0 };
    m_windowLevelController->reset();

    m_slider->blockSignals(true);
    m_slider->setEnabled(false);
    m_slider->setRange(0, 0);
    m_slider->setValue(0);
    m_slider->blockSignals(false);
    m_sliceLabel->setText(message);

    m_reslice->SetInputData(static_cast<vtkImageData *>(nullptr));
    m_imageActor->SetVisibility(false);
    m_crosshairController->setVisible(false);
    m_vtkWidget->renderWindow()->Render();
}

bool MprViewWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_vtkWidget || !m_hasImage) {
        return QWidget::eventFilter(watched, event);
    }

    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() != Qt::LeftButton) {
            break;
        }

        if (m_crosshairController->isEnabled() && mouseEvent->modifiers() == Qt::NoModifier) {
            Axis worldPosition;
            if (!m_crosshairController->beginInteraction(m_renderer,
                                                         m_vtkWidget,
                                                         mouseEvent->pos(),
                                                         m_sliceGeometry,
                                                         m_slider->value(),
                                                         &worldPosition)) {
                break;
            }

            setCursorWorldPositionInternal(worldPosition, true, false);
            return true;
        }

        if (!m_crosshairController->isEnabled() && mouseEvent->modifiers() == Qt::NoModifier) {
            if (!m_windowLevelController->canStartDrag()) {
                break;
            }

            m_windowLevelController->beginDrag(mouseEvent->pos());
            return true;
        }
        break;
    }
    case QEvent::MouseMove: {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);

        Axis worldPosition;
        if (m_crosshairController->updateInteraction(m_renderer,
                                                     m_vtkWidget,
                                                     (mouseEvent->buttons() & Qt::LeftButton) != 0,
                                                     mouseEvent->pos(),
                                                     m_sliceGeometry,
                                                     m_slider->value(),
                                                     &worldPosition)) {
            setCursorWorldPositionInternal(worldPosition, true, false);
            return true;
        }

        if (m_windowLevelController->isDragging()) {
            if ((mouseEvent->buttons() & Qt::LeftButton) == 0) {
                m_windowLevelController->endDrag();
                break;
            }

            MprWindowLevelController::Values values;
            if (m_windowLevelController->dragWindowLevel(mouseEvent->pos(), m_vtkWidget->size(), &values)) {
                setWindowLevelInternal(values.window, values.level, true);
            }
            return true;
        }
        break;
    }
    case QEvent::MouseButtonRelease: {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (m_crosshairController->endInteraction(mouseEvent->button() == Qt::LeftButton)) {
            return true;
        }
        if (m_windowLevelController->isDragging() && mouseEvent->button() == Qt::LeftButton) {
            m_windowLevelController->endDrag();
            return true;
        }
        break;
    }
    default:
        break;
    }

    return QWidget::eventFilter(watched, event);
}

void MprViewWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    m_windowLevelController->updateOverlayPosition();

    if (!m_hasImage) {
        return;
    }

    renderCurrentState(true);
}

void MprViewWidget::onSliceChanged(int value)
{
    if (!m_hasImage || m_sliceGeometry.sliceCount <= 0) {
        return;
    }

    updateCursorWorldPositionFromSlider(value);
    renderCurrentState(false);
    if (m_crosshairController->isEnabled()) {
        emit cursorWorldPositionChanged(m_cursorWorldPosition[0],
                                        m_cursorWorldPosition[1],
                                        m_cursorWorldPosition[2]);
    }
}

void MprViewWidget::configureSliceGeometry(vtkImageData *imageData)
{
    m_sliceGeometry = MprSliceMath::buildSliceGeometry(imageData, m_orientation);

    m_reslice->SetResliceAxesDirectionCosines(m_sliceGeometry.xAxis.data(),
                                              m_sliceGeometry.yAxis.data(),
                                              m_sliceGeometry.normalAxis.data());
    m_reslice->SetOutputSpacing(m_sliceGeometry.xSpacing, m_sliceGeometry.ySpacing, 1.0);
    m_reslice->SetOutputOrigin(m_sliceGeometry.outputOrigin[0], m_sliceGeometry.outputOrigin[1], 0.0);
    m_reslice->SetOutputExtent(m_sliceGeometry.outputExtent[0],
                               m_sliceGeometry.outputExtent[1],
                               m_sliceGeometry.outputExtent[2],
                               m_sliceGeometry.outputExtent[3],
                               m_sliceGeometry.outputExtent[4],
                               m_sliceGeometry.outputExtent[5]);
}

void MprViewWidget::applyCurrentSlice(int sliderValue)
{
    if (m_sliceGeometry.sliceCount <= 0) {
        return;
    }

    const Axis sliceOrigin = sliceOriginForSliderValue(sliderValue);
    m_reslice->SetResliceAxesOrigin(sliceOrigin[0], sliceOrigin[1], sliceOrigin[2]);
    updateSliceLabel(sliderValue);
}

void MprViewWidget::fitImageToViewport()
{
    if (!m_hasImage || !m_imageActor->GetVisibility()) {
        return;
    }

    if (!m_vtkWidget->isVisible() || m_vtkWidget->width() <= 0 || m_vtkWidget->height() <= 0) {
        return;
    }

    m_reslice->Update();
    m_windowLevel->Update();

    const double *bounds = m_imageActor->GetBounds();
    if (vtkMath::AreBoundsInitialized(bounds)) {
        m_renderer->ResetCameraScreenSpace(bounds, 0.98);
    } else {
        m_renderer->ResetCameraScreenSpace(0.98);
    }

    m_renderer->ResetCameraClippingRange();
}

void MprViewWidget::resetCamera()
{
    auto *camera = m_renderer->GetActiveCamera();
    camera->ParallelProjectionOn();
    camera->SetPosition(0.0, 0.0, 1.0);
    camera->SetFocalPoint(0.0, 0.0, 0.0);
    camera->SetViewUp(0.0, 1.0, 0.0);
    fitImageToViewport();
}

void MprViewWidget::renderCurrentState(bool fitViewport)
{
    if (!m_hasImage || m_sliceGeometry.sliceCount <= 0) {
        return;
    }

    m_crosshairController->setVisible(true);
    m_windowLevelController->ensureInitialized(m_imageData);
    m_windowLevelController->applyTo(m_windowLevel);
    applyCurrentSlice(m_slider->value());
    m_crosshairController->updateGeometry(m_cursorWorldPosition, m_sliceGeometry, m_slider->value());

    if (!m_vtkWidget->isVisible() || m_vtkWidget->width() <= 0 || m_vtkWidget->height() <= 0) {
        return;
    }

    if (fitViewport) {
        resetCamera();
    } else {
        m_reslice->Update();
        m_windowLevel->Update();
        m_renderer->ResetCameraClippingRange();
    }

    m_vtkWidget->renderWindow()->Render();
}

void MprViewWidget::setCursorWorldPositionInternal(const Axis &worldPosition, bool emitSignal, bool fitViewport)
{
    if (!m_hasImage || m_sliceGeometry.sliceCount <= 0) {
        m_cursorWorldPosition = worldPosition;
        return;
    }

    const double xPosition = MprSliceMath::dotProduct(MprSliceMath::subtractAxes(worldPosition, m_sliceGeometry.center), m_sliceGeometry.xAxis);
    const double yPosition = MprSliceMath::dotProduct(MprSliceMath::subtractAxes(worldPosition, m_sliceGeometry.center), m_sliceGeometry.yAxis);
    const int sliderValue = sliderValueForWorldPosition(worldPosition);
    const Axis sliceOrigin = sliceOriginForSliderValue(sliderValue);
    m_cursorWorldPosition = MprSliceMath::addAxes(MprSliceMath::addAxes(sliceOrigin,
                                                                        MprSliceMath::scaleAxis(m_sliceGeometry.xAxis, xPosition)),
                                                  MprSliceMath::scaleAxis(m_sliceGeometry.yAxis, yPosition));

    m_slider->blockSignals(true);
    m_slider->setValue(sliderValue);
    m_slider->blockSignals(false);

    renderCurrentState(fitViewport);

    if (emitSignal) {
        emit cursorWorldPositionChanged(m_cursorWorldPosition[0],
                                        m_cursorWorldPosition[1],
                                        m_cursorWorldPosition[2]);
    }
}

void MprViewWidget::updateCursorWorldPositionFromSlider(int sliderValue)
{
    const double xPosition = MprSliceMath::dotProduct(MprSliceMath::subtractAxes(m_cursorWorldPosition, m_sliceGeometry.center), m_sliceGeometry.xAxis);
    const double yPosition = MprSliceMath::dotProduct(MprSliceMath::subtractAxes(m_cursorWorldPosition, m_sliceGeometry.center), m_sliceGeometry.yAxis);
    const Axis sliceOrigin = sliceOriginForSliderValue(sliderValue);
    m_cursorWorldPosition = MprSliceMath::addAxes(MprSliceMath::addAxes(sliceOrigin,
                                                                        MprSliceMath::scaleAxis(m_sliceGeometry.xAxis, xPosition)),
                                                  MprSliceMath::scaleAxis(m_sliceGeometry.yAxis, yPosition));
}

void MprViewWidget::updateSliceControls()
{
    const int currentValue = sliderValueForWorldPosition(m_cursorWorldPosition);

    m_slider->blockSignals(true);
    m_slider->setEnabled(m_sliceGeometry.sliceCount > 0);
    m_slider->setRange(0, std::max(0, m_sliceGeometry.sliceCount - 1));
    m_slider->setValue(currentValue);
    m_slider->blockSignals(false);

    applyCurrentSlice(currentValue);
}

void MprViewWidget::updateSliceLabel(int sliderValue)
{
    if (m_sliceGeometry.sliceCount <= 0) {
        m_sliceLabel->setText(QStringLiteral("未加载"));
        return;
    }

    const int clampedValue = std::clamp(sliderValue, 0, m_sliceGeometry.sliceCount - 1);
    m_sliceLabel->setText(QStringLiteral("%1 Slice: %2 / %3")
                              .arg(MprSliceMath::orientationName(m_orientation))
                              .arg(clampedValue + 1)
                              .arg(m_sliceGeometry.sliceCount));
}

void MprViewWidget::setWindowLevelInternal(double window, double level, bool emitSignal)
{
    m_windowLevelController->setCurrentWindowLevel(window, level);
    m_windowLevelController->applyTo(m_windowLevel);
    m_vtkWidget->renderWindow()->Render();

    if (emitSignal) {
        const auto values = m_windowLevelController->currentWindowLevel();
        emit windowLevelChanged(values.window, values.level);
    }
}

std::array<double, 3> MprViewWidget::sliceOriginForSliderValue(int sliderValue) const
{
    return MprSliceMath::sliceOriginForSliderValue(m_sliceGeometry, sliderValue);
}

int MprViewWidget::sliderValueForWorldPosition(const Axis &worldPosition) const
{
    return MprSliceMath::sliderValueForWorldPosition(m_sliceGeometry, worldPosition);
}
