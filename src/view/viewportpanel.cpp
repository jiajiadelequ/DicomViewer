#include "viewportpanel.h"

#include <QLabel>
#include <QVBoxLayout>

ViewportPanel::ViewportPanel(const QString &title, const QString &description, QWidget *parent)
    : QFrame(parent)
    , m_titleLabel(new QLabel(title, this))
    , m_descriptionLabel(new QLabel(description, this))
{
    setFrameShape(QFrame::StyledPanel);
    setMinimumSize(220, 180);
    setStyleSheet(QStringLiteral(
        "QFrame { background-color: #101215; border: 1px solid #4a4f57; }"
        "QLabel { color: #e7ebef; }"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(8);

    m_titleLabel->setStyleSheet(QStringLiteral("font-size: 13pt; font-weight: 700;"));
    m_descriptionLabel->setWordWrap(true);
    m_descriptionLabel->setStyleSheet(QStringLiteral("color: #b8c0ca; font-size: 10pt;"));

    layout->addWidget(m_titleLabel);
    layout->addStretch();
    layout->addWidget(m_descriptionLabel);
}

void ViewportPanel::setDescription(const QString &description)
{
    m_descriptionLabel->setText(description);
}
