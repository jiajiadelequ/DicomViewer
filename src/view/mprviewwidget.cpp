#include "mprviewwidget.h"

#include <QLabel>
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
#include <vtkMatrix3x3.h>
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
        return { Axis { 1.0, 0.0, 0.0 }, Axis { 0.0, 0.0, 1.0 }, true };
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
}

MprViewWidget::MprViewWidget(const QString &title, Orientation orientation, QWidget *parent)
    : QWidget(parent)
    , m_orientation(orientation)
    , m_titleLabel(new QLabel(title, this))
    , m_sliceLabel(new QLabel(QStringLiteral("未加载"), this))
    , m_slider(new QSlider(Qt::Horizontal, this))
    , m_vtkWidget(new QVTKOpenGLNativeWidget(this))
    , m_renderer(vtkSmartPointer<vtkRenderer>::New())
    , m_reslice(vtkSmartPointer<vtkImageReslice>::New())
    , m_windowLevel(vtkSmartPointer<vtkImageMapToWindowLevelColors>::New())
    , m_imageActor(vtkSmartPointer<vtkImageActor>::New())
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
    m_vtkWidget->interactor()->SetInteractorStyle(vtkSmartPointer<vtkInteractorStyleImage>::New());

    m_reslice->SetOutputDimensionality(2);
    m_reslice->SetInterpolationModeToLinear();
    m_windowLevel->SetInputConnection(m_reslice->GetOutputPort());
    m_windowLevel->SetOutputFormatToLuminance();
    m_imageActor->GetMapper()->SetInputConnection(m_windowLevel->GetOutputPort());
    m_imageActor->InterpolateOff();
    m_imageActor->SetVisibility(false);

    m_renderer->SetBackground(0.05, 0.05, 0.06);
    m_renderer->AddActor(m_imageActor);
    m_vtkWidget->renderWindow()->AddRenderer(m_renderer);
    resetCamera();

    layout->addWidget(m_titleLabel);
    layout->addWidget(m_vtkWidget, 1);
    layout->addWidget(m_sliceLabel);
    layout->addWidget(m_slider);

    connect(m_slider, &QSlider::valueChanged, this, &MprViewWidget::onSliceChanged);
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

    updateWindowLevel(m_imageData);
    m_imageActor->SetVisibility(true);
    updateSliceControls();
    resetCamera();
    m_vtkWidget->renderWindow()->Render();
}

void MprViewWidget::clearView(const QString &message)
{
    m_hasImage = false;
    m_sliceGeometry = SliceGeometry {};
    m_slider->blockSignals(true);
    m_slider->setEnabled(false);
    m_slider->setRange(0, 0);
    m_slider->setValue(0);
    m_slider->blockSignals(false);
    m_sliceLabel->setText(message);

    m_reslice->SetInputData(static_cast<vtkImageData *>(nullptr));
    m_imageActor->SetVisibility(false);
    m_vtkWidget->renderWindow()->Render();
}

void MprViewWidget::onSliceChanged(int value)
{
    if (!m_hasImage || m_sliceGeometry.sliceCount <= 0) {
        return;
    }

    applyCurrentSlice(value);
    m_vtkWidget->renderWindow()->Render();
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

    const double xSpacing = std::max(1e-3, spacingAlongAxis(imageData, m_sliceGeometry.xAxis));
    const double ySpacing = std::max(1e-3, spacingAlongAxis(imageData, m_sliceGeometry.yAxis));
    m_sliceGeometry.sliceSpacing = std::max(1e-3, spacingAlongAxis(imageData, m_sliceGeometry.normalAxis));
    m_sliceGeometry.minSlice = sliceRange.first;
    m_sliceGeometry.sliceCount = sampleCount(sliceRange.first, sliceRange.second, m_sliceGeometry.sliceSpacing);
    m_sliceGeometry.outputOrigin = { xRange.first, yRange.first, 0.0 };
    m_sliceGeometry.outputExtent = {
        0,
        sampleCount(xRange.first, xRange.second, xSpacing) - 1,
        0,
        sampleCount(yRange.first, yRange.second, ySpacing) - 1,
        0,
        0
    };

    m_reslice->SetResliceAxesDirectionCosines(m_sliceGeometry.xAxis.data(),
                                              m_sliceGeometry.yAxis.data(),
                                              m_sliceGeometry.normalAxis.data());
    m_reslice->SetOutputSpacing(xSpacing, ySpacing, 1.0);
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

    const int clampedValue = std::clamp(sliderValue, 0, m_sliceGeometry.sliceCount - 1);
    const int sliceIndex = m_sliceGeometry.reverseSlider
        ? (m_sliceGeometry.sliceCount - 1 - clampedValue)
        : clampedValue;
    const double sliceOffset = m_sliceGeometry.minSlice + sliceIndex * m_sliceGeometry.sliceSpacing;
    const Axis sliceOrigin = addAxes(m_sliceGeometry.center,
                                     scaleAxis(m_sliceGeometry.normalAxis, sliceOffset));

    m_reslice->SetResliceAxesOrigin(sliceOrigin[0], sliceOrigin[1], sliceOrigin[2]);
    updateSliceLabel(clampedValue);
}

void MprViewWidget::resetCamera()
{
    auto *camera = m_renderer->GetActiveCamera();
    camera->ParallelProjectionOn();
    camera->SetPosition(0.0, 0.0, 1.0);
    camera->SetFocalPoint(0.0, 0.0, 0.0);
    camera->SetViewUp(0.0, 1.0, 0.0);
    m_renderer->ResetCamera();
    m_renderer->ResetCameraClippingRange();
}

void MprViewWidget::updateSliceControls()
{
    const int currentValue = std::clamp(m_sliceGeometry.sliceCount / 2,
                                        0,
                                        std::max(0, m_sliceGeometry.sliceCount - 1));

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
    if (m_hasRecommendedWindowLevel) {
        m_windowLevel->SetWindow(m_recommendedWindow);
        m_windowLevel->SetLevel(m_recommendedLevel);
        return;
    }

    double scalarRange[2] { 0.0, 0.0 };
    imageData->GetScalarRange(scalarRange);
    const double window = std::max(1.0, scalarRange[1] - scalarRange[0]);
    const double level = 0.5 * (scalarRange[0] + scalarRange[1]);

    m_windowLevel->SetWindow(window);
    m_windowLevel->SetLevel(level);
}



