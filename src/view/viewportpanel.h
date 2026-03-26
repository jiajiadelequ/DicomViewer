#pragma once

#include <QFrame>

class QLabel;

class ViewportPanel final : public QFrame
{
    Q_OBJECT

public:
    explicit ViewportPanel(const QString &title, const QString &description, QWidget *parent = nullptr);

    void setDescription(const QString &description);

private:
    QLabel *m_titleLabel;
    QLabel *m_descriptionLabel;
};
