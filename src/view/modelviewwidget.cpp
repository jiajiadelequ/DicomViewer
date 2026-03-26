#include "modelviewwidget.h"

#include <QFileInfo>
#include <QLabel>
#include <QVBoxLayout>

#include <QVTKOpenGLNativeWidget.h>

#include <vtkActor.h>
#include <vtkAlgorithmOutput.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkNew.h>
#include <vtkOBJReader.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderer.h>
#include <vtkSTLReader.h>

ModelViewWidget::ModelViewWidget(QWidget *parent)
    : QWidget(parent)
    , m_statusLabel(new QLabel(QStringLiteral("等待加载模型"), this))
    , m_vtkWidget(new QVTKOpenGLNativeWidget(this))
    , m_renderWindow(vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New())
    , m_renderer(vtkSmartPointer<vtkRenderer>::New())
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    m_statusLabel->setStyleSheet(QStringLiteral("color: #6b7280;"));

    m_vtkWidget->setRenderWindow(m_renderWindow);
    m_renderWindow->AddRenderer(m_renderer);
    m_renderer->SetBackground(0.95, 0.95, 0.97);

    layout->addWidget(m_vtkWidget, 1);
    layout->addWidget(m_statusLabel);
}

void ModelViewWidget::clearScene(const QString &message)
{
    m_renderer->RemoveAllViewProps();
    m_statusLabel->setText(message);
    m_renderWindow->Render();
}

void ModelViewWidget::addModelFile(const QString &filePath)
{
    const QFileInfo info(filePath);
    const QString suffix = info.suffix().toLower();

    vtkAlgorithmOutput *outputPort = nullptr;
    vtkSmartPointer<vtkSTLReader> stlReader;
    vtkSmartPointer<vtkOBJReader> objReader;

    if (suffix == QStringLiteral("stl")) {
        stlReader = vtkSmartPointer<vtkSTLReader>::New();
        stlReader->SetFileName(filePath.toStdString().c_str());
        stlReader->Update();
        outputPort = stlReader->GetOutputPort();
    } else if (suffix == QStringLiteral("obj")) {
        objReader = vtkSmartPointer<vtkOBJReader>::New();
        objReader->SetFileName(filePath.toStdString().c_str());
        objReader->Update();
        outputPort = objReader->GetOutputPort();
    } else {
        return;
    }

    vtkNew<vtkPolyDataMapper> mapper;
    mapper->SetInputConnection(outputPort);

    vtkNew<vtkActor> actor;
    actor->SetMapper(mapper);
    actor->GetProperty()->SetOpacity(1.0);

    m_renderer->AddActor(actor);
    m_renderer->ResetCamera();
    m_statusLabel->setText(QStringLiteral("已加载模型: %1").arg(info.fileName()));
    m_renderWindow->Render();
}
