#include "modelviewwidget.h"

#include <QFileInfo>
#include <QLabel>
#include <QVBoxLayout>

#include <QVTKOpenGLNativeWidget.h>

#include <vtkActor.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkNew.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderer.h>

ModelViewWidget::ModelViewWidget(QWidget *parent)
    : QWidget(parent)
    , m_statusLabel(new QLabel(QStringLiteral("等待加载模型"), this))
    , m_vtkWidget(new QVTKOpenGLNativeWidget(this))
    , m_renderer(vtkSmartPointer<vtkRenderer>::New())
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    m_statusLabel->setStyleSheet(QStringLiteral("color: #6b7280;"));

    auto renderWindow = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
    m_vtkWidget->setRenderWindow(renderWindow);
    renderWindow->AddRenderer(m_renderer);
    m_renderer->SetBackground(0.95, 0.95, 0.97);

    layout->addWidget(m_vtkWidget, 1);
    layout->addWidget(m_statusLabel);
}

void ModelViewWidget::clearScene(const QString &message)
{
    m_renderer->RemoveAllViewProps();
    m_statusLabel->setText(message);
    m_vtkWidget->renderWindow()->Render();
}

void ModelViewWidget::addModelData(const QString &filePath, vtkPolyData *polyData)
{
    if (polyData == nullptr || (polyData->GetNumberOfPoints() <= 0 && polyData->GetNumberOfCells() <= 0)) {
        return;
    }

    const QFileInfo info(filePath);

    vtkNew<vtkPolyDataMapper> mapper;
    mapper->SetInputData(polyData);

    vtkNew<vtkActor> actor;
    actor->SetMapper(mapper);
    actor->GetProperty()->SetOpacity(1.0);

    m_renderer->AddActor(actor);
    m_renderer->ResetCamera();
    m_statusLabel->setText(QStringLiteral("已加载模型: %1").arg(info.fileName()));
    m_vtkWidget->renderWindow()->Render();
}
