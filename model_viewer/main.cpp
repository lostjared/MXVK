
/**
 * @file main.cpp
 * @brief Starts the Qt model_viewer application.
 */

#include "model_view.hpp"
#include <QApplication>
#include <QFile>

/**
 * @brief Application entry point for the MXMOD model viewer launcher.
 * @param argc Argument count forwarded to QApplication.
 * @param argv Argument vector forwarded to QApplication.
 * @return Qt application exit code.
 */
int main(int argc, char **argv) {
    QApplication app(argc, argv);

    app.setApplicationName("Professional Model Viewer");
    app.setOrganizationName("MXModel");
    app.setOrganizationDomain("mxmodel.io");
    app.setApplicationVersion("1.0.0");

    QFile styleFile(":/styles/data/stylesheet.qss");
    if (styleFile.open(QFile::ReadOnly)) {
        QString style = QLatin1String(styleFile.readAll());
        app.setStyleSheet(style);
        styleFile.close();
    }

    MainWindow viewer;
    viewer.show();

    return app.exec();
}
