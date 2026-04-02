#include "mprwindowlevelcontroller.h"

#include <QLabel>
#include <QWidget>

#include <vtkImageData.h>
#include <vtkImageMapToWindowLevelColors.h>

#include <algorithm>
#include <cmath>

MprWindowLevelController::MprWindowLevelController(QWidget *viewport)
    : m_viewport(viewport)
    , m_overlayLabel(new QLabel(viewport))
{
    m_overlayLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_overlayLabel->setStyleSheet(QStringLiteral(
        "padding: 4px 8px;"
        "border-radius: 4px;"
        "background-color: rgba(10, 12, 16, 180);"
        "color: #f3f4f6;"
        "font-size: 10pt;"
        "font-weight: 600;"));
    m_overlayLabel->setText(QStringLiteral("WW: -, WL: -"));
    m_overlayLabel->hide();
}

void MprWindowLevelController::setRecommendedWindowLevel(double window, double level)
{
    if (!std::isfinite(window) || !std::isfinite(level) || window <= 0.0) {
        clearRecommendedWindowLevel();
        return;
    }

    m_recommendedWindow = window;
    m_recommendedLevel = level;
    m_hasRecommendedWindowLevel = true;
}

void MprWindowLevelController::clearRecommendedWindowLevel()
{
    m_recommendedWindow = 0.0;
    m_recommendedLevel = 0.0;
    m_hasRecommendedWindowLevel = false;
}

void MprWindowLevelController::reset()
{
    m_currentWindow = 0.0;
    m_currentLevel = 0.0;
    m_dragActive = false;
    updateOverlay();
}

void MprWindowLevelController::ensureInitialized(vtkImageData *imageData)
{
    if (hasCurrentWindowLevel() || imageData == nullptr) {
        return;
    }

    if (m_hasRecommendedWindowLevel && m_recommendedWindow > 0.0) {
        m_currentWindow = m_recommendedWindow;
        m_currentLevel = m_recommendedLevel;
        return;
    }

    double scalarRange[2] { 0.0, 0.0 };
    imageData->GetScalarRange(scalarRange);
    m_currentWindow = std::max(1.0, scalarRange[1] - scalarRange[0]);
    m_currentLevel = 0.5 * (scalarRange[0] + scalarRange[1]);
}

void MprWindowLevelController::setCurrentWindowLevel(double window, double level)
{
    if (!std::isfinite(window) || !std::isfinite(level)) {
        return;
    }

    m_currentWindow = std::max(1.0, window);
    m_currentLevel = level;
}

MprWindowLevelController::Values MprWindowLevelController::currentWindowLevel() const
{
    return Values { m_currentWindow, m_currentLevel };
}

void MprWindowLevelController::applyTo(vtkImageMapToWindowLevelColors *filter)
{
    if (filter != nullptr && hasCurrentWindowLevel()) {
        filter->SetWindow(std::max(1.0, m_currentWindow));
        filter->SetLevel(m_currentLevel);
    }

    updateOverlay();
}

void MprWindowLevelController::updateOverlayPosition()
{
    if (m_overlayLabel == nullptr || m_viewport == nullptr) {
        return;
    }

    const int margin = 8;
    const QSize labelSize = m_overlayLabel->sizeHint();
    const int x = std::max(margin, m_viewport->width() - labelSize.width() - margin);
    m_overlayLabel->move(x, margin);
}

bool MprWindowLevelController::canStartDrag() const
{
    return hasCurrentWindowLevel();
}

void MprWindowLevelController::beginDrag(const QPoint &position)
{
    if (!canStartDrag()) {
        m_dragActive = false;
        return;
    }

    m_dragActive = true;
    m_dragStartPosition = position;
    m_dragStartWindow = m_currentWindow;
    m_dragStartLevel = m_currentLevel;
}

bool MprWindowLevelController::isDragging() const
{
    return m_dragActive;
}

bool MprWindowLevelController::dragWindowLevel(const QPoint &position, const QSize &viewportSize, Values *values) const
{
    if (!m_dragActive || values == nullptr) {
        return false;
    }

    const double widthScale = std::max(1.0, m_dragStartWindow);
    const double levelScale = std::max(1.0, m_dragStartWindow);
    const double deltaX = static_cast<double>(position.x() - m_dragStartPosition.x());
    const double deltaY = static_cast<double>(position.y() - m_dragStartPosition.y());
    const double normalizedX = deltaX / std::max(1, viewportSize.width());
    const double normalizedY = deltaY / std::max(1, viewportSize.height());

    values->window = std::max(1.0, m_dragStartWindow + normalizedX * widthScale * 4.0);
    values->level = m_dragStartLevel - normalizedY * levelScale * 4.0;
    return true;
}

void MprWindowLevelController::endDrag()
{
    m_dragActive = false;
}

bool MprWindowLevelController::hasCurrentWindowLevel() const
{
    return std::isfinite(m_currentWindow) && std::isfinite(m_currentLevel) && m_currentWindow > 0.0;
}

void MprWindowLevelController::updateOverlay()
{
    if (m_overlayLabel == nullptr) {
        return;
    }

    if (!hasCurrentWindowLevel()) {
        m_overlayLabel->hide();
        return;
    }

    m_overlayLabel->setText(QStringLiteral("WW: %1  WL: %2")
                                .arg(static_cast<int>(std::lround(m_currentWindow)))
                                .arg(static_cast<int>(std::lround(m_currentLevel))));
    m_overlayLabel->adjustSize();
    updateOverlayPosition();
    m_overlayLabel->show();
    m_overlayLabel->raise();
}
