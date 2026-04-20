#include "modelviewwidget.h"

#include "modelclippingcontroller.h"
#include "modelviewcameracontroller.h"
#include "modelviewcrosshaircontroller.h"
#include "modelrulercontroller.h"

#include <QEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include <QVTKOpenGLNativeWidget.h>

#include <algorithm>
#include <array>

#include <vtkActor.h>
#include <vtkClipClosedSurface.h>
#include <vtkCommand.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkImageData.h>
#include <vtkInteractorStyleTrackballCamera.h>
#include <vtkOpenGLPolyDataMapper.h>
#include <vtkPlane.h>
#include <vtkPlaneCollection.h>
#include <vtkPlanes.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderer.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkShader.h>
#include <vtkShaderProgram.h>
#include <vtkShaderProperty.h>

namespace
{
constexpr float kRulerOccludedModelOpacity = 0.38f;
constexpr int kMaxProjectedSegments = 128;
constexpr int kMaxProjectedNodes = 128;
constexpr int kMaxProjectedTextRects = 32;
constexpr float kProjectedLineRadiusPixels = 4.5f;
constexpr float kProjectedDepthEpsilon = 0.0005f;

class ModelOcclusionShaderCallback final : public vtkCommand
{
public:
    static ModelOcclusionShaderCallback *New()
    {
        return new ModelOcclusionShaderCallback();
    }

    ModelRulerController *RulerController = nullptr;
    const bool *RulerEnabled = nullptr;

    void Execute(vtkObject *, unsigned long, void *callData) override
    {
        auto *program = reinterpret_cast<vtkShaderProgram *>(callData);
        if (program == nullptr) {
            return;
        }

        program->SetUniformf("rulerOccludedAlpha", kRulerOccludedModelOpacity);
        program->SetUniformf("rulerDepthEpsilon", kProjectedDepthEpsilon);
        program->SetUniformf("rulerLineRadiusPixels", kProjectedLineRadiusPixels);

        if (RulerController == nullptr || RulerEnabled == nullptr || !(*RulerEnabled)) {
            program->SetUniformi("rulerSegmentCount", 0);
            program->SetUniformi("rulerNodeCount", 0);
            program->SetUniformi("rulerTextRectCount", 0);
            return;
        }

        RulerController->refreshProjectedOverlays();

        const auto &segments = RulerController->projectedSegments();
        const auto &nodes = RulerController->projectedNodes();
        const auto &textRects = RulerController->projectedTextRects();

        const int segmentCount = std::min<int>(static_cast<int>(segments.size()), kMaxProjectedSegments);
        const int nodeCount = std::min<int>(static_cast<int>(nodes.size()), kMaxProjectedNodes);
        const int textRectCount = std::min<int>(static_cast<int>(textRects.size()), kMaxProjectedTextRects);

        std::array<std::array<float, 4>, kMaxProjectedSegments> segmentData {};
        std::array<std::array<float, 2>, kMaxProjectedSegments> segmentDepthData {};
        std::array<std::array<float, 4>, kMaxProjectedNodes> nodeData {};
        std::array<std::array<float, 4>, kMaxProjectedTextRects> textRectData {};
        std::array<std::array<float, 2>, kMaxProjectedTextRects> textDepthData {};

        for (int index = 0; index < segmentCount; ++index) {
            const auto &segment = segments[static_cast<std::size_t>(index)];
            segmentData[static_cast<std::size_t>(index)] = { segment.startDisplay[0],
                                                             segment.startDisplay[1],
                                                             segment.endDisplay[0],
                                                             segment.endDisplay[1] };
            segmentDepthData[static_cast<std::size_t>(index)] = { segment.startDepth, segment.endDepth };
        }

        for (int index = 0; index < nodeCount; ++index) {
            const auto &node = nodes[static_cast<std::size_t>(index)];
            nodeData[static_cast<std::size_t>(index)] = { node.centerDisplay[0],
                                                          node.centerDisplay[1],
                                                          node.depth,
                                                          node.radiusPixels };
        }

        for (int index = 0; index < textRectCount; ++index) {
            const auto &textRect = textRects[static_cast<std::size_t>(index)];
            textRectData[static_cast<std::size_t>(index)] = { textRect.minDisplay[0],
                                                              textRect.minDisplay[1],
                                                              textRect.maxDisplay[0],
                                                              textRect.maxDisplay[1] };
            textDepthData[static_cast<std::size_t>(index)] = { textRect.depth, 0.0f };
        }

        program->SetUniformi("rulerSegmentCount", segmentCount);
        program->SetUniformi("rulerNodeCount", nodeCount);
        program->SetUniformi("rulerTextRectCount", textRectCount);
        program->SetUniform4fv("rulerSegments",
                               segmentCount,
                               reinterpret_cast<const float (*)[4]>(segmentData.data()));
        program->SetUniform2fv("rulerSegmentDepths",
                               segmentCount,
                               reinterpret_cast<const float (*)[2]>(segmentDepthData.data()));
        program->SetUniform4fv("rulerNodes",
                               nodeCount,
                               reinterpret_cast<const float (*)[4]>(nodeData.data()));
        program->SetUniform4fv("rulerTextRects",
                               textRectCount,
                               reinterpret_cast<const float (*)[4]>(textRectData.data()));
        program->SetUniform2fv("rulerTextRectDepths",
                               textRectCount,
                               reinterpret_cast<const float (*)[2]>(textDepthData.data()));
    }
};

void installLocalRulerOcclusionShader(vtkActor *actor,
                                      vtkPolyDataMapper *mapper,
                                      ModelRulerController *rulerController,
                                      const bool *rulerEnabled)
{
    if (actor == nullptr || mapper == nullptr || rulerController == nullptr || rulerEnabled == nullptr) {
        return;
    }

    auto *shaderProperty = actor->GetShaderProperty();
    shaderProperty->AddFragmentShaderReplacement(
        "//VTK::System::Dec",
        false,
        R"(
//VTK::System::Dec
uniform int rulerSegmentCount;
uniform int rulerNodeCount;
uniform int rulerTextRectCount;
uniform vec4 rulerSegments[128];
uniform vec2 rulerSegmentDepths[128];
uniform vec4 rulerNodes[128];
uniform vec4 rulerTextRects[32];
uniform vec2 rulerTextRectDepths[32];
uniform float rulerOccludedAlpha;
uniform float rulerDepthEpsilon;
uniform float rulerLineRadiusPixels;

float rulerDistanceToSegment(vec2 p, vec2 a, vec2 b, out float tValue)
{
  vec2 ab = b - a;
  float abLengthSquared = dot(ab, ab);
  if (abLengthSquared <= 0.0001)
  {
    tValue = 0.0;
    return length(p - a);
  }
  tValue = clamp(dot(p - a, ab) / abLengthSquared, 0.0, 1.0);
  vec2 closest = a + tValue * ab;
  return length(p - closest);
}

bool vtkIsLocallyOccludedByRuler(vec2 fragXY, float fragZ)
{
  for (int i = 0; i < rulerSegmentCount; ++i)
  {
    float tValue = 0.0;
    float distanceToSegment = rulerDistanceToSegment(fragXY, rulerSegments[i].xy, rulerSegments[i].zw, tValue);
    if (distanceToSegment <= rulerLineRadiusPixels)
    {
      float rulerDepth = mix(rulerSegmentDepths[i].x, rulerSegmentDepths[i].y, tValue);
      if (fragZ < rulerDepth - rulerDepthEpsilon)
      {
        return true;
      }
    }
  }

  for (int i = 0; i < rulerNodeCount; ++i)
  {
    if (length(fragXY - rulerNodes[i].xy) <= rulerNodes[i].w
        && fragZ < rulerNodes[i].z - rulerDepthEpsilon)
    {
      return true;
    }
  }

  for (int i = 0; i < rulerTextRectCount; ++i)
  {
    if (fragXY.x >= rulerTextRects[i].x
        && fragXY.x <= rulerTextRects[i].z
        && fragXY.y >= rulerTextRects[i].y
        && fragXY.y <= rulerTextRects[i].w
        && fragZ < rulerTextRectDepths[i].x - rulerDepthEpsilon)
    {
      return true;
    }
  }

  return false;
}
)",
        false);

    shaderProperty->AddFragmentShaderReplacement(
        "//VTK::Light::Impl",
        false,
        R"(
//VTK::Light::Impl
if (vtkIsLocallyOccludedByRuler(gl_FragCoord.xy, gl_FragCoord.z))
{
  float targetOpacity = min(fragOutput0.a, rulerOccludedAlpha);
  float alphaScale = targetOpacity / max(fragOutput0.a, 0.0001);
  fragOutput0.rgb *= alphaScale;
  fragOutput0.a = targetOpacity;
}
)",
        false);

    vtkNew<ModelOcclusionShaderCallback> shaderCallback;
    shaderCallback->RulerController = rulerController;
    shaderCallback->RulerEnabled = rulerEnabled;
    mapper->AddObserver(vtkCommand::UpdateShaderEvent, shaderCallback);
}
}

ModelViewWidget::ModelViewWidget(QWidget *parent)
    : QWidget(parent)
    , m_statusLabel(new QLabel(QStringLiteral("等待加载模型"), this))
    , m_maximizeButton(new QToolButton(this))
    , m_vtkWidget(new QVTKOpenGLNativeWidget(this))
    , m_cameraController(nullptr)
    , m_clippingController(nullptr)
    , m_crosshairController(nullptr)
    , m_renderer(vtkSmartPointer<vtkRenderer>::New())
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    auto *headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(6);

    m_statusLabel->setStyleSheet(QStringLiteral("color: #6b7280;"));
    m_maximizeButton->setAutoRaise(true);
    connect(m_maximizeButton, &QToolButton::clicked, this, &ModelViewWidget::maximizeToggled);

    auto renderWindow = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
    renderWindow->SetAlphaBitPlanes(1);
    renderWindow->SetMultiSamples(0);
    m_vtkWidget->setRenderWindow(renderWindow);
    m_vtkWidget->interactor()->SetInteractorStyle(vtkSmartPointer<vtkInteractorStyleTrackballCamera>::New());
    m_vtkWidget->installEventFilter(this);
    m_vtkWidget->setMouseTracking(true);
    renderWindow->AddRenderer(m_renderer);
    // Use a low-saturation light background to improve contrast for pale anatomy meshes.
    m_renderer->SetBackground(0.93, 0.95, 0.98);
    m_renderer->UseDepthPeelingOn();
    m_renderer->SetMaximumNumberOfPeels(32);
    m_renderer->SetOcclusionRatio(0.08);

    m_cameraController = std::make_unique<ModelViewCameraController>(this, m_renderer, m_vtkWidget->interactor());
    m_clippingController = std::make_unique<ModelClippingController>(m_vtkWidget, m_renderer);
    m_crosshairController = std::make_unique<ModelViewCrosshairController>(m_renderer);
    m_rulerController = std::make_unique<ModelRulerController>(m_renderer);
    m_renderer->AddActor(m_rulerController->actor());
    m_cameraController->setBoundsProvider([this](ModelViewCameraController::BoundsArray &bounds) {
        return m_crosshairController != nullptr && m_crosshairController->cameraBounds(bounds.data());
    });
    m_clippingController->setStatusHandler([this](const QString &message) {
        if (m_statusLabel != nullptr) {
            m_statusLabel->setText(message);
        }
    });
    m_clippingController->setCommitHandler([this]() {
        updateClippedModels();
    });

    headerLayout->addStretch(1);
    headerLayout->addWidget(m_cameraController->viewButton());
    headerLayout->addWidget(m_maximizeButton);

    layout->addLayout(headerLayout);
    layout->addWidget(m_vtkWidget, 1);
    layout->addWidget(m_statusLabel);
    setMaximizedState(false);
}

void ModelViewWidget::beginSceneBatch(const QString &message)
{
    m_sceneBatchActive = true;
    m_sceneNeedsRender = false;
    m_sceneNeedsCameraReset = false;
    clearScene(message);
}

void ModelViewWidget::endSceneBatch(const QString &message)
{
    m_statusLabel->setText(message);
    m_sceneBatchActive = false;
    flushQueuedSceneUpdate();
}

void ModelViewWidget::clearScene(const QString &message)
{
    if (m_clippingController != nullptr) {
        m_clippingController->deactivate();
    }
    m_models.clear();
    m_crosshairController->clearScene();
    m_rulerController->clearScene();
    m_renderer->AddActor(m_rulerController->actor());
    m_rulerController->setVisible(m_rulerEnabled);
    updateRulerOcclusionAppearance();
    m_statusLabel->setText(message);
    queueSceneUpdate(false);
}

void ModelViewWidget::addModelData(const QString &filePath,
                                   vtkPolyData *polyData,
                                   const LoadedModelData::MaterialData &material)
{
    if (polyData == nullptr || (polyData->GetNumberOfPoints() <= 0 && polyData->GetNumberOfCells() <= 0)) {
        return;
    }

    const QFileInfo info(filePath);

    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputData(polyData);

    auto actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);
    const double baseOpacity = material.hasMaterial ? material.opacity : 1.0;
    actor->GetProperty()->SetOpacity(baseOpacity);
    if (material.hasMaterial) {
        actor->GetProperty()->SetColor(material.color[0], material.color[1], material.color[2]);
        actor->GetProperty()->SetSpecularColor(material.specularColor[0],
                                               material.specularColor[1],
                                               material.specularColor[2]);
        actor->GetProperty()->SetSpecular(material.specularStrength);
        actor->GetProperty()->SetSpecularPower(material.specularPower);
    }
    installLocalRulerOcclusionShader(actor, mapper, m_rulerController.get(), &m_rulerEnabled);

    m_models.push_back(ModelEntry { actor, mapper, polyData, baseOpacity });
    m_crosshairController->addModelActor(actor, polyData);
    m_rulerController->addModelActor(actor);
    m_crosshairController->updateGeometry();
    m_rulerController->updateGeometry();
    if (!m_sceneBatchActive) {
        m_statusLabel->setText(QStringLiteral("已加载模型: %1").arg(info.fileName()));
    }
    if (m_clippingEnabled) {
        updateClippedModels();
    }
    updateRulerOcclusionAppearance();
    queueSceneUpdate(true);
}

void ModelViewWidget::setModelVisibility(int index, bool visible)
{
    m_crosshairController->setModelVisibility(index, visible);
    m_crosshairController->updateGeometry();
    queueSceneUpdate(false);
}

void ModelViewWidget::setReferenceImageData(vtkImageData *imageData)
{
    m_crosshairController->setReferenceImageData(imageData);
    m_crosshairController->updateGeometry();
    queueSceneUpdate(false);
}

void ModelViewWidget::setCrosshairEnabled(bool enabled)
{
    m_crosshairController->setEnabled(enabled);
    m_crosshairController->updateGeometry();
    queueSceneUpdate(false);
}

void ModelViewWidget::setRulerEnabled(bool enabled)
{
    m_rulerEnabled = enabled;
    m_rulerController->setEnabled(enabled);
    m_rulerController->setVisible(enabled);
    m_rulerController->updateGeometry();
    updateRulerOcclusionAppearance();
    queueSceneUpdate(false);
}

void ModelViewWidget::setClippingEnabled(bool enabled)
{
    if (m_clippingEnabled == enabled) {
        if (enabled && m_clippingController != nullptr) {
            m_clippingController->activate([this](double bounds[6]) {
                return m_crosshairController != nullptr && m_crosshairController->cameraBounds(bounds);
            });
        }
        return;
    }

    m_clippingEnabled = enabled;
    if (m_clippingEnabled) {
        if (m_clippingController != nullptr) {
            m_clippingController->activate([this](double bounds[6]) {
                return m_crosshairController != nullptr && m_crosshairController->cameraBounds(bounds);
            });
        }
        updateClippedModels();
        m_statusLabel->setText(QStringLiteral("模型裁剪已启用，拖拽面可伸缩裁剪框，空白处左键可旋转视角"));
    } else {
        if (m_clippingController != nullptr) {
            m_clippingController->deactivate();
        }
        for (ModelEntry &entry : m_models) {
            if (entry.mapper != nullptr && entry.originalPolyData != nullptr) {
                entry.mapper->SetInputData(entry.originalPolyData);
                entry.mapper->Update();
            }
        }
        m_statusLabel->setText(m_models.empty()
                                   ? QStringLiteral("等待加载模型")
                                   : QStringLiteral("模型裁剪已关闭，已恢复原始模型"));
    }

    m_crosshairController->updateGeometry();
    queueSceneUpdate(false);
}

void ModelViewWidget::setCursorWorldPosition(double x, double y, double z)
{
    setCursorWorldPositionInternal(ModelViewCrosshairController::Axis { x, y, z }, false);
}

std::array<double, 3> ModelViewWidget::cursorWorldPosition() const
{
    return m_crosshairController->cursorWorldPosition();
}

bool ModelViewWidget::hasModels() const
{
    return !m_models.empty();
}

bool ModelViewWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_vtkWidget) {
        return QWidget::eventFilter(watched, event);
    }

    switch (event->type()) {
    case QEvent::MouseButtonDblClick: {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            emit maximizeToggled();
            return true;
        }
        break;
    }
    case QEvent::MouseButtonPress: {
        if (m_clippingEnabled) {
            if (m_clippingController != nullptr && m_clippingController->handleViewportEvent(event)) {
                return true;
            }
            return QWidget::eventFilter(watched, event);
        }
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (m_rulerEnabled
            && mouseEvent->button() == Qt::RightButton
            && mouseEvent->modifiers() == Qt::NoModifier
            && m_rulerController->isMeasuring()) {
            m_rulerController->finishMeasurement();
            m_rulerController->updateGeometry();
            m_vtkWidget->renderWindow()->Render();
            return true;
        }

        if (mouseEvent->button() != Qt::LeftButton) {
            break;
        }

        if (m_rulerEnabled && mouseEvent->modifiers() == Qt::NoModifier) {
            if (m_rulerController->beginNodeDrag(m_vtkWidget, mouseEvent->pos())) {
                m_rulerController->updateGeometry();
                m_vtkWidget->renderWindow()->Render();
                return true;
            }

            if (m_rulerController->recordPoint(m_vtkWidget, mouseEvent->pos())) {
                m_rulerController->updateGeometry();
                m_vtkWidget->renderWindow()->Render();
                return true;
            }
        }

        ModelViewCrosshairController::Axis worldPosition;
        if (!m_crosshairController->beginInteraction(m_vtkWidget, mouseEvent->pos(), &worldPosition)) {
            break;
        }

        setCursorWorldPositionInternal(worldPosition, true);
        return true;
    }
    case QEvent::MouseMove: {
        if (m_clippingEnabled) {
            if (m_clippingController != nullptr && m_clippingController->handleViewportEvent(event)) {
                return true;
            }
            return QWidget::eventFilter(watched, event);
        }
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (m_rulerEnabled && mouseEvent->modifiers() == Qt::NoModifier) {
            if (m_rulerController->updateNodeDrag(m_vtkWidget, mouseEvent->pos())) {
                m_rulerController->updateGeometry();
                m_vtkWidget->renderWindow()->Render();
                return true;
            }

            if (m_rulerController->updateHoverPoint(m_vtkWidget, mouseEvent->pos())) {
                m_rulerController->updateGeometry();
                m_vtkWidget->renderWindow()->Render();
                return true;
            }
        }
        ModelViewCrosshairController::Axis worldPosition;
        if (!m_crosshairController->updateInteraction(m_vtkWidget,
                                                      (mouseEvent->buttons() & Qt::LeftButton) != 0,
                                                      mouseEvent->pos(),
                                                      &worldPosition)) {
            break;
        }

        setCursorWorldPositionInternal(worldPosition, true);
        return true;
    }
    case QEvent::MouseButtonRelease: {
        if (m_clippingEnabled) {
            if (m_clippingController != nullptr && m_clippingController->handleViewportEvent(event)) {
                return true;
            }
            return QWidget::eventFilter(watched, event);
        }
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton && m_rulerController->endNodeDrag()) {
            m_rulerController->updateGeometry();
            m_vtkWidget->renderWindow()->Render();
            return true;
        }
        if (m_crosshairController->endInteraction(mouseEvent->button() == Qt::LeftButton)) {
            return true;
        }
        break;
    }
    case QEvent::Leave: {
        if (m_clippingEnabled) {
            if (m_clippingController != nullptr) {
                (void)m_clippingController->handleViewportEvent(event);
            }
            return QWidget::eventFilter(watched, event);
        }
        break;
    }
    default:
        break;
    }

    return QWidget::eventFilter(watched, event);
}

void ModelViewWidget::setMaximizedState(bool maximized)
{
    m_maximizeButton->setIcon(style()->standardIcon(maximized
                                                        ? QStyle::SP_TitleBarNormalButton
                                                        : QStyle::SP_TitleBarMaxButton));
    m_maximizeButton->setToolTip(maximized
                                     ? QStringLiteral("恢复四视图")
                                     : QStringLiteral("放大当前视图"));
}

void ModelViewWidget::setCursorWorldPositionInternal(const std::array<double, 3> &worldPosition, bool emitSignal)
{
    m_crosshairController->setCursorWorldPosition(ModelViewCrosshairController::Axis { worldPosition[0],
                                                                                       worldPosition[1],
                                                                                       worldPosition[2] });
    m_crosshairController->updateGeometry();
    m_vtkWidget->renderWindow()->Render();

    if (emitSignal) {
        const auto currentPosition = m_crosshairController->cursorWorldPosition();
        emit cursorWorldPositionChanged(currentPosition[0],
                                        currentPosition[1],
                                        currentPosition[2]);
    }
}

void ModelViewWidget::queueSceneUpdate(bool resetCamera)
{
    m_sceneNeedsRender = true;
    m_sceneNeedsCameraReset = m_sceneNeedsCameraReset || resetCamera;
    if (!m_sceneBatchActive) {
        flushQueuedSceneUpdate();
    }
}

void ModelViewWidget::flushQueuedSceneUpdate()
{
    if (m_sceneNeedsCameraReset) {
        m_cameraController->resetToAnatomicalView();
    }
    if (m_sceneNeedsRender) {
        m_vtkWidget->renderWindow()->Render();
    }

    m_sceneNeedsRender = false;
    m_sceneNeedsCameraReset = false;
}

void ModelViewWidget::updateRulerOcclusionAppearance()
{
    for (ModelEntry &entry : m_models) {
        if (entry.actor == nullptr) {
            continue;
        }

        entry.actor->GetProperty()->SetOpacity(entry.baseOpacity);
        if (m_rulerEnabled || entry.baseOpacity < 0.999) {
            entry.actor->ForceOpaqueOff();
            entry.actor->ForceTranslucentOn();
        } else {
            entry.actor->ForceTranslucentOff();
            entry.actor->ForceOpaqueOff();
        }
    }
}

void ModelViewWidget::updateClippedModels()
{
    if (!m_clippingEnabled || m_models.empty() || m_clippingController == nullptr) {
        return;
    }

    auto boxPlanes = vtkSmartPointer<vtkPlanes>::New();
    if (!m_clippingController->copyPlanesTo(boxPlanes)) {
        return;
    }

    auto clippingPlanes = vtkSmartPointer<vtkPlaneCollection>::New();
    for (int planeIndex = 0; planeIndex < boxPlanes->GetNumberOfPlanes(); ++planeIndex) {
        auto plane = vtkSmartPointer<vtkPlane>::New();
        boxPlanes->GetPlane(planeIndex, plane);
        clippingPlanes->AddItem(plane);
    }

    for (ModelEntry &entry : m_models) {
        if (entry.mapper == nullptr || entry.originalPolyData == nullptr) {
            continue;
        }

        auto clipper = vtkSmartPointer<vtkClipClosedSurface>::New();
        clipper->SetInputData(entry.originalPolyData);
        clipper->SetClippingPlanes(clippingPlanes);
        // vtkBoxRepresentation defaults to outward-facing plane normals, while
        // vtkClipClosedSurface keeps the "front" side unless InsideOut is enabled.
        // Turn InsideOut on so the inside of the box remains visible.
        clipper->InsideOutOn();
        clipper->GenerateFacesOn();
        clipper->Update();

        auto clippedPolyData = vtkSmartPointer<vtkPolyData>::New();
        clippedPolyData->DeepCopy(clipper->GetOutput());
        entry.mapper->SetInputData(clippedPolyData);
        entry.mapper->Update();
    }

    m_crosshairController->updateGeometry();
    queueSceneUpdate(false);
}
