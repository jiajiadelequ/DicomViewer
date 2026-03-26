#pragma once

#include <QWidget>

#include <vtkSmartPointer.h>

class QLabel;
class QSlider;
class QVTKOpenGLNativeWidget;
class vtkImageData;
class vtkImageViewer2;

class MprViewWidget final : public QWidget
{
    Q_OBJECT

public:
    enum class Orientation
    {
        Axial,
        Coronal,
        Sagittal
    };

    explicit MprViewWidget(const QString &title, Orientation orientation, QWidget *parent = nullptr);

    void setImageData(vtkImageData *imageData);
    void clearView(const QString &message);

private slots:
    void onSliceChanged(int value);

private:
    void configureViewerOrientation();
    void updateSliceControls();
    void updateSliceLabel(int slice, int sliceMin, int sliceMax);
    void updateWindowLevel(vtkImageData *imageData);

    Orientation m_orientation;
    QLabel *m_titleLabel;
    QLabel *m_sliceLabel;
    QSlider *m_slider;
    QVTKOpenGLNativeWidget *m_vtkWidget;
    vtkSmartPointer<vtkImageViewer2> m_viewer;
    vtkSmartPointer<vtkImageData> m_imageData;
    bool m_hasImage = false;
};
