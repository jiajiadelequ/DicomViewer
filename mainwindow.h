#pragma once

#include "src/model/studypackage.h"

#include <QMainWindow>
#include <functional>

class CasePackageReader;
class FourPaneViewer;
class QLabel;
class QProgressBar;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void openStudyPackage();

private:
    void createMenus();
    void updateStatusBar(const StudyPackage &package);
    void showPackageError(const QString &message);
    bool runWithLoadingDialog(const QString &title, const std::function<bool(QLabel *, QProgressBar *)> &task);

    FourPaneViewer *m_viewer;
    QLabel *m_statusLabel;
    CasePackageReader *m_packageReader;
};
