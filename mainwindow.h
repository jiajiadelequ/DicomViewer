#pragma once

#include "src/model/studypackage.h"

#include <QMainWindow>

class FourPaneViewer;
class QLabel;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override = default;

private slots:
    void openStudyPackage();

private:
    void createMenus();
    void updateStatusBar(const StudyPackage &package);
    void showPackageError(const QString &message);

    FourPaneViewer *m_viewer;
    QLabel *m_statusLabel;
};
