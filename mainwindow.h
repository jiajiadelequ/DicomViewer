#pragma once

#include "src/core/runtime/studyloadresult.h"
#include "src/model/studypackage.h"

#include <QMainWindow>

template <typename T>
class QFutureWatcher;

class QAction;
class QDialog;
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
    void handleStudyLoadFinished();

private:
    void beginStudyLoad(const QString &rootPath);
    void ensureLoadingDialog();
    void createMenus();
    void updateStatusBar(const StudyPackage &package);
    void showPackageError(const QString &message);

    FourPaneViewer *m_viewer;
    QLabel *m_statusLabel;
    QAction *m_openAction;
    QFutureWatcher<StudyLoadResult> *m_loadWatcher;
    QDialog *m_loadingDialog;
    QLabel *m_loadingMessageLabel;
};
