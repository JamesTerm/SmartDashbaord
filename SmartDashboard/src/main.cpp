#include "app/main_window.h"

#include <QApplication>

int main(int argc, char* argv[])
{
    // Qt application bootstrap: create app object, show main window, run event loop.
    QApplication app(argc, argv);

    MainWindow window;
    window.show();

    return app.exec();
}
