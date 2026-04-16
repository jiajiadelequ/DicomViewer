#pragma once

#include "src/core/runtime/studyloadresult.h"

#include <array>
#include <memory>
#include <vector>

#include <QWidget>

#include <vtkSmartPointer.h>

class QEvent;
class QLabel;
class QObject;
class QPoint;
class QVTKOpenGLNativeWidget;
class QToolButton;
class ModelViewCameraController;
class ModelViewCrosshairController;
class vtkActor;
class vtkBoxWidget2;
class vtkCallbackCommand;
class vtkPlanes;
class vtkImageData;
class vtkObject;
class vtkPolyDataMapper;
class vtkPolyData;
class vtkRenderer;

// 只保留 Qt 外壳职责，把 3D 相机和十字线交互交给独立控制器。
class ModelViewWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit ModelViewWidget(QWidget *parent = nullptr);

    void beginSceneBatch(const QString &message);
    void endSceneBatch(const QString &message);
    void clearScene(const QString &message);
    void addModelData(const QString &filePath, vtkPolyData *polyData, const LoadedModelData::MaterialData &material);
    void setModelVisibility(int index, bool visible);
    void setReferenceImageData(vtkImageData *imageData);
    void setCrosshairEnabled(bool enabled);
    void setClippingEnabled(bool enabled);
    void setCursorWorldPosition(double x, double y, double z);
    [[nodiscard]] std::array<double, 3> cursorWorldPosition() const;
    [[nodiscard]] bool hasModels() const;

signals:
    void cursorWorldPositionChanged(double x, double y, double z);
    void maximizeToggled();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct ModelEntry
    {
        vtkSmartPointer<vtkActor> actor;
        vtkSmartPointer<vtkPolyDataMapper> mapper;
        vtkSmartPointer<vtkPolyData> originalPolyData;
    };

    void initializeClippingWidget();
    void teardownClippingWidget();
    void updateClippedModels();
    static void handleBoxWidgetInteractionEnd(vtkObject *caller, unsigned long eventId, void *clientData, void *callData);
    static void handleBoxWidgetInteraction(vtkObject *caller, unsigned long eventId, void *clientData, void *callData);

    void setCursorWorldPositionInternal(const std::array<double, 3> &worldPosition, bool emitSignal);
    void queueSceneUpdate(bool resetCamera);
    void flushQueuedSceneUpdate();

    QLabel *m_statusLabel;
    QToolButton *m_maximizeButton;
    QVTKOpenGLNativeWidget *m_vtkWidget;
    std::unique_ptr<ModelViewCameraController> m_cameraController;
    std::unique_ptr<ModelViewCrosshairController> m_crosshairController;
    vtkSmartPointer<vtkRenderer> m_renderer;
    vtkSmartPointer<vtkBoxWidget2> m_clippingWidget;
    vtkSmartPointer<vtkCallbackCommand> m_clippingCallback;
    std::vector<ModelEntry> m_models;
    bool m_clippingEnabled = false;
    bool m_clippingPreviewDirty = false;
    bool m_sceneBatchActive = false;
    bool m_sceneNeedsRender = false;
    bool m_sceneNeedsCameraReset = false;

public:
    void setMaximizedState(bool maximized);
};
