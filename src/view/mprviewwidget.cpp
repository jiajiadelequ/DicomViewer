#include "mprviewwidget.h"

#include <QLabel>
#include <QSlider>
#include <QVBoxLayout>

#include <QVTKOpenGLNativeWidget.h>

#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkImageData.h>
#include <vtkImageViewer2.h>
#include <vtkInteractorStyleImage.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>

#include <algorithm>

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
}

MprViewWidget::MprViewWidget(const QString &title, Orientation orientation, QWidget *parent)
    : QWidget(parent)
    , m_orientation(orientation)
    , m_titleLabel(new QLabel(title, this))
    , m_sliceLabel(new QLabel(QStringLiteral("未加载"), this))
    , m_slider(new QSlider(Qt::Horizontal, this))
    , m_vtkWidget(new QVTKOpenGLNativeWidget(this))
    , m_viewer(vtkSmartPointer<vtkImageViewer2>::New())
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
    m_viewer->SetRenderWindow(m_vtkWidget->renderWindow());
    m_viewer->SetupInteractor(m_vtkWidget->interactor());
    m_vtkWidget->interactor()->SetInteractorStyle(vtkSmartPointer<vtkInteractorStyleImage>::New());
    configureViewerOrientation();
    m_viewer->GetRenderer()->SetBackground(0.05, 0.05, 0.06);

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

    m_imageData = imageData;
    m_hasImage = true;

    m_viewer->SetInputData(m_imageData);
    configureViewerOrientation();
    updateWindowLevel(m_imageData);
    updateSliceControls();
    m_viewer->GetRenderer()->ResetCamera();
    m_viewer->Render();
}

void MprViewWidget::clearView(const QString &message)
{
    m_hasImage = false;
    m_slider->blockSignals(true);
    m_slider->setEnabled(false);
    m_slider->setRange(0, 0);
    m_slider->setValue(0);
    m_slider->blockSignals(false);
    m_sliceLabel->setText(message);

    m_viewer->SetInputData(nullptr);
    m_vtkWidget->renderWindow()->Render();
}

void MprViewWidget::onSliceChanged(int value)
{
    if (!m_hasImage) {
        return;
    }

    const int sliceMin = m_viewer->GetSliceMin();
    const int sliceMax = m_viewer->GetSliceMax();
    const int slice = std::clamp(sliceMin + value, sliceMin, sliceMax);
    m_viewer->SetSlice(slice);
    updateSliceLabel(slice, sliceMin, sliceMax);
    m_viewer->Render();
}

void MprViewWidget::configureViewerOrientation()
{
    switch (m_orientation) {
    case Orientation::Axial:
        m_viewer->SetSliceOrientationToXY();
        break;
    case Orientation::Coronal:
        m_viewer->SetSliceOrientationToXZ();
        break;
    case Orientation::Sagittal:
        m_viewer->SetSliceOrientationToYZ();
        break;
    }
}

void MprViewWidget::updateSliceControls()
{
    const int sliceMin = m_viewer->GetSliceMin();
    const int sliceMax = m_viewer->GetSliceMax();
    const int currentSlice = std::clamp((sliceMin + sliceMax) / 2, sliceMin, sliceMax);

    m_slider->blockSignals(true);
    m_slider->setEnabled(sliceMax >= sliceMin);
    m_slider->setRange(0, std::max(0, sliceMax - sliceMin));
    m_slider->setValue(currentSlice - sliceMin);
    m_slider->blockSignals(false);

    m_viewer->SetSlice(currentSlice);
    updateSliceLabel(currentSlice, sliceMin, sliceMax);
}

void MprViewWidget::updateSliceLabel(int slice, int sliceMin, int sliceMax)
{
    m_sliceLabel->setText(QStringLiteral("%1 Slice: %2 / %3")
                              .arg(orientationName(m_orientation))
                              .arg(slice - sliceMin + 1)
                              .arg(sliceMax - sliceMin + 1));
}

void MprViewWidget::updateWindowLevel(vtkImageData *imageData)
{
    double scalarRange[2] { 0.0, 0.0 };
    imageData->GetScalarRange(scalarRange);
    const double window = std::max(1.0, scalarRange[1] - scalarRange[0]);
    const double level = 0.5 * (scalarRange[0] + scalarRange[1]);

    m_viewer->SetColorWindow(window);
    m_viewer->SetColorLevel(level);
}
