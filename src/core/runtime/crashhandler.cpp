#include "src/core/runtime/crashhandler.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageLogContext>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QTextStream>
#include <QThread>

#include <csignal>
#include <cstdlib>
#include <exception>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dbghelp.h>
#endif

namespace
{
QMutex g_logMutex;
QString g_logFilePath;
QString g_dumpDirPath;
QtMessageHandler g_previousHandler = nullptr;
std::terminate_handler g_previousTerminateHandler = nullptr;

QString baseDiagnosticsDirectory()
{
    QString basePath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (basePath.isEmpty()) {
        basePath = QCoreApplication::applicationDirPath();
    }

    QDir dir(basePath);
    dir.mkpath(QStringLiteral("."));
    return dir.absolutePath();
}

QString timestampForFileName()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
}

QString levelToString(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return QStringLiteral("DEBUG");
    case QtInfoMsg:
        return QStringLiteral("INFO");
    case QtWarningMsg:
        return QStringLiteral("WARN");
    case QtCriticalMsg:
        return QStringLiteral("CRITICAL");
    case QtFatalMsg:
        return QStringLiteral("FATAL");
    }

    return QStringLiteral("UNKNOWN");
}

void appendLineToLogFile(const QString &line)
{
    if (g_logFilePath.isEmpty()) {
        return;
    }

    QMutexLocker locker(&g_logMutex);
    QFile file(g_logFilePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return;
    }

    QTextStream stream(&file);
    stream << line << '\n';
    stream.flush();
}

void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    const QString timestamp = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    const QString category = context.category != nullptr ? QString::fromUtf8(context.category) : QStringLiteral("default");
    const QString fileName = context.file != nullptr ? QFileInfo(QString::fromUtf8(context.file)).fileName() : QStringLiteral("<unknown>");
    const QString functionName = context.function != nullptr ? QString::fromUtf8(context.function) : QStringLiteral("<unknown>");
    const quintptr threadId = reinterpret_cast<quintptr>(QThread::currentThreadId());
    const QString line = QStringLiteral("%1 [%2] [tid=%3] [%4] %5:%6 %7 | %8")
                             .arg(timestamp,
                                  levelToString(type))
                             .arg(threadId, 0, 16)
                             .arg(category,
                                  fileName)
                             .arg(context.line)
                             .arg(functionName,
                                  message);

    appendLineToLogFile(line);

    if (g_previousHandler != nullptr) {
        g_previousHandler(type, context, message);
    }

    if (type == QtFatalMsg) {
        std::abort();
    }
}

#ifdef Q_OS_WIN
LONG WINAPI unhandledExceptionFilter(EXCEPTION_POINTERS *exceptionPointers)
{
    const DWORD exceptionCode = exceptionPointers != nullptr && exceptionPointers->ExceptionRecord != nullptr
        ? exceptionPointers->ExceptionRecord->ExceptionCode
        : 0;
    appendLineToLogFile(QStringLiteral("%1 [FATAL] [tid=%2] [crash] Unhandled SEH exception code=0x%3")
                            .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs))
                            .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()), 0, 16)
                            .arg(QString::number(exceptionCode, 16)));

    if (!g_dumpDirPath.isEmpty()) {
        QDir().mkpath(g_dumpDirPath);
        const QString dumpPath = QDir(g_dumpDirPath).filePath(QStringLiteral("crash-%1.dmp").arg(timestampForFileName()));
        HANDLE dumpFile = CreateFileW(reinterpret_cast<LPCWSTR>(dumpPath.utf16()),
                                      GENERIC_WRITE,
                                      0,
                                      nullptr,
                                      CREATE_ALWAYS,
                                      FILE_ATTRIBUTE_NORMAL,
                                      nullptr);
        if (dumpFile != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION exceptionInfo;
            exceptionInfo.ThreadId = GetCurrentThreadId();
            exceptionInfo.ExceptionPointers = exceptionPointers;
            exceptionInfo.ClientPointers = FALSE;

            const BOOL dumpWritten = MiniDumpWriteDump(GetCurrentProcess(),
                                                       GetCurrentProcessId(),
                                                       dumpFile,
                                                       static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory),
                                                       exceptionPointers != nullptr ? &exceptionInfo : nullptr,
                                                       nullptr,
                                                       nullptr);
            CloseHandle(dumpFile);

            appendLineToLogFile(QStringLiteral("%1 [FATAL] [tid=%2] [crash] dump=%3 status=%4")
                                    .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs))
                                    .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()), 0, 16)
                                    .arg(dumpPath,
                                         dumpWritten ? QStringLiteral("written") : QStringLiteral("failed")));
        }
    }

    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

void terminateHandler()
{
    const QString reason = QStringLiteral("std::terminate invoked");
    appendLineToLogFile(QStringLiteral("%1 [FATAL] [tid=%2] [terminate] %3")
                            .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs))
                            .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()), 0, 16)
                            .arg(reason));

#ifdef Q_OS_WIN
    RaiseException(0xE0000001, 0, 0, nullptr);
#endif

    if (g_previousTerminateHandler != nullptr) {
        g_previousTerminateHandler();
    }

    std::abort();
}

void signalHandler(int signalNumber)
{
    appendLineToLogFile(QStringLiteral("%1 [FATAL] [tid=%2] [signal] signal=%3")
                            .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs))
                            .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()), 0, 16)
                            .arg(signalNumber));

    std::_Exit(signalNumber);
}
}

namespace CrashHandler
{
void initialize(const QString &applicationName)
{
    Q_UNUSED(applicationName);

    const QString baseDirPath = baseDiagnosticsDirectory();
    const QString logsDirPath = QDir(baseDirPath).filePath(QStringLiteral("logs"));
    g_dumpDirPath = QDir(baseDirPath).filePath(QStringLiteral("crashdumps"));
    QDir().mkpath(logsDirPath);
    QDir().mkpath(g_dumpDirPath);

    g_logFilePath = QDir(logsDirPath).filePath(QStringLiteral("session-%1.log").arg(timestampForFileName()));

    g_previousHandler = qInstallMessageHandler(messageHandler);
    g_previousTerminateHandler = std::set_terminate(terminateHandler);

#ifdef Q_OS_WIN
    SetUnhandledExceptionFilter(unhandledExceptionFilter);
#endif

    std::signal(SIGABRT, signalHandler);
    std::signal(SIGILL, signalHandler);
    std::signal(SIGINT, signalHandler);
    std::signal(SIGSEGV, signalHandler);
    std::signal(SIGTERM, signalHandler);

    qInfo().noquote() << QStringLiteral("Diagnostics initialized. logDir=%1 dumpDir=%2")
                             .arg(logsDirPath, g_dumpDirPath);
}

QString logDirectoryPath()
{
    return QDir(baseDiagnosticsDirectory()).filePath(QStringLiteral("logs"));
}

QString dumpDirectoryPath()
{
    return QDir(baseDiagnosticsDirectory()).filePath(QStringLiteral("crashdumps"));
}
}
