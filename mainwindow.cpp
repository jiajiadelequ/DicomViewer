#include "mainwindow.h"

#include "src/core/studyloader.h"
#include "src/view/fourpaneviewer.h"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDebug>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QPointer>
#include <QProgressBar>
#include <QSettings>
#include <QStatusBar>
#include <QStringList>
#include <QVBoxLayout>

#include <QtConcurrent/QtConcurrentRun>

#include <gdcmVersion.h>
#include <itkVersion.h>
#include <vtkVersion.h>
#include <zlib.h>

namespace
{
constexpr int kMaxRecentEntries = 10;
constexpr const char kLastOpenDirectoryKey[] = "paths/lastOpenDirectory";
constexpr const char kRecentEntriesKey[] = "paths/recentEntries";
constexpr const char kRecentEntryPathKey[] = "path";
constexpr const char kRecentEntryIsFileKey[] = "sourceIsFile";
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_viewer(new FourPaneViewer(this))
    , m_statusLabel(new QLabel(this))
    , m_openAction(nullptr)
    , m_openImageAction(nullptr)
    , m_loadWatcher(new QFutureWatcher<StudyLoadResult>(this))
    , m_loadingDialog(nullptr)
    , m_loadingMessageLabel(nullptr)
    , m_loadingProgressBar(nullptr)
    , m_loadingCancelButton(nullptr)
    , m_recentMenu(nullptr)
{
    setCentralWidget(m_viewer);
    createMenus();

    statusBar()->addPermanentWidget(m_statusLabel, 1);
    updateStatusBar(StudyPackage{});
    connect(m_loadWatcher, &QFutureWatcher<StudyLoadResult>::finished, this, &MainWindow::handleStudyLoadFinished);

    resize(1440, 900);
    setWindowTitle(QStringLiteral("Dicom Viewer Workstation"));
}

MainWindow::~MainWindow()
{
    ++m_activeLoadId;
    if (m_loadCancelFlag != nullptr) {
        m_loadCancelFlag->store(true);
    }
    if (m_loadWatcher != nullptr && m_loadWatcher->isRunning()) {
        m_loadWatcher->waitForFinished();
    }
}

namespace
{
QString lastOpenDirectory()
{
    QSettings settings;
    return settings.value(QLatin1String(kLastOpenDirectoryKey)).toString();
}

void rememberLastOpenDirectory(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    QSettings settings;
    settings.setValue(QLatin1String(kLastOpenDirectoryKey), path);
}

QString recentEntryText(const QString &path, bool sourceIsFile)
{
    const QString nativePath = QDir::toNativeSeparators(path);
    return sourceIsFile
        ? QStringLiteral("影像文件: %1").arg(nativePath)
        : QStringLiteral("医学影像包: %1").arg(nativePath);
}
}

void MainWindow::openStudyPackage()
{
    if (m_loadWatcher->isRunning()) {
        qWarning() << "Ignored openStudyPackage request while a load is already running.";
        return;
    }

    const QString rootPath = QFileDialog::getExistingDirectory(
        this,
        QStringLiteral("选择病例包目录"),
        lastOpenDirectory());
    if (rootPath.isEmpty()) {
        qInfo() << "Open study package canceled by user.";
        return;
    }

    qInfo().noquote() << QStringLiteral("Selected study package directory: %1").arg(rootPath);
    rememberLastOpenDirectory(rootPath);
    beginStudyLoad(rootPath, false);
}

void MainWindow::openImageFile()
{
    if (m_loadWatcher->isRunning()) {
        qWarning() << "Ignored openImageFile request while a load is already running.";
        return;
    }

    const QString filePath = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("选择 NIfTI 影像文件"),
        lastOpenDirectory(),
        QStringLiteral("NIfTI Files (*.nii *.nii.gz);;All Files (*.*)"));
    if (filePath.isEmpty()) {
        qInfo() << "Open image file canceled by user.";
        return;
    }

    qInfo().noquote() << QStringLiteral("Selected image file: %1").arg(filePath);
    rememberLastOpenDirectory(QFileInfo(filePath).absolutePath());
    beginStudyLoad(filePath, true);
}

void MainWindow::handleStudyLoadFinished()
{
    if (m_loadingDialog != nullptr) {
        m_loadingDialog->hide();
    }
    if (m_openAction != nullptr) {
        m_openAction->setEnabled(true);
    }
    if (m_openImageAction != nullptr) {
        m_openImageAction->setEnabled(true);
    }

    const StudyLoadResult result = m_loadWatcher->result();
    m_loadCancelFlag.reset();
    if (result.cancelled) {
        qWarning() << "Study load was cancelled.";
        if (!m_currentPackage.isValid()) {
            m_viewer->showEmptyState();
            updateStatusBar(StudyPackage {});
        }
        statusBar()->showMessage(QStringLiteral("病例加载已取消。"), 3000);
        return;
    }

    QString errorMessage = result.errorMessage;
    if (!result.succeeded() || !m_viewer->applyStudyLoadResult(result, &errorMessage)) {
        qCritical().noquote() << QStringLiteral("Study load failed: %1").arg(errorMessage);
        if (!m_currentPackage.isValid()) {
            m_viewer->showErrorState(errorMessage);
            updateStatusBar(StudyPackage {});
        }
        showPackageError(errorMessage);
        return;
    }

    m_currentPackage = result.package;
    rememberRecentEntry(m_lastRequestedSourcePath, m_lastRequestedSourceIsFile);
    updateStatusBar(result.package);
    qInfo().noquote() << QStringLiteral("Study load succeeded. root=%1 models=%2 image=%3")
                             .arg(result.package.rootPath)
                             .arg(result.models.size())
                             .arg(result.imageData != nullptr ? QStringLiteral("yes") : QStringLiteral("no"));
}

void MainWindow::beginStudyLoad(const QString &sourcePath, bool sourceIsFile)
{
    qInfo().noquote() << QStringLiteral("Begin study load. source=%1 sourceIsFile=%2")
                             .arg(sourcePath)
                             .arg(sourceIsFile ? QStringLiteral("true") : QStringLiteral("false"));
    m_lastRequestedSourcePath = sourcePath;
    m_lastRequestedSourceIsFile = sourceIsFile;
    ensureLoadingDialog();
    ++m_activeLoadId;
    m_loadCancelFlag = std::make_shared<std::atomic_bool>(false);

    if (m_openAction != nullptr) {
        m_openAction->setEnabled(false);
    }
    if (m_openImageAction != nullptr) {
        m_openImageAction->setEnabled(false);
    }

    if (!m_currentPackage.isValid()) {
        m_viewer->showLoadingState(QStringLiteral("正在后台读取影像和模型数据..."));
    }

    const QString loadingSourceText = sourceIsFile
        ? QStringLiteral("正在准备加载影像文件...\n%1").arg(sourcePath)
        : QStringLiteral("正在准备加载病例目录...\n%1").arg(sourcePath);
    updateLoadingProgress(loadingSourceText, 0);
    m_loadingCancelButton->setEnabled(true);
    m_loadingCancelButton->setText(QStringLiteral("取消"));
    m_loadingDialog->show();
    m_loadingDialog->raise();
    m_loadingDialog->activateWindow();

    const int loadId = m_activeLoadId;
    const std::shared_ptr<std::atomic_bool> cancelFlag = m_loadCancelFlag;
    const QPointer<MainWindow> guardedThis(this);
    StudyLoadFeedback feedback;
    feedback.isCancelled = [cancelFlag]() {
        return cancelFlag != nullptr && cancelFlag->load();
    };
    feedback.reportProgress = [guardedThis, loadId](const QString &message, int percent) {
        if (guardedThis.isNull()) {
            return;
        }

        QMetaObject::invokeMethod(guardedThis.data(),
                                  [guardedThis, loadId, message, percent]() {
                                      if (guardedThis.isNull() || loadId != guardedThis->m_activeLoadId) {
                                          return;
                                      }

                                      guardedThis->updateLoadingProgress(message, percent);
                                  },
                                  Qt::QueuedConnection);
    };

    m_loadWatcher->setFuture(QtConcurrent::run([sourcePath, sourceIsFile, feedback]() {
        try {
            return sourceIsFile
                ? StudyLoader::loadFromFile(sourcePath, feedback)
                : StudyLoader::loadFromDirectory(sourcePath, feedback);
        } catch (const std::exception &ex) {
            qCritical().noquote() << QStringLiteral("Unhandled exception during background study load: %1")
                                         .arg(QString::fromLocal8Bit(ex.what()));
            StudyLoadResult result;
            result.errorMessage = QStringLiteral("后台加载发生未处理异常: %1").arg(QString::fromLocal8Bit(ex.what()));
            return result;
        } catch (...) {
            qCritical() << "Unhandled unknown exception during background study load.";
            StudyLoadResult result;
            result.errorMessage = QStringLiteral("后台加载发生未知未处理异常。");
            return result;
        }
    }));
}

void MainWindow::ensureLoadingDialog()
{
    if (m_loadingDialog != nullptr) {
        return;
    }

    m_loadingDialog = new QDialog(this);
    m_loadingDialog->setWindowTitle(QStringLiteral("正在加载数据"));
    m_loadingDialog->setWindowModality(Qt::WindowModal);
    m_loadingDialog->setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint);
    m_loadingDialog->setMinimumWidth(420);

    auto *layout = new QVBoxLayout(m_loadingDialog);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(12);

    auto *titleLabel = new QLabel(QStringLiteral("正在后台处理，请稍候..."), m_loadingDialog);
    titleLabel->setStyleSheet(QStringLiteral("font-size: 12pt; font-weight: 700;"));

    m_loadingMessageLabel = new QLabel(QStringLiteral("正在读取影像、模型数据..."), m_loadingDialog);
    m_loadingMessageLabel->setWordWrap(true);

    m_loadingProgressBar = new QProgressBar(m_loadingDialog);
    m_loadingProgressBar->setTextVisible(true);
    m_loadingProgressBar->setRange(0, 100);

    m_loadingCancelButton = new QPushButton(QStringLiteral("取消"), m_loadingDialog);
    connect(m_loadingCancelButton, &QPushButton::clicked, this, &MainWindow::cancelStudyLoad);

    layout->addWidget(titleLabel);
    layout->addWidget(m_loadingMessageLabel);
    layout->addWidget(m_loadingProgressBar);
    layout->addWidget(m_loadingCancelButton);
}

void MainWindow::cancelStudyLoad()
{
    if (m_loadWatcher == nullptr || !m_loadWatcher->isRunning() || m_loadCancelFlag == nullptr) {
        return;
    }

    qWarning() << "Cancel requested for current study load.";
    m_loadCancelFlag->store(true);
    m_loadingCancelButton->setEnabled(false);
    m_loadingCancelButton->setText(QStringLiteral("正在取消..."));
    if (m_loadingMessageLabel != nullptr) {
        m_loadingMessageLabel->setText(QStringLiteral("正在取消当前加载任务，请稍候..."));
    }
}

void MainWindow::updateLoadingProgress(const QString &message, int percent)
{
    if (m_loadingMessageLabel != nullptr) {
        m_loadingMessageLabel->setText(message);
    }
    if (m_loadingProgressBar != nullptr) {
        m_loadingProgressBar->setValue(qBound(0, percent, 100));
    }
}

void MainWindow::createMenus()
{
    auto *fileMenu = menuBar()->addMenu(QStringLiteral("文件"));

    m_openImageAction = new QAction(QStringLiteral("打开影像文件"), this);
    m_openImageAction->setShortcut(QKeySequence::Open);
    connect(m_openImageAction, &QAction::triggered, this, &MainWindow::openImageFile);
    fileMenu->addAction(m_openImageAction);

    m_openAction = new QAction(QStringLiteral("打开病例包目录"), this);
    connect(m_openAction, &QAction::triggered, this, &MainWindow::openStudyPackage);
    fileMenu->addAction(m_openAction);

    m_recentMenu = fileMenu->addMenu(QStringLiteral("最近浏览过的医学影像包"));
    refreshRecentEntriesMenu();

    fileMenu->addSeparator();

    auto *exitAction = new QAction(QStringLiteral("退出"), this);
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, this, &QWidget::close);
    fileMenu->addAction(exitAction);
}

void MainWindow::refreshRecentEntriesMenu()
{
    if (m_recentMenu == nullptr) {
        return;
    }

    m_recentMenu->clear();

    QSettings settings;
    const QVariantList entries = settings.value(QLatin1String(kRecentEntriesKey)).toList();
    if (entries.isEmpty()) {
        auto *emptyAction = m_recentMenu->addAction(QStringLiteral("暂无最近记录"));
        emptyAction->setEnabled(false);
        return;
    }

    for (const QVariant &entryValue : entries) {
        const QVariantMap entry = entryValue.toMap();
        const QString path = entry.value(QLatin1String(kRecentEntryPathKey)).toString();
        const bool sourceIsFile = entry.value(QLatin1String(kRecentEntryIsFileKey)).toBool();
        if (path.isEmpty()) {
            continue;
        }

        auto *action = m_recentMenu->addAction(recentEntryText(path, sourceIsFile));
        action->setData(entry);
        connect(action, &QAction::triggered, this, &MainWindow::openRecentEntry);
    }

    if (m_recentMenu->isEmpty()) {
        auto *emptyAction = m_recentMenu->addAction(QStringLiteral("暂无最近记录"));
        emptyAction->setEnabled(false);
    }
}

void MainWindow::rememberRecentEntry(const QString &sourcePath, bool sourceIsFile)
{
    if (sourcePath.isEmpty()) {
        return;
    }

    QSettings settings;
    QVariantList entries = settings.value(QLatin1String(kRecentEntriesKey)).toList();
    QVariantList updatedEntries;
    QVariantMap newEntry;
    newEntry.insert(QLatin1String(kRecentEntryPathKey), sourcePath);
    newEntry.insert(QLatin1String(kRecentEntryIsFileKey), sourceIsFile);
    updatedEntries.push_back(newEntry);

    for (const QVariant &entryValue : entries) {
        const QVariantMap entry = entryValue.toMap();
        if (entry.value(QLatin1String(kRecentEntryPathKey)).toString() == sourcePath
            && entry.value(QLatin1String(kRecentEntryIsFileKey)).toBool() == sourceIsFile) {
            continue;
        }
        updatedEntries.push_back(entry);
        if (updatedEntries.size() >= kMaxRecentEntries) {
            break;
        }
    }

    settings.setValue(QLatin1String(kRecentEntriesKey), updatedEntries);
    refreshRecentEntriesMenu();
}

void MainWindow::openRecentEntry()
{
    if (m_loadWatcher->isRunning()) {
        qWarning() << "Ignored openRecentEntry request while a load is already running.";
        return;
    }

    const auto *action = qobject_cast<QAction *>(sender());
    if (action == nullptr) {
        return;
    }

    const QVariantMap entry = action->data().toMap();
    const QString sourcePath = entry.value(QLatin1String(kRecentEntryPathKey)).toString();
    const bool sourceIsFile = entry.value(QLatin1String(kRecentEntryIsFileKey)).toBool();
    if (sourcePath.isEmpty()) {
        return;
    }

    const QFileInfo fileInfo(sourcePath);
    const bool exists = sourceIsFile ? fileInfo.isFile() : fileInfo.isDir();
    if (!exists) {
        qWarning().noquote() << QStringLiteral("Recent entry no longer exists: %1").arg(sourcePath);
        QMessageBox::warning(this,
                             QStringLiteral("最近记录不可用"),
                             QStringLiteral("路径已不存在，无法打开：\n%1").arg(QDir::toNativeSeparators(sourcePath)));
        return;
    }

    qInfo().noquote() << QStringLiteral("Opening recent entry: %1").arg(sourcePath);
    rememberLastOpenDirectory(sourceIsFile ? fileInfo.absolutePath() : sourcePath);
    beginStudyLoad(sourcePath, sourceIsFile);
}

void MainWindow::updateStatusBar(const StudyPackage &package)
{
    QStringList parts;
    parts << QStringLiteral("VTK %1").arg(QString::fromLatin1(vtkVersion::GetVTKVersion()));
    parts << QStringLiteral("ITK %1").arg(QString::fromLatin1(itk::Version::GetITKVersion()));
    parts << QStringLiteral("GDCM %1").arg(QString::fromLatin1(gdcm::Version::GetVersion()));
    parts << QStringLiteral("ZLIB %1").arg(QString::fromLatin1(zlibVersion()));

    if (package.isValid()) {
        parts << QStringLiteral("当前来源: %1").arg(package.rootPath);
    } else {
        parts << QStringLiteral("当前来源: 未加载");
    }

    m_statusLabel->setText(parts.join(QStringLiteral(" | ")));
}

void MainWindow::showPackageError(const QString &message)
{
    QMessageBox::warning(
        this,
        QStringLiteral("无法加载数据"),
        message.isEmpty() ? QStringLiteral("未找到可识别的影像或模型数据。") : message);
}
