#include "viewerstatewidget.h"

#include <QLabel>
#include <QVBoxLayout>

ViewerStateWidget::ViewerStateWidget(QWidget *parent)
    : QWidget(parent)
    , m_titleLabel(new QLabel(this))
    , m_messageLabel(new QLabel(this))
{
    // 这类状态页只切标题和文案，一个可复用部件就够了。
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(48, 48, 48, 48);
    layout->addStretch();

    m_titleLabel->setAlignment(Qt::AlignCenter);
    m_titleLabel->setStyleSheet(QStringLiteral("font-size: 18pt; font-weight: 700;"));

    m_messageLabel->setAlignment(Qt::AlignCenter);
    m_messageLabel->setWordWrap(true);
    m_messageLabel->setStyleSheet(QStringLiteral("font-size: 11pt; color: #6b7280;"));

    layout->addWidget(m_titleLabel);
    layout->addSpacing(12);
    layout->addWidget(m_messageLabel);
    layout->addStretch();
}

void ViewerStateWidget::setState(const QString &title, const QString &message)
{
    m_titleLabel->setText(title);
    m_messageLabel->setText(message);
}
