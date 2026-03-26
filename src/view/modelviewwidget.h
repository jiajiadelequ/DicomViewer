#pragma once

#include <QWidget>

#include <vtkSmartPointer.h>

class QLabel;
class QVTKOpenGLNativeWidget;
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
    QLabel *m_statusLabel;
    QVTKOpenGLNativeWidget *m_vtkWidget;
    vtkSmartPointer<vtkRenderer> m_renderer;
};
