#pragma once

#include <array>
#include <QPoint>
#include <QWidget>

#include <vtkSmartPointer.h>

class QLabel;
class QObject;
class QEvent;
class QResizeEvent;
class QSlider;
class QVTKOpenGLNativeWidget;
class vtkActor;
class vtkCellPicker;
class vtkImageActor;
class vtkImageData;
class vtkImageMapToWindowLevelColors;
class vtkImageReslice;
class vtkPoints;
class vtkPolyData;
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
    void setCrosshairEnabled(bool enabled);
    void setCursorWorldPosition(double x, double y, double z);
    void setWindowLevel(double window, double level);
    [[nodiscard]] std::array<double, 3> cursorWorldPosition() const;
    void refreshView();
    void clearView(const QString &message);

signals:
    void cursorWorldPositionChanged(double x, double y, double z);
    void windowLevelChanged(double window, double level);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
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
        double xSpacing = 1.0;
        double ySpacing = 1.0;
        double sliceSpacing = 1.0;
        double minSlice = 0.0;
        int sliceCount = 0;
        bool reverseSlider = false;
    };

    void configureSliceGeometry(vtkImageData *imageData);
    void applyCurrentSlice(int sliderValue);
    void fitImageToViewport();
    void resetCamera();
    void renderCurrentState(bool fitViewport);
    void setCursorWorldPositionInternal(const std::array<double, 3> &worldPosition, bool emitSignal, bool fitViewport);
    void updateCursorWorldPositionFromSlider(int sliderValue);
    void updateCrosshairGeometry();
    void updateSliceControls();
    void updateSliceLabel(int sliderValue);
    void updateWindowLevel(vtkImageData *imageData);
    void applyWindowLevelValues();
    void updateWindowLevelOverlay();
    void updateOverlayPosition();
    void setWindowLevelInternal(double window, double level, bool emitSignal);
    [[nodiscard]] std::array<double, 3> sliceOriginForSliderValue(int sliderValue) const;
    [[nodiscard]] int sliderValueForWorldPosition(const std::array<double, 3> &worldPosition) const;
    [[nodiscard]] bool pickWorldPosition(const QPoint &widgetPosition, std::array<double, 3> *worldPosition) const;

    Orientation m_orientation;
    QLabel *m_titleLabel;
    QLabel *m_windowLevelLabel;
    QLabel *m_sliceLabel;
    QSlider *m_slider;
    QVTKOpenGLNativeWidget *m_vtkWidget;
    vtkSmartPointer<vtkRenderer> m_renderer;
    vtkSmartPointer<vtkImageReslice> m_reslice;
    vtkSmartPointer<vtkImageMapToWindowLevelColors> m_windowLevel;
    vtkSmartPointer<vtkImageActor> m_imageActor;
    vtkSmartPointer<vtkActor> m_crosshairActor;
    vtkSmartPointer<vtkPolyData> m_crosshairPolyData;
    vtkSmartPointer<vtkPoints> m_crosshairPoints;
    vtkSmartPointer<vtkCellPicker> m_imagePicker;
    vtkSmartPointer<vtkImageData> m_imageData;
    SliceGeometry m_sliceGeometry;
    std::array<double, 3> m_cursorWorldPosition { 0.0, 0.0, 0.0 };
    QPoint m_windowLevelDragStartPosition;
    double m_recommendedWindow = 0.0;
    double m_recommendedLevel = 0.0;
    double m_currentWindow = 0.0;
    double m_currentLevel = 0.0;
    double m_windowLevelDragStartWindow = 0.0;
    double m_windowLevelDragStartLevel = 0.0;
    bool m_hasRecommendedWindowLevel = false;
    bool m_hasImage = false;
    bool m_crosshairEnabled = false;
    bool m_crosshairDragActive = false;
    bool m_windowLevelDragActive = false;
};
