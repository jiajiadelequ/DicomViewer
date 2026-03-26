#pragma once

#include <QWidget>

#include <vtkSmartPointer.h>

class QLabel;
class QVTKOpenGLNativeWidget;
class vtkGenericOpenGLRenderWindow;
class vtkRenderer;

class ModelViewWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit ModelViewWidget(QWidget *parent = nullptr);

    void clearScene(const QString &message);
    void addModelFile(const QString &filePath);

private:
    QLabel *m_statusLabel;
    QVTKOpenGLNativeWidget *m_vtkWidget;
    vtkSmartPointer<vtkRenderer> m_renderer;
};




