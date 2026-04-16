#include "mainwindow.h"
#include "src/core/runtime/crashhandler.h"

#include <QApplication>
#include <QLoggingCategory>
#include <QtGlobal>
#include <QSurfaceFormat>
#include <QVTKOpenGLNativeWidget.h>

int main(int argc, char *argv[])
{
    QSurfaceFormat::setDefaultFormat(QVTKOpenGLNativeWidget::defaultFormat());

    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("DicomViewer"));
    QApplication::setApplicationName(QStringLiteral("DicomViewerSkeleton"));

#if defined(MEDPRO_ENABLE_QT_FILE_LOGGING) || defined(MEDPRO_ENABLE_CRASH_HANDLER)
    qSetMessagePattern(QStringLiteral("%{time yyyy-MM-dd HH:mm:ss.zzz} [%{type}] [tid=%{threadid}] [%{category}] %{file}:%{line} %{function} | %{message}"));
#endif

#ifdef MEDPRO_ENABLE_CRASH_HANDLER
    CrashHandler::initialize(QApplication::applicationName());
#endif

    QLoggingCategory::setFilterRules(QStringLiteral(
        "*.debug=true\n"
        "*.info=true\n"
        "qt.*.debug=false\n"));

    MainWindow window;
    window.show();

    return app.exec();
}
