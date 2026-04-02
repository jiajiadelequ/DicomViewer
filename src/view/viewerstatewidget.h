#pragma once

#include <QWidget>

class QLabel;

// 用于空状态、加载中和加载失败这几类页面提示。
class ViewerStateWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit ViewerStateWidget(QWidget *parent = nullptr);

    void setState(const QString &title, const QString &message);

private:
    QLabel *m_titleLabel;
    QLabel *m_messageLabel;
};
