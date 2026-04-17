#pragma once

#include <functional>

#include <QString>

#include <vtkSmartPointer.h>

class QEvent;
class QPoint;
class QVTKOpenGLNativeWidget;
class vtkBoxWidget2;
class vtkCallbackCommand;
class vtkObject;
class vtkPlanes;
class vtkRenderer;

class ModelClippingController final
{
public:
    using BoundsProvider = std::function<bool(double bounds[6])>;
    using StatusHandler = std::function<void(const QString &)>;
    using CommitHandler = std::function<void()>;

    ModelClippingController(QVTKOpenGLNativeWidget *viewport, vtkRenderer *renderer);

    void setStatusHandler(StatusHandler handler);
    void setCommitHandler(CommitHandler handler);

    void activate(const BoundsProvider &boundsProvider);
    void deactivate();
    [[nodiscard]] bool isEnabled() const;
    [[nodiscard]] bool handleViewportEvent(QEvent *event);
    [[nodiscard]] bool copyPlanesTo(vtkPlanes *planes) const;

private:
    static void handleWidgetInteraction(vtkObject *caller, unsigned long eventId, void *clientData, void *callData);

    void resetInteractionState();
    void updateStatus(const QString &message) const;
    void commitPreview();
    void refreshHoverState(const QPoint &position);
    [[nodiscard]] int resolvedInteractionState(const QPoint &position) const;

    QVTKOpenGLNativeWidget *m_viewport;
    vtkRenderer *m_renderer;
    vtkSmartPointer<vtkBoxWidget2> m_widget;
    vtkSmartPointer<vtkCallbackCommand> m_callback;
    StatusHandler m_statusHandler;
    CommitHandler m_commitHandler;
    bool m_enabled = false;
    bool m_previewDirty = false;
    bool m_manualFaceDragActive = false;
    int m_manualFaceInteractionState = 0;
    int m_hoveredInteractionState = 0;
};
