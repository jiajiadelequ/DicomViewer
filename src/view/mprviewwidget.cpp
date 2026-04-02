#include "mprviewwidget.h"

#include <QEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QSlider>
#include <QVBoxLayout>

#include <QVTKOpenGLNativeWidget.h>

#include <vtkActor.h>
#include <vtkCamera.h>
#include <vtkCellArray.h>
#include <vtkCellPicker.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkImageActor.h>
#include <vtkImageData.h>
#include <vtkImageMapToWindowLevelColors.h>
#include <vtkImageMapper3D.h>
#include <vtkImageReslice.h>
#include <vtkInteractorStyleImage.h>
#include <vtkMath.h>
#include <vtkMatrix3x3.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace
{
using Axis = std::array<double, 3>;

QString orientationName(MprViewWidget::Orientation orientation)
{
    switch (orientation) {
    case MprViewWidget::Orientation::Axial:
        return QStringLiteral("Axial");
    case MprViewWidget::Orientation::Coronal:
        return QStringLiteral("Coronal");
    case MprViewWidget::Orientation::Sagittal:
        return QStringLiteral("Sagittal");
    }

    return QStringLiteral("Unknown");
}

struct OrientationPreset
{
    Axis xAxis;
    Axis yAxis;
    bool reverseSlider = false;
};

OrientationPreset orientationPreset(MprViewWidget::Orientation orientation)
{
    switch (orientation) {
    case MprViewWidget::Orientation::Axial:
        return { Axis { 1.0, 0.0, 0.0 }, Axis { 0.0, -1.0, 0.0 }, true };
    case MprViewWidget::Orientation::Coronal:
        return { Axis { 1.0, 0.0, 0.0 }, Axis { 0.0, 0.0, 1.0 }, false };
    case MprViewWidget::Orientation::Sagittal:
        return { Axis { 0.0, 1.0, 0.0 }, Axis { 0.0, 0.0, 1.0 }, false };
    }

    return { Axis { 1.0, 0.0, 0.0 }, Axis { 0.0, 1.0, 0.0 }, false };
}

double directionElement(vtkImageData *imageData, int row, int column)
{
    auto *directionMatrix = imageData != nullptr ? imageData->GetDirectionMatrix() : nullptr;
    if (directionMatrix == nullptr) {
        return row == column ? 1.0 : 0.0;
    }

    return directionMatrix->GetElement(row, column);
}

Axis crossProduct(const Axis &lhs, const Axis &rhs)
{
    return Axis {
        lhs[1] * rhs[2] - lhs[2] * rhs[1],
        lhs[2] * rhs[0] - lhs[0] * rhs[2],
        lhs[0] * rhs[1] - lhs[1] * rhs[0]
    };
}

double dotProduct(const Axis &lhs, const Axis &rhs)
{
    return lhs[0] * rhs[0] + lhs[1] * rhs[1] + lhs[2] * rhs[2];
}

double magnitude(const Axis &axis)
{
    return std::sqrt(dotProduct(axis, axis));
}

Axis normalizeAxis(const Axis &axis)
{
    const double length = magnitude(axis);
    if (length <= 0.0) {
        return Axis { 0.0, 0.0, 1.0 };
    }

    return Axis { axis[0] / length, axis[1] / length, axis[2] / length };
}

Axis subtractAxes(const Axis &lhs, const Axis &rhs)
{
    return Axis { lhs[0] - rhs[0], lhs[1] - rhs[1], lhs[2] - rhs[2] };
}

Axis addAxes(const Axis &lhs, const Axis &rhs)
{
    return Axis { lhs[0] + rhs[0], lhs[1] + rhs[1], lhs[2] + rhs[2] };
}

Axis scaleAxis(const Axis &axis, double scale)
{
    return Axis { axis[0] * scale, axis[1] * scale, axis[2] * scale };
}

Axis pointFromIndex(vtkImageData *imageData, double i, double j, double k)
{
    double origin[3] { 0.0, 0.0, 0.0 };
    double spacing[3] { 1.0, 1.0, 1.0 };
    imageData->GetOrigin(origin);
    imageData->GetSpacing(spacing);

    const double scaledIndex[3] { i * spacing[0], j * spacing[1], k * spacing[2] };
    Axis point { origin[0], origin[1], origin[2] };
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            point[row] += directionElement(imageData, row, column) * scaledIndex[column];
        }
    }

    return point;
}

Axis imageCenter(vtkImageData *imageData)
{
    int extent[6] { 0, -1, 0, -1, 0, -1 };
    imageData->GetExtent(extent);

    return pointFromIndex(imageData,
                          0.5 * static_cast<double>(extent[0] + extent[1]),
                          0.5 * static_cast<double>(extent[2] + extent[3]),
                          0.5 * static_cast<double>(extent[4] + extent[5]));
}

std::pair<double, double> projectionRange(vtkImageData *imageData, const Axis &center, const Axis &axis)
{
    int extent[6] { 0, -1, 0, -1, 0, -1 };
    imageData->GetExtent(extent);

    double minimum = 0.0;
    double maximum = 0.0;
    bool firstSample = true;

    for (int i : { extent[0], extent[1] }) {
        for (int j : { extent[2], extent[3] }) {
            for (int k : { extent[4], extent[5] }) {
                const Axis point = pointFromIndex(imageData, i, j, k);
                const double projection = dotProduct(subtractAxes(point, center), axis);
                if (firstSample) {
                    minimum = projection;
                    maximum = projection;
                    firstSample = false;
                } else {
                    minimum = std::min(minimum, projection);
                    maximum = std::max(maximum, projection);
                }
            }
        }
    }

    return { minimum, maximum };
}

double spacingAlongAxis(vtkImageData *imageData, const Axis &axis)
{
    double spacing[3] { 1.0, 1.0, 1.0 };
    imageData->GetSpacing(spacing);

    Axis worldToIndex {
        (directionElement(imageData, 0, 0) * axis[0] + directionElement(imageData, 1, 0) * axis[1] + directionElement(imageData, 2, 0) * axis[2])
            / std::max(spacing[0], 1e-6),
        (directionElement(imageData, 0, 1) * axis[0] + directionElement(imageData, 1, 1) * axis[1] + directionElement(imageData, 2, 1) * axis[2])
            / std::max(spacing[1], 1e-6),
        (directionElement(imageData, 0, 2) * axis[0] + directionElement(imageData, 1, 2) * axis[1] + directionElement(imageData, 2, 2) * axis[2])
            / std::max(spacing[2], 1e-6)
    };

    const double indexStep = magnitude(worldToIndex);
    return indexStep > 0.0 ? 1.0 / indexStep : 1.0;
}

int sampleCount(double minimum, double maximum, double spacing)
{
    if (spacing <= 0.0) {
        return 1;
    }

    return std::max(1, static_cast<int>(std::lround((maximum - minimum) / spacing)) + 1);
}

double maxOutputCoordinate(double origin, double spacing, int minExtent, int maxExtent)
{
    return origin + static_cast<double>(std::max(0, maxExtent - minExtent)) * spacing;
}
}

MprViewWidget::MprViewWidget(const QString &title, Orientation orientation, QWidget *parent)
    : QWidget(parent)
    , m_orientation(orientation)
    , m_titleLabel(new QLabel(title, this))
    , m_windowLevelLabel(new QLabel(this))
    , m_sliceLabel(new QLabel(QStringLiteral("未加载"), this))
    , m_slider(new QSlider(Qt::Horizontal, this))
    , m_vtkWidget(new QVTKOpenGLNativeWidget(this))
    , m_renderer(vtkSmartPointer<vtkRenderer>::New())
    , m_reslice(vtkSmartPointer<vtkImageReslice>::New())
    , m_windowLevel(vtkSmartPointer<vtkImageMapToWindowLevelColors>::New())
    , m_imageActor(vtkSmartPointer<vtkImageActor>::New())
    , m_crosshairActor(vtkSmartPointer<vtkActor>::New())
    , m_crosshairPolyData(vtkSmartPointer<vtkPolyData>::New())
    , m_crosshairPoints(vtkSmartPointer<vtkPoints>::New())
    , m_imagePicker(vtkSmartPointer<vtkCellPicker>::New())
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

    m_windowLevelLabel->setParent(m_vtkWidget);
    m_windowLevelLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_windowLevelLabel->setStyleSheet(QStringLiteral(
        "padding: 4px 8px;"
        "border-radius: 4px;"
        "background-color: rgba(10, 12, 16, 180);"
        "color: #f3f4f6;"
        "font-size: 10pt;"
        "font-weight: 600;"));
    m_windowLevelLabel->setText(QStringLiteral("WW: -, WL: -"));
    m_windowLevelLabel->hide();

    m_reslice->SetOutputDimensionality(2);
    m_reslice->SetInterpolationModeToLinear();
    m_windowLevel->SetInputConnection(m_reslice->GetOutputPort());
    m_windowLevel->SetOutputFormatToLuminance();
    m_imageActor->GetMapper()->SetInputConnection(m_windowLevel->GetOutputPort());
    m_imageActor->InterpolateOff();
    m_imageActor->SetVisibility(false);

    m_crosshairPoints->SetNumberOfPoints(4);
    auto crosshairLines = vtkSmartPointer<vtkCellArray>::New();
    crosshairLines->InsertNextCell(2);
    crosshairLines->InsertCellPoint(0);
    crosshairLines->InsertCellPoint(1);
    crosshairLines->InsertNextCell(2);
    crosshairLines->InsertCellPoint(2);
    crosshairLines->InsertCellPoint(3);
    m_crosshairPolyData->SetPoints(m_crosshairPoints);
    m_crosshairPolyData->SetLines(crosshairLines);

    auto crosshairMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    crosshairMapper->SetInputData(m_crosshairPolyData);
    m_crosshairActor->SetMapper(crosshairMapper);
    m_crosshairActor->GetProperty()->SetColor(0.95, 0.82, 0.15);
    m_crosshairActor->GetProperty()->SetLineWidth(2.0f);
    m_crosshairActor->GetProperty()->LightingOff();
    m_crosshairActor->PickableOff();
    m_crosshairActor->SetVisibility(false);

    m_imagePicker->PickFromListOn();
    m_imagePicker->AddPickList(m_imageActor);
    m_imagePicker->SetTolerance(0.0005);

    m_renderer->SetBackground(0.05, 0.05, 0.06);
    m_renderer->AddActor(m_imageActor);
    m_renderer->AddActor(m_crosshairActor);
    m_vtkWidget->renderWindow()->AddRenderer(m_renderer);
    resetCamera();

    layout->addWidget(m_titleLabel);
    layout->addWidget(m_vtkWidget, 1);
    layout->addWidget(m_sliceLabel);
    layout->addWidget(m_slider);

    connect(m_slider, &QSlider::valueChanged, this, &MprViewWidget::onSliceChanged);
    updateOverlayPosition();
}

void MprViewWidget::setRecommendedWindowLevel(double window, double level)
{
    if (!std::isfinite(window) || !std::isfinite(level) || window <= 0.0) {
        clearRecommendedWindowLevel();
        return;
    }

    m_recommendedWindow = window;
    m_recommendedLevel = level;
    m_hasRecommendedWindowLevel = true;
}

void MprViewWidget::setWindowLevel(double window, double level)
{
    setWindowLevelInternal(window, level, false);
}

void MprViewWidget::clearRecommendedWindowLevel()
{
    m_recommendedWindow = 0.0;
    m_recommendedLevel = 0.0;
    m_hasRecommendedWindowLevel = false;
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
    updateWindowLevel(m_imageData);
    applyWindowLevelValues();
    m_imageActor->SetVisibility(true);
    m_crosshairActor->SetVisibility(m_crosshairEnabled);
    updateSliceControls();
    renderCurrentState(true);
}

void MprViewWidget::setCrosshairEnabled(bool enabled)
{
    m_crosshairEnabled = enabled;
    m_crosshairDragActive = false;
    m_crosshairActor->SetVisibility(m_hasImage && m_crosshairEnabled);

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
    m_crosshairDragActive = false;
    m_sliceGeometry = SliceGeometry {};
    m_cursorWorldPosition = Axis { 0.0, 0.0, 0.0 };
    m_currentWindow = 0.0;
    m_currentLevel = 0.0;
    m_windowLevelDragActive = false;

    m_slider->blockSignals(true);
    m_slider->setEnabled(false);
    m_slider->setRange(0, 0);
    m_slider->setValue(0);
    m_slider->blockSignals(false);
    m_sliceLabel->setText(message);

    m_reslice->SetInputData(static_cast<vtkImageData *>(nullptr));
    m_imageActor->SetVisibility(false);
    m_crosshairActor->SetVisibility(false);
    updateWindowLevelOverlay();
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

        if (m_crosshairEnabled && mouseEvent->modifiers() == Qt::NoModifier) {
            Axis worldPosition;
            if (!pickWorldPosition(mouseEvent->pos(), &worldPosition)) {
                break;
            }

            m_crosshairDragActive = true;
            setCursorWorldPositionInternal(worldPosition, true, false);
            return true;
        }

        if (!m_crosshairEnabled && mouseEvent->modifiers() == Qt::NoModifier) {
            m_windowLevelDragActive = true;
            m_windowLevelDragStartPosition = mouseEvent->pos();
            m_windowLevelDragStartWindow = m_currentWindow;
            m_windowLevelDragStartLevel = m_currentLevel;
            return true;
        }
        break;
    }
    case QEvent::MouseMove: {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);

        if (m_crosshairDragActive) {
            if ((mouseEvent->buttons() & Qt::LeftButton) == 0) {
                m_crosshairDragActive = false;
                break;
            }

            Axis worldPosition;
            if (pickWorldPosition(mouseEvent->pos(), &worldPosition)) {
                setCursorWorldPositionInternal(worldPosition, true, false);
            }
            return true;
        }

        if (m_windowLevelDragActive) {
            if ((mouseEvent->buttons() & Qt::LeftButton) == 0) {
                m_windowLevelDragActive = false;
                break;
            }

            const double widthScale = std::max(1.0, m_windowLevelDragStartWindow);
            const double levelScale = std::max(1.0, m_windowLevelDragStartWindow);
            const double deltaX = static_cast<double>(mouseEvent->pos().x() - m_windowLevelDragStartPosition.x());
            const double deltaY = static_cast<double>(mouseEvent->pos().y() - m_windowLevelDragStartPosition.y());
            const double normalizedX = deltaX / std::max(1, m_vtkWidget->width());
            const double normalizedY = deltaY / std::max(1, m_vtkWidget->height());

            const double window = std::max(1.0, m_windowLevelDragStartWindow + normalizedX * widthScale * 4.0);
            const double level = m_windowLevelDragStartLevel - normalizedY * levelScale * 4.0;
            setWindowLevelInternal(window, level, true);
            return true;
        }
        break;
    }
    case QEvent::MouseButtonRelease: {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (m_crosshairDragActive && mouseEvent->button() == Qt::LeftButton) {
            m_crosshairDragActive = false;
            return true;
        }
        if (m_windowLevelDragActive && mouseEvent->button() == Qt::LeftButton) {
            m_windowLevelDragActive = false;
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
    updateOverlayPosition();

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
    if (m_crosshairEnabled) {
        emit cursorWorldPositionChanged(m_cursorWorldPosition[0],
                                        m_cursorWorldPosition[1],
                                        m_cursorWorldPosition[2]);
    }
}

void MprViewWidget::configureSliceGeometry(vtkImageData *imageData)
{
    m_sliceGeometry = SliceGeometry {};
    if (imageData == nullptr) {
        return;
    }

    const auto preset = orientationPreset(m_orientation);
    m_sliceGeometry.center = imageCenter(imageData);
    m_sliceGeometry.xAxis = normalizeAxis(preset.xAxis);
    m_sliceGeometry.yAxis = normalizeAxis(preset.yAxis);
    m_sliceGeometry.normalAxis = normalizeAxis(crossProduct(m_sliceGeometry.xAxis, m_sliceGeometry.yAxis));
    m_sliceGeometry.reverseSlider = preset.reverseSlider;

    const auto xRange = projectionRange(imageData, m_sliceGeometry.center, m_sliceGeometry.xAxis);
    const auto yRange = projectionRange(imageData, m_sliceGeometry.center, m_sliceGeometry.yAxis);
    const auto sliceRange = projectionRange(imageData, m_sliceGeometry.center, m_sliceGeometry.normalAxis);

    m_sliceGeometry.xSpacing = std::max(1e-3, spacingAlongAxis(imageData, m_sliceGeometry.xAxis));
    m_sliceGeometry.ySpacing = std::max(1e-3, spacingAlongAxis(imageData, m_sliceGeometry.yAxis));
    m_sliceGeometry.sliceSpacing = std::max(1e-3, spacingAlongAxis(imageData, m_sliceGeometry.normalAxis));
    m_sliceGeometry.minSlice = sliceRange.first;
    m_sliceGeometry.sliceCount = sampleCount(sliceRange.first, sliceRange.second, m_sliceGeometry.sliceSpacing);
    m_sliceGeometry.outputOrigin = { xRange.first, yRange.first, 0.0 };
    m_sliceGeometry.outputExtent = {
        0,
        sampleCount(xRange.first, xRange.second, m_sliceGeometry.xSpacing) - 1,
        0,
        sampleCount(yRange.first, yRange.second, m_sliceGeometry.ySpacing) - 1,
        0,
        0
    };

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

    m_crosshairActor->SetVisibility(m_crosshairEnabled);
    updateWindowLevel(m_imageData);
    applyWindowLevelValues();
    applyCurrentSlice(m_slider->value());
    updateCrosshairGeometry();

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

    const double xPosition = dotProduct(subtractAxes(worldPosition, m_sliceGeometry.center), m_sliceGeometry.xAxis);
    const double yPosition = dotProduct(subtractAxes(worldPosition, m_sliceGeometry.center), m_sliceGeometry.yAxis);
    const int sliderValue = sliderValueForWorldPosition(worldPosition);
    const Axis sliceOrigin = sliceOriginForSliderValue(sliderValue);
    m_cursorWorldPosition = addAxes(addAxes(sliceOrigin,
                                            scaleAxis(m_sliceGeometry.xAxis, xPosition)),
                                    scaleAxis(m_sliceGeometry.yAxis, yPosition));

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
    const double xPosition = dotProduct(subtractAxes(m_cursorWorldPosition, m_sliceGeometry.center), m_sliceGeometry.xAxis);
    const double yPosition = dotProduct(subtractAxes(m_cursorWorldPosition, m_sliceGeometry.center), m_sliceGeometry.yAxis);
    const Axis sliceOrigin = sliceOriginForSliderValue(sliderValue);
    m_cursorWorldPosition = addAxes(addAxes(sliceOrigin,
                                            scaleAxis(m_sliceGeometry.xAxis, xPosition)),
                                    scaleAxis(m_sliceGeometry.yAxis, yPosition));
}

void MprViewWidget::updateCrosshairGeometry()
{
    if (!m_hasImage || !m_crosshairActor->GetVisibility()) {
        return;
    }

    const Axis sliceOrigin = sliceOriginForSliderValue(m_slider->value());
    const double xMinimum = m_sliceGeometry.outputOrigin[0];
    const double xMaximum = maxOutputCoordinate(m_sliceGeometry.outputOrigin[0],
                                                m_sliceGeometry.xSpacing,
                                                m_sliceGeometry.outputExtent[0],
                                                m_sliceGeometry.outputExtent[1]);
    const double yMinimum = m_sliceGeometry.outputOrigin[1];
    const double yMaximum = maxOutputCoordinate(m_sliceGeometry.outputOrigin[1],
                                                m_sliceGeometry.ySpacing,
                                                m_sliceGeometry.outputExtent[2],
                                                m_sliceGeometry.outputExtent[3]);
    const double xPosition = std::clamp(dotProduct(subtractAxes(m_cursorWorldPosition, sliceOrigin), m_sliceGeometry.xAxis),
                                        xMinimum,
                                        xMaximum);
    const double yPosition = std::clamp(dotProduct(subtractAxes(m_cursorWorldPosition, sliceOrigin), m_sliceGeometry.yAxis),
                                        yMinimum,
                                        yMaximum);
    constexpr double overlayDepth = 0.1;

    m_crosshairPoints->SetPoint(0, xPosition, yMinimum, overlayDepth);
    m_crosshairPoints->SetPoint(1, xPosition, yMaximum, overlayDepth);
    m_crosshairPoints->SetPoint(2, xMinimum, yPosition, overlayDepth);
    m_crosshairPoints->SetPoint(3, xMaximum, yPosition, overlayDepth);
    m_crosshairPoints->Modified();
    m_crosshairPolyData->Modified();
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
                              .arg(orientationName(m_orientation))
                              .arg(clampedValue + 1)
                              .arg(m_sliceGeometry.sliceCount));
}

void MprViewWidget::updateWindowLevel(vtkImageData *imageData)
{
    if (m_currentWindow > 0.0 && std::isfinite(m_currentLevel)) {
        return;
    }

    if (m_hasRecommendedWindowLevel && m_recommendedWindow > 0.0) {
        m_currentWindow = m_recommendedWindow;
        m_currentLevel = m_recommendedLevel;
        return;
    }

    double scalarRange[2] { 0.0, 0.0 };
    imageData->GetScalarRange(scalarRange);
    m_currentWindow = std::max(1.0, scalarRange[1] - scalarRange[0]);
    m_currentLevel = 0.5 * (scalarRange[0] + scalarRange[1]);
}

void MprViewWidget::applyWindowLevelValues()
{
    const double clampedWindow = std::max(1.0, m_currentWindow);
    m_currentWindow = clampedWindow;
    m_windowLevel->SetWindow(clampedWindow);
    m_windowLevel->SetLevel(m_currentLevel);
    updateWindowLevelOverlay();
}

void MprViewWidget::updateWindowLevelOverlay()
{
    if (!m_hasImage || m_currentWindow <= 0.0) {
        m_windowLevelLabel->hide();
        return;
    }

    m_windowLevelLabel->setText(QStringLiteral("WW: %1  WL: %2")
                                    .arg(static_cast<int>(std::lround(m_currentWindow)))
                                    .arg(static_cast<int>(std::lround(m_currentLevel))));
    m_windowLevelLabel->adjustSize();
    updateOverlayPosition();
    m_windowLevelLabel->show();
    m_windowLevelLabel->raise();
}

void MprViewWidget::updateOverlayPosition()
{
    if (m_windowLevelLabel == nullptr || m_vtkWidget == nullptr) {
        return;
    }

    const int margin = 8;
    const QSize labelSize = m_windowLevelLabel->sizeHint();
    const int x = std::max(margin, m_vtkWidget->width() - labelSize.width() - margin);
    m_windowLevelLabel->move(x, margin);
}

void MprViewWidget::setWindowLevelInternal(double window, double level, bool emitSignal)
{
    if (!std::isfinite(window) || !std::isfinite(level)) {
        return;
    }

    m_currentWindow = std::max(1.0, window);
    m_currentLevel = level;
    applyWindowLevelValues();
    m_vtkWidget->renderWindow()->Render();

    if (emitSignal) {
        emit windowLevelChanged(m_currentWindow, m_currentLevel);
    }
}

std::array<double, 3> MprViewWidget::sliceOriginForSliderValue(int sliderValue) const
{
    if (m_sliceGeometry.sliceCount <= 0) {
        return m_sliceGeometry.center;
    }

    const int clampedValue = std::clamp(sliderValue, 0, m_sliceGeometry.sliceCount - 1);
    const int sliceIndex = m_sliceGeometry.reverseSlider
        ? (m_sliceGeometry.sliceCount - 1 - clampedValue)
        : clampedValue;
    const double sliceOffset = m_sliceGeometry.minSlice + sliceIndex * m_sliceGeometry.sliceSpacing;
    return addAxes(m_sliceGeometry.center,
                   scaleAxis(m_sliceGeometry.normalAxis, sliceOffset));
}

int MprViewWidget::sliderValueForWorldPosition(const Axis &worldPosition) const
{
    if (m_sliceGeometry.sliceCount <= 0 || m_sliceGeometry.sliceSpacing <= 0.0) {
        return 0;
    }

    const double projection = dotProduct(subtractAxes(worldPosition, m_sliceGeometry.center), m_sliceGeometry.normalAxis);
    const int sliceIndex = std::clamp(static_cast<int>(std::lround((projection - m_sliceGeometry.minSlice) / m_sliceGeometry.sliceSpacing)),
                                      0,
                                      std::max(0, m_sliceGeometry.sliceCount - 1));
    return m_sliceGeometry.reverseSlider
        ? (m_sliceGeometry.sliceCount - 1 - sliceIndex)
        : sliceIndex;
}

bool MprViewWidget::pickWorldPosition(const QPoint &widgetPosition, Axis *worldPosition) const
{
    if (worldPosition == nullptr || !m_hasImage || !m_imageActor->GetVisibility()) {
        return false;
    }

    const double devicePixelRatio = m_vtkWidget->devicePixelRatioF();
    const int displayX = static_cast<int>(std::lround(widgetPosition.x() * devicePixelRatio));
    const int displayY = static_cast<int>(std::lround((m_vtkWidget->height() - 1 - widgetPosition.y()) * devicePixelRatio));
    if (m_imagePicker->Pick(displayX, displayY, 0.0, m_renderer) == 0) {
        return false;
    }

    double pickedPosition[3] { 0.0, 0.0, 0.0 };
    m_imagePicker->GetPickPosition(pickedPosition);
    const Axis sliceOrigin = sliceOriginForSliderValue(m_slider->value());
    *worldPosition = addAxes(addAxes(sliceOrigin,
                                     scaleAxis(m_sliceGeometry.xAxis, pickedPosition[0])),
                             scaleAxis(m_sliceGeometry.yAxis, pickedPosition[1]));
    return true;
}


