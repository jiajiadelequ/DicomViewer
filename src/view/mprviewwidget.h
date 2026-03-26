#pragma once

#include <QWidget>

#include <vtkSmartPointer.h>

class QLabel;
class QSlider;
class QVTKOpenGLNativeWidget;
class vtkGenericOpenGLRenderWindow;
class vtkImageActor;
class vtkImageData;
class vtkImageMapToWindowLevelColors;
class vtkImageReslice;
class vtkRenderer;

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
    struct SliceGeometry
    {
        double xAxis[3] { 1.0, 0.0, 0.0 };
        double yAxis[3] { 0.0, 1.0, 0.0 };
        double normal[3] { 0.0, 0.0, 1.0 };
        double centerX = 0.0;
        double centerY = 0.0;
        double minPosition = 0.0;
        double maxPosition = 0.0;
        double step = 1.0;
    };

    void updateSliceControls();
    void updateSliceDisplay(int value);
    SliceGeometry buildSliceGeometry(vtkImageData *imageData) const;
    void updateWindowLevel(vtkImageData *imageData);

    Orientation m_orientation;
    QLabel *m_titleLabel;
    QLabel *m_sliceLabel;
    QSlider *m_slider;
    QVTKOpenGLNativeWidget *m_vtkWidget;
    vtkSmartPointer<vtkGenericOpenGLRenderWindow> m_renderWindow;
    vtkSmartPointer<vtkRenderer> m_renderer;
    vtkSmartPointer<vtkImageReslice> m_reslice;
    vtkSmartPointer<vtkImageMapToWindowLevelColors> m_windowLevel;
    vtkSmartPointer<vtkImageActor> m_imageActor;
    vtkSmartPointer<vtkImageData> m_imageData;
    SliceGeometry m_sliceGeometry;
    bool m_hasImage = false;
};
