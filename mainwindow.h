#pragma once

#include "src/core/runtime/studyloadresult.h"
#include "src/model/studypackage.h"

#include <atomic>
#include <memory>

#include <QMainWindow>

template <typename T>
class QFutureWatcher;

class QAction;
class QDialog;
class FourPaneViewer;
class QLabel;
class QProgressBar;
class QPushButton;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void openStudyPackage();
    void openImageFile();
    void handleStudyLoadFinished();
    void cancelStudyLoad();

private:
    void beginStudyLoad(const QString &sourcePath, bool sourceIsFile);
    void ensureLoadingDialog();
    void updateLoadingProgress(const QString &message, int percent);
    void createMenus();
    void updateStatusBar(const StudyPackage &package);
    void showPackageError(const QString &message);

    FourPaneViewer *m_viewer;
    QLabel *m_statusLabel;
    QAction *m_openAction;
    QAction *m_openImageAction;
    QFutureWatcher<StudyLoadResult> *m_loadWatcher;
    QDialog *m_loadingDialog;
    QLabel *m_loadingMessageLabel;
    QProgressBar *m_loadingProgressBar;
    QPushButton *m_loadingCancelButton;
    StudyPackage m_currentPackage;
    std::shared_ptr<std::atomic_bool> m_loadCancelFlag;
    int m_activeLoadId = 0;
};
