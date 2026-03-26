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
#include <vtkImageReslice.h>
#include <vtkInteractorStyleImage.h>
#include <vtkMath.h>
#include <vtkMatrix3x3.h>
#include <vtkMatrix4x4.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace
{
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

double dotProduct(const double lhs[3], const double rhs[3])
{
    return lhs[0] * rhs[0] + lhs[1] * rhs[1] + lhs[2] * rhs[2];
}

void copyVector(const double source[3], double target[3])
{
    target[0] = source[0];
    target[1] = source[1];
    target[2] = source[2];
}

std::array<double, 3> multiplyDirectionVector(vtkImageData *imageData, const double ijkVector[3])
{
    auto *directionMatrix = imageData->GetDirectionMatrix();
    std::array<double, 3> worldVector {
        directionMatrix->GetElement(0, 0) * ijkVector[0] + directionMatrix->GetElement(0, 1) * ijkVector[1] + directionMatrix->GetElement(0, 2) * ijkVector[2],
        directionMatrix->GetElement(1, 0) * ijkVector[0] + directionMatrix->GetElement(1, 1) * ijkVector[1] + directionMatrix->GetElement(1, 2) * ijkVector[2],
        directionMatrix->GetElement(2, 0) * ijkVector[0] + directionMatrix->GetElement(2, 1) * ijkVector[1] + directionMatrix->GetElement(2, 2) * ijkVector[2]
    };
    return worldVector;
}

std::array<double, 3> basisToWorld(vtkImageData *imageData, int axisIndex)
{
    double ijkBasis[3] { 0.0, 0.0, 0.0 };
    ijkBasis[axisIndex] = 1.0;
    return multiplyDirectionVector(imageData, ijkBasis);
}

double projectedSpacing(vtkImageData *imageData, const double axis[3])
{
    double spacing[3] { 1.0, 1.0, 1.0 };
    imageData->GetSpacing(spacing);

    double minimumSpacing = std::numeric_limits<double>::max();
    for (int i = 0; i < 3; ++i) {
        const auto basis = basisToWorld(imageData, i);
        const double scaledBasis[3] {
            basis[0] * spacing[i],
            basis[1] * spacing[i],
            basis[2] * spacing[i]
        };
        const double contribution = std::abs(dotProduct(scaledBasis, axis));
        if (contribution > 1e-6) {
            minimumSpacing = std::min(minimumSpacing, contribution);
        }
    }

    return minimumSpacing == std::numeric_limits<double>::max() ? 1.0 : minimumSpacing;
}

std::array<double, 3> worldPointForExtent(vtkImageData *imageData, int i, int j, int k)
{
    int extent[6] { 0, -1, 0, -1, 0, -1 };
    double origin[3] { 0.0, 0.0, 0.0 };
    double spacing[3] { 1.0, 1.0, 1.0 };
    imageData->GetExtent(extent);
    imageData->GetOrigin(origin);
    imageData->GetSpacing(spacing);

    const double ijkOffset[3] {
        (i - extent[0]) * spacing[0],
        (j - extent[2]) * spacing[1],
        (k - extent[4]) * spacing[2]
    };
    const auto rotatedOffset = multiplyDirectionVector(imageData, ijkOffset);

    std::array<double, 3> worldPoint {
        origin[0] + rotatedOffset[0],
        origin[1] + rotatedOffset[1],
        origin[2] + rotatedOffset[2]
    };
    return worldPoint;
}
}

MprViewWidget::MprViewWidget(const QString &title, Orientation orientation, QWidget *parent)
    : QWidget(parent)
    , m_orientation(orientation)
    , m_titleLabel(new QLabel(title, this))
    , m_sliceLabel(new QLabel(QStringLiteral("未加载"), this))
    , m_slider(new QSlider(Qt::Horizontal, this))
    , m_vtkWidget(new QVTKOpenGLNativeWidget(this))
    , m_renderWindow(vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New())
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

    m_vtkWidget->setRenderWindow(m_renderWindow);
    m_renderWindow->AddRenderer(m_renderer);
    m_vtkWidget->interactor()->SetInteractorStyle(vtkSmartPointer<vtkInteractorStyleImage>::New());

    m_renderer->SetBackground(0.05, 0.05, 0.06);
    m_renderer->AddActor(m_imageActor);

    m_reslice->SetOutputDimensionality(2);
    m_reslice->SetInterpolationModeToLinear();
    m_reslice->AutoCropOutputOn();

    m_windowLevel->SetOutputFormatToLuminance();
    m_windowLevel->PassAlphaToOutputOn();

    layout->addWidget(m_titleLabel);
    layout->addWidget(m_vtkWidget, 1);
    layout->addWidget(m_sliceLabel);
    layout->addWidget(m_slider);

    connect(m_slider, &QSlider::valueChanged, this, &MprViewWidget::onSliceChanged);
}

void MprViewWidget::setImageData(vtkImageData *imageData)
{
    if (imageData == nullptr) {
        clearView(QStringLiteral("未加载"));
        return;
    }

    m_imageData->DeepCopy(imageData);
    m_hasImage = true;
    m_sliceGeometry = buildSliceGeometry(m_imageData);

    m_reslice->SetInputData(m_imageData);
    updateWindowLevel(m_imageData);
    updateSliceControls();

    auto *camera = m_renderer->GetActiveCamera();
    camera->ParallelProjectionOn();
    m_renderer->ResetCamera();
    m_vtkWidget->renderWindow()->Render();
}

void MprViewWidget::clearView(const QString &message)
{
    m_hasImage = false;
    m_slider->setEnabled(false);
    m_slider->setRange(0, 0);
    m_sliceLabel->setText(message);

    m_reslice->SetInputData(nullptr);
    m_windowLevel->SetInputData(nullptr);
    m_imageActor->SetInputData(nullptr);
    m_renderer->ResetCamera();
    m_vtkWidget->renderWindow()->Render();
}

void MprViewWidget::onSliceChanged(int value)
{
    if (!m_hasImage) {
        return;
    }

    updateSliceDisplay(value);
}

MprViewWidget::SliceGeometry MprViewWidget::buildSliceGeometry(vtkImageData *imageData) const
{
    SliceGeometry geometry;

    switch (m_orientation) {
    case Orientation::Axial:
        geometry.xAxis[0] = 1.0;
        geometry.xAxis[1] = 0.0;
        geometry.xAxis[2] = 0.0;
        geometry.yAxis[0] = 0.0;
        geometry.yAxis[1] = 1.0;
        geometry.yAxis[2] = 0.0;
        geometry.normal[0] = 0.0;
        geometry.normal[1] = 0.0;
        geometry.normal[2] = 1.0;
        break;
    case Orientation::Coronal:
        geometry.xAxis[0] = 1.0;
        geometry.xAxis[1] = 0.0;
        geometry.xAxis[2] = 0.0;
        geometry.yAxis[0] = 0.0;
        geometry.yAxis[1] = 0.0;
        geometry.yAxis[2] = 1.0;
        geometry.normal[0] = 0.0;
        geometry.normal[1] = 1.0;
        geometry.normal[2] = 0.0;
        break;
    case Orientation::Sagittal:
        geometry.xAxis[0] = 0.0;
        geometry.xAxis[1] = 1.0;
        geometry.xAxis[2] = 0.0;
        geometry.yAxis[0] = 0.0;
        geometry.yAxis[1] = 0.0;
        geometry.yAxis[2] = 1.0;
        geometry.normal[0] = 1.0;
        geometry.normal[1] = 0.0;
        geometry.normal[2] = 0.0;
        break;
    }

    double center[3] { 0.0, 0.0, 0.0 };
    imageData->GetCenter(center);
    geometry.centerX = dotProduct(center, geometry.xAxis);
    geometry.centerY = dotProduct(center, geometry.yAxis);

    int extent[6] { 0, -1, 0, -1, 0, -1 };
    imageData->GetExtent(extent);

    geometry.minPosition = std::numeric_limits<double>::max();
    geometry.maxPosition = std::numeric_limits<double>::lowest();

    for (int iIndex = 0; iIndex < 2; ++iIndex) {
        for (int jIndex = 0; jIndex < 2; ++jIndex) {
            for (int kIndex = 0; kIndex < 2; ++kIndex) {
                const auto corner = worldPointForExtent(imageData,
                                                        iIndex == 0 ? extent[0] : extent[1],
                                                        jIndex == 0 ? extent[2] : extent[3],
                                                        kIndex == 0 ? extent[4] : extent[5]);
                const double position = corner[0] * geometry.normal[0] + corner[1] * geometry.normal[1]
                    + corner[2] * geometry.normal[2];
                geometry.minPosition = std::min(geometry.minPosition, position);
                geometry.maxPosition = std::max(geometry.maxPosition, position);
            }
        }
    }

    geometry.step = projectedSpacing(imageData, geometry.normal);
    if (geometry.step <= 1e-6) {
        geometry.step = 1.0;
    }

    return geometry;
}

void MprViewWidget::updateWindowLevel(vtkImageData *imageData)
{
    double scalarRange[2] { 0.0, 0.0 };
    imageData->GetScalarRange(scalarRange);
    const double window = std::max(1.0, scalarRange[1] - scalarRange[0]);
    const double level = 0.5 * (scalarRange[0] + scalarRange[1]);

    m_windowLevel->SetWindow(window);
    m_windowLevel->SetLevel(level);
}

void MprViewWidget::updateSliceControls()
{
    const int sliceCount = std::max(1, static_cast<int>(std::llround((m_sliceGeometry.maxPosition - m_sliceGeometry.minPosition)
                                                                      / m_sliceGeometry.step))
                                           + 1);
    const int currentSlice = sliceCount / 2;

    m_slider->blockSignals(true);
    m_slider->setEnabled(true);
    m_slider->setRange(0, sliceCount - 1);
    m_slider->setValue(currentSlice);
    m_slider->blockSignals(false);

    updateSliceDisplay(currentSlice);
}

void MprViewWidget::updateSliceDisplay(int value)
{
    const double slicePosition = m_sliceGeometry.minPosition + static_cast<double>(value) * m_sliceGeometry.step;

    vtkNew<vtkMatrix4x4> resliceAxes;
    resliceAxes->Identity();
    for (int row = 0; row < 3; ++row) {
        resliceAxes->SetElement(row, 0, m_sliceGeometry.xAxis[row]);
        resliceAxes->SetElement(row, 1, m_sliceGeometry.yAxis[row]);
        resliceAxes->SetElement(row, 2, m_sliceGeometry.normal[row]);
    }

    const double origin[3] {
        m_sliceGeometry.xAxis[0] * m_sliceGeometry.centerX + m_sliceGeometry.yAxis[0] * m_sliceGeometry.centerY
            + m_sliceGeometry.normal[0] * slicePosition,
        m_sliceGeometry.xAxis[1] * m_sliceGeometry.centerX + m_sliceGeometry.yAxis[1] * m_sliceGeometry.centerY
            + m_sliceGeometry.normal[1] * slicePosition,
        m_sliceGeometry.xAxis[2] * m_sliceGeometry.centerX + m_sliceGeometry.yAxis[2] * m_sliceGeometry.centerY
            + m_sliceGeometry.normal[2] * slicePosition
    };
    resliceAxes->SetElement(0, 3, origin[0]);
    resliceAxes->SetElement(1, 3, origin[1]);
    resliceAxes->SetElement(2, 3, origin[2]);

    const double outputSpacingX = projectedSpacing(m_imageData, m_sliceGeometry.xAxis);
    const double outputSpacingY = projectedSpacing(m_imageData, m_sliceGeometry.yAxis);

    m_reslice->SetResliceAxes(resliceAxes);
    m_reslice->SetOutputSpacing(outputSpacingX, outputSpacingY, 1.0);
    m_reslice->Update();

    m_windowLevel->SetInputConnection(m_reslice->GetOutputPort());
    m_windowLevel->Update();

    m_imageActor->SetInputData(m_windowLevel->GetOutput());
    m_renderer->ResetCameraClippingRange();
    m_sliceLabel->setText(QStringLiteral("%1 Slice: %2 / %3")
                              .arg(orientationName(m_orientation))
                              .arg(value + 1)
                              .arg(m_slider->maximum() + 1));
    m_vtkWidget->renderWindow()->Render();
}
