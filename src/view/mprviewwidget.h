#pragma once

#include <array>
#include <QWidget>

#include <vtkSmartPointer.h>

class QLabel;
class QResizeEvent;
class QSlider;
class QVTKOpenGLNativeWidget;
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

    void setRecommendedWindowLevel(double window, double level);
    void clearRecommendedWindowLevel();
    void setImageData(vtkImageData *imageData);
    void clearView(const QString &message);

protected:
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void onSliceChanged(int value);

private:
    struct SliceGeometry
    {
        std::array<double, 3> center { 0.0, 0.0, 0.0 };
        std::array<double, 3> xAxis { 1.0, 0.0, 0.0 };
        std::array<double, 3> yAxis { 0.0, 1.0, 0.0 };
        std::array<double, 3> normalAxis { 0.0, 0.0, 1.0 };
        std::array<double, 3> outputOrigin { 0.0, 0.0, 0.0 };
        std::array<int, 6> outputExtent { 0, -1, 0, -1, 0, 0 };
        double sliceSpacing = 1.0;
        double minSlice = 0.0;
        int sliceCount = 0;
        bool reverseSlider = false;
    };

    void configureSliceGeometry(vtkImageData *imageData);
    void applyCurrentSlice(int sliderValue);
    void fitImageToViewport();
    void resetCamera();
    void updateSliceControls();
    void updateSliceLabel(int sliderValue);
    void updateWindowLevel(vtkImageData *imageData);

    Orientation m_orientation;
    QLabel *m_titleLabel;
    QLabel *m_sliceLabel;
    QSlider *m_slider;
    QVTKOpenGLNativeWidget *m_vtkWidget;
    vtkSmartPointer<vtkRenderer> m_renderer;
    vtkSmartPointer<vtkImageReslice> m_reslice;
    vtkSmartPointer<vtkImageMapToWindowLevelColors> m_windowLevel;
    vtkSmartPointer<vtkImageActor> m_imageActor;
    vtkSmartPointer<vtkImageData> m_imageData;
    SliceGeometry m_sliceGeometry;
    double m_recommendedWindow = 0.0;
    double m_recommendedLevel = 0.0;
    bool m_hasRecommendedWindowLevel = false;
    bool m_hasImage = false;
};
