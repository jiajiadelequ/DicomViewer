#pragma once

#include <QPoint>
#include <QSize>

class QLabel;
class QWidget;
class vtkImageData;
class vtkImageMapToWindowLevelColors;

class MprWindowLevelController final
{
public:
    struct Values
    {
        double window = 0.0;
        double level = 0.0;
    };

    explicit MprWindowLevelController(QWidget *viewport);

    void setRecommendedWindowLevel(double window, double level);
    void clearRecommendedWindowLevel();
    void reset();
    void ensureInitialized(vtkImageData *imageData);
    void setCurrentWindowLevel(double window, double level);
    [[nodiscard]] Values currentWindowLevel() const;
    void applyTo(vtkImageMapToWindowLevelColors *filter);
    void updateOverlayPosition();

    [[nodiscard]] bool canStartDrag() const;
    void beginDrag(const QPoint &position);
    [[nodiscard]] bool isDragging() const;
    bool dragWindowLevel(const QPoint &position, const QSize &viewportSize, Values *values) const;
    void endDrag();

private:
    [[nodiscard]] bool hasCurrentWindowLevel() const;
    void updateOverlay();

    QWidget *m_viewport;
    QLabel *m_overlayLabel;
    QPoint m_dragStartPosition;
    double m_recommendedWindow = 0.0;
    double m_recommendedLevel = 0.0;
    double m_currentWindow = 0.0;
    double m_currentLevel = 0.0;
    double m_dragStartWindow = 0.0;
    double m_dragStartLevel = 0.0;
    bool m_hasRecommendedWindowLevel = false;
    bool m_dragActive = false;
};
