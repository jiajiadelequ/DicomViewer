#include "mainwindow.h"

#include "src/core/studyloader.h"
#include "src/view/fourpaneviewer.h"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QEventLoop>
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
{
    setCentralWidget(m_viewer);
    createMenus();

    statusBar()->addPermanentWidget(m_statusLabel, 1);
    updateStatusBar(StudyPackage{});

    resize(1440, 900);
    setWindowTitle(QStringLiteral("Dicom Viewer Workstation"));
}

void MainWindow::openStudyPackage()
{
    const QString rootPath = QFileDialog::getExistingDirectory(
        this,
        QStringLiteral("选择病例包目录"));
    if (rootPath.isEmpty()) {
        return;
    }

    m_viewer->showLoadingState(QStringLiteral("正在后台扫描目录并读取 DICOM/模型..."));

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("正在加载病例包"));
    dialog.setModal(true);
    dialog.setWindowFlags(dialog.windowFlags() & ~Qt::WindowContextHelpButtonHint);
    dialog.setMinimumWidth(420);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(12);

    auto *titleLabel = new QLabel(QStringLiteral("正在后台处理，请稍候..."), &dialog);
    titleLabel->setStyleSheet(QStringLiteral("font-size: 12pt; font-weight: 700;"));

    auto *messageLabel = new QLabel(QStringLiteral("正在扫描病例目录并读取影像、模型数据..."), &dialog);
    messageLabel->setWordWrap(true);

    auto *progressBar = new QProgressBar(&dialog);
    progressBar->setTextVisible(false);
    progressBar->setRange(0, 0);

    layout->addWidget(titleLabel);
    layout->addWidget(messageLabel);
    layout->addWidget(progressBar);

    QFutureWatcher<StudyLoadResult> watcher;
    QEventLoop loop;
    connect(&watcher, &QFutureWatcher<StudyLoadResult>::finished, &loop, &QEventLoop::quit);

    dialog.show();
    qApp->processEvents(QEventLoop::ExcludeUserInputEvents);

    watcher.setFuture(QtConcurrent::run([rootPath]() {
        return StudyLoader::loadFromDirectory(rootPath);
    }));

    loop.exec();
    dialog.accept();
    qApp->processEvents(QEventLoop::ExcludeUserInputEvents);

    const StudyLoadResult result = watcher.result();
    QString errorMessage = result.errorMessage;
    if (!result.succeeded() || !m_viewer->applyStudyLoadResult(result, &errorMessage)) {
        m_viewer->showErrorState(errorMessage);
        showPackageError(errorMessage);
        return;
    }

    updateStatusBar(result.package);
}

void MainWindow::createMenus()
{
    auto *fileMenu = menuBar()->addMenu(QStringLiteral("文件"));

    auto *openAction = new QAction(QStringLiteral("打开病例包"), this);
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::openStudyPackage);
    fileMenu->addAction(openAction);

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
