#pragma once

#include <array>
#include <vector>

#include <QWidget>

#include <vtkSmartPointer.h>

class QEvent;
class QLabel;
class QObject;
class QPoint;
class QToolButton;
class QVTKOpenGLNativeWidget;
class vtkAxesActor;
class vtkActor;
class vtkCellPicker;
class vtkCursor3D;
class vtkImageData;
class vtkOrientationMarkerWidget;
class vtkPolyData;
class vtkRenderer;

class ModelViewWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit ModelViewWidget(QWidget *parent = nullptr);

    void clearScene(const QString &message);
    void addModelData(const QString &filePath, vtkPolyData *polyData);
    void setReferenceImageData(vtkImageData *imageData);
    void setCrosshairEnabled(bool enabled);
    void setCursorWorldPosition(double x, double y, double z);
    [[nodiscard]] std::array<double, 3> cursorWorldPosition() const;

signals:
    void cursorWorldPositionChanged(double x, double y, double z);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    enum class StandardView
    {
        Coronal,
        Sagittal,
        Axial
    };

    void updateViewButtonText();
    void resetCameraToAnatomicalView();
    void applyStandardView(StandardView view);
    void setCursorWorldPositionInternal(const std::array<double, 3> &worldPosition, bool emitSignal);
    void updateCrosshairGeometry();
    [[nodiscard]] bool pickWorldPosition(const QPoint &widgetPosition, std::array<double, 3> *worldPosition) const;
    [[nodiscard]] bool crosshairBounds(double bounds[6]) const;
    [[nodiscard]] bool cameraBounds(double bounds[6]) const;

    QToolButton *m_viewButton;
    QLabel *m_statusLabel;
    QVTKOpenGLNativeWidget *m_vtkWidget;
    vtkSmartPointer<vtkRenderer> m_renderer;
    vtkSmartPointer<vtkActor> m_crosshairActor;
    vtkSmartPointer<vtkCursor3D> m_crosshairCursor;
    vtkSmartPointer<vtkCellPicker> m_modelPicker;
    vtkSmartPointer<vtkAxesActor> m_orientationAxes;
    vtkSmartPointer<vtkOrientationMarkerWidget> m_orientationMarkerWidget;
    std::vector<vtkSmartPointer<vtkActor>> m_modelActors;
    std::array<double, 3> m_cursorWorldPosition { 0.0, 0.0, 0.0 };
    std::array<double, 6> m_referenceBounds { 0.0, -1.0, 0.0, -1.0, 0.0, -1.0 };
    std::array<double, 6> m_modelBounds { 0.0, -1.0, 0.0, -1.0, 0.0, -1.0 };
    bool m_hasReferenceBounds = false;
    bool m_hasModelBounds = false;
    bool m_crosshairEnabled = false;
    bool m_crosshairDragActive = false;
    StandardView m_currentStandardView = StandardView::Coronal;
};
