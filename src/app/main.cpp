#include <QApplication>
#include "../ui/MainWindow.h"
int main(int argc, char** argv) {
    QApplication app(argc,argv);
    app.setApplicationName("SOFIT");
    app.setOrganizationName("SOFIT Community");
    MainWindow window;
    window.show();
    return app.exec();
}
