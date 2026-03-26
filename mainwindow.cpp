#include "mainwindow.h"

#include "src/core/casepackagereader.h"
#include "src/view/fourpaneviewer.h"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QEventLoop>
#include <QFileDialog>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressBar>
#include <QStatusBar>
#include <QStringList>
#include <QVBoxLayout>

#include <gdcmVersion.h>
#include <itkVersion.h>
#include <vtkVersion.h>
#include <zlib.h>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_viewer(new FourPaneViewer(this))
    , m_statusLabel(new QLabel(this))
    , m_packageReader(new CasePackageReader())
{
    setCentralWidget(m_viewer);
    createMenus();

    statusBar()->addPermanentWidget(m_statusLabel, 1);
    updateStatusBar(StudyPackage{});

    resize(1440, 900);
    setWindowTitle(QStringLiteral("Dicom Viewer Workstation"));
}

MainWindow::~MainWindow()
{
    delete m_packageReader;
}

void MainWindow::openStudyPackage()
{
    const QString rootPath = QFileDialog::getExistingDirectory(
        this,
        QStringLiteral("选择病例包目录"));
    if (rootPath.isEmpty()) {
        return;
    }

    StudyPackage package;
    QString errorMessage;
    m_viewer->showLoadingState(QStringLiteral("正在准备病例目录..."));

    const bool ok = runWithLoadingDialog(
        QStringLiteral("正在加载病例包"),
        [&](QLabel *messageLabel, QProgressBar *progressBar) {
            messageLabel->setText(QStringLiteral("正在扫描病例目录..."));
            progressBar->setRange(0, 0);
            qApp->processEvents(QEventLoop::ExcludeUserInputEvents);

            package = m_packageReader->readFromDirectory(rootPath, &errorMessage);
            if (!package.isValid()) {
                return false;
            }

            messageLabel->setText(QStringLiteral("正在通过 ITK + GDCM 读取 DICOM 序列..."));
            m_viewer->showLoadingState(QStringLiteral("正在通过 ITK + GDCM 读取 DICOM 序列..."));
            qApp->processEvents(QEventLoop::ExcludeUserInputEvents);

            if (!m_viewer->loadStudyPackage(package, &errorMessage)) {
                return false;
            }

            messageLabel->setText(QStringLiteral("正在完成界面刷新..."));
            qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
            updateStatusBar(package);
            return true;
        });

    if (!ok) {
        m_viewer->showErrorState(errorMessage);
        showPackageError(errorMessage);
    }
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

bool MainWindow::runWithLoadingDialog(const QString &title, const std::function<bool(QLabel *, QProgressBar *)> &task)
{
    QDialog dialog(this);
    dialog.setWindowTitle(title);
    dialog.setModal(true);
    dialog.setWindowFlags(dialog.windowFlags() & ~Qt::WindowContextHelpButtonHint);
    dialog.setMinimumWidth(420);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(12);

    auto *titleLabel = new QLabel(QStringLiteral("正在处理，请稍候..."), &dialog);
    titleLabel->setStyleSheet(QStringLiteral("font-size: 12pt; font-weight: 700;"));

    auto *messageLabel = new QLabel(QStringLiteral("准备开始..."), &dialog);
    messageLabel->setWordWrap(true);

    auto *progressBar = new QProgressBar(&dialog);
    progressBar->setTextVisible(false);
    progressBar->setRange(0, 0);

    layout->addWidget(titleLabel);
    layout->addWidget(messageLabel);
    layout->addWidget(progressBar);

    dialog.show();
    qApp->processEvents(QEventLoop::ExcludeUserInputEvents);

    const bool ok = task(messageLabel, progressBar);

    dialog.accept();
    qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
    return ok;
}
