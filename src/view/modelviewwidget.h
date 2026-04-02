#pragma once

#include <QWidget>

#include <vtkSmartPointer.h>

class QLabel;
class QToolButton;
class QVTKOpenGLNativeWidget;
class vtkAxesActor;
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

    QToolButton *m_viewButton;
    QLabel *m_statusLabel;
    QVTKOpenGLNativeWidget *m_vtkWidget;
    vtkSmartPointer<vtkRenderer> m_renderer;
    vtkSmartPointer<vtkAxesActor> m_orientationAxes;
    vtkSmartPointer<vtkOrientationMarkerWidget> m_orientationMarkerWidget;
    StandardView m_currentStandardView = StandardView::Coronal;
};
