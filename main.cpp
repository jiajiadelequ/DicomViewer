#include "mainwindow.h"

#include <QApplication>
#include <QSurfaceFormat>
#include <QVTKOpenGLNativeWidget.h>

int main(int argc, char *argv[])
{
    QSurfaceFormat::setDefaultFormat(QVTKOpenGLNativeWidget::defaultFormat());

    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("DicomViewer"));
    QApplication::setApplicationName(QStringLiteral("DicomViewerSkeleton"));

    MainWindow window;
    window.show();

    return app.exec();
}
