#pragma once

#include <array>
#include <memory>

#include <QWidget>

#include <vtkSmartPointer.h>

class QEvent;
class QLabel;
class QObject;
class QPoint;
class QVTKOpenGLNativeWidget;
class ModelViewCameraController;
class ModelViewCrosshairController;
class vtkActor;
class vtkImageData;
class vtkPolyData;
class vtkRenderer;

// 只保留 Qt 外壳职责，把 3D 相机和十字线交互交给独立控制器。
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
    void setCursorWorldPositionInternal(const std::array<double, 3> &worldPosition, bool emitSignal);

    QLabel *m_statusLabel;
    QVTKOpenGLNativeWidget *m_vtkWidget;
    std::unique_ptr<ModelViewCameraController> m_cameraController;
    std::unique_ptr<ModelViewCrosshairController> m_crosshairController;
    vtkSmartPointer<vtkRenderer> m_renderer;
};
