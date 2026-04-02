#include "mainwindow.h"

#include "src/core/studyloader.h"
#include "src/view/fourpaneviewer.h"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QFileDialog>
#include <QFutureWatcher>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressBar>
#include <QStatusBar>
#include <QStringList>
#include <QVBoxLayout>

#include <QtConcurrent/QtConcurrentRun>

#include <gdcmVersion.h>
#include <itkVersion.h>
#include <vtkVersion.h>
#include <zlib.h>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_viewer(new FourPaneViewer(this))
    , m_statusLabel(new QLabel(this))
    , m_openAction(nullptr)
    , m_loadWatcher(new QFutureWatcher<StudyLoadResult>(this))
    , m_loadingDialog(nullptr)
    , m_loadingMessageLabel(nullptr)
{
    setCentralWidget(m_viewer);
    createMenus();

    statusBar()->addPermanentWidget(m_statusLabel, 1);
    updateStatusBar(StudyPackage{});
    connect(m_loadWatcher, &QFutureWatcher<StudyLoadResult>::finished, this, &MainWindow::handleStudyLoadFinished);

    resize(1440, 900);
    setWindowTitle(QStringLiteral("Dicom Viewer Workstation"));
}

void MainWindow::openStudyPackage()
{
    if (m_loadWatcher->isRunning()) {
        return;
    }

    const QString rootPath = QFileDialog::getExistingDirectory(
        this,
        QStringLiteral("选择病例包目录"));
    if (rootPath.isEmpty()) {
        return;
    }

    beginStudyLoad(rootPath);
}

void MainWindow::handleStudyLoadFinished()
{
    if (m_loadingDialog != nullptr) {
        m_loadingDialog->hide();
    }
    if (m_openAction != nullptr) {
        m_openAction->setEnabled(true);
    }

    const StudyLoadResult result = m_loadWatcher->result();
    QString errorMessage = result.errorMessage;
    if (!result.succeeded() || !m_viewer->applyStudyLoadResult(result, &errorMessage)) {
        m_viewer->showErrorState(errorMessage);
        updateStatusBar(StudyPackage{});
        showPackageError(errorMessage);
        return;
    }

    updateStatusBar(result.package);
}

void MainWindow::beginStudyLoad(const QString &rootPath)
{
    ensureLoadingDialog();

    if (m_openAction != nullptr) {
        m_openAction->setEnabled(false);
    }

    m_viewer->showLoadingState(QStringLiteral("正在后台扫描目录并读取 DICOM/模型..."));
    m_loadingMessageLabel->setText(QStringLiteral("正在扫描病例目录并读取影像、模型数据...\n%1").arg(rootPath));
    m_loadingDialog->show();
    m_loadingDialog->raise();
    m_loadingDialog->activateWindow();

    m_loadWatcher->setFuture(QtConcurrent::run([rootPath]() {
        return StudyLoader::loadFromDirectory(rootPath);
    }));
}

void MainWindow::ensureLoadingDialog()
{
    if (m_loadingDialog != nullptr) {
        return;
    }

    m_loadingDialog = new QDialog(this);
    m_loadingDialog->setWindowTitle(QStringLiteral("正在加载病例包"));
    m_loadingDialog->setWindowModality(Qt::WindowModal);
    m_loadingDialog->setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint);
    m_loadingDialog->setMinimumWidth(420);

    auto *layout = new QVBoxLayout(m_loadingDialog);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(12);

    auto *titleLabel = new QLabel(QStringLiteral("正在后台处理，请稍候..."), m_loadingDialog);
    titleLabel->setStyleSheet(QStringLiteral("font-size: 12pt; font-weight: 700;"));

    m_loadingMessageLabel = new QLabel(QStringLiteral("正在扫描病例目录并读取影像、模型数据..."), m_loadingDialog);
    m_loadingMessageLabel->setWordWrap(true);

    auto *progressBar = new QProgressBar(m_loadingDialog);
    progressBar->setTextVisible(false);
    progressBar->setRange(0, 0);

    layout->addWidget(titleLabel);
    layout->addWidget(m_loadingMessageLabel);
    layout->addWidget(progressBar);
}

void MainWindow::createMenus()
{
    auto *fileMenu = menuBar()->addMenu(QStringLiteral("文件"));

    m_openAction = new QAction(QStringLiteral("打开病例包"), this);
    m_openAction->setShortcut(QKeySequence::Open);
    connect(m_openAction, &QAction::triggered, this, &MainWindow::openStudyPackage);
    fileMenu->addAction(m_openAction);

    auto *exitAction = new QAction(QStringLiteral("退出"), this);
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, this, &QWidget::close);
    fileMenu->addAction(exitAction);
}

void MainWindow::updateStatusBar(const StudyPackage &package)
{
    QStringList parts;
    parts << QStringLiteral("VTK %1").arg(QString::fromLatin1(vtkVersion::GetVTKVersion()));
    parts << QStringLiteral("ITK %1").arg(QString::fromLatin1(itk::Version::GetITKVersion()));
    parts << QStringLiteral("GDCM %1").arg(QString::fromLatin1(gdcm::Version::GetVersion()));
    parts << QStringLiteral("ZLIB %1").arg(QString::fromLatin1(zlibVersion()));

    if (package.isValid()) {
        parts << QStringLiteral("当前病例: %1").arg(package.rootPath);
    } else {
        parts << QStringLiteral("当前病例: 未加载");
    }

    m_statusLabel->setText(parts.join(QStringLiteral(" | ")));
}

void MainWindow::showPackageError(const QString &message)
{
    QMessageBox::warning(
        this,
        QStringLiteral("无法加载病例包"),
        message.isEmpty() ? QStringLiteral("病例包目录缺少可识别的数据。") : message);
}
