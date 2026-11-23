#include <QApplication>
#include <QPalette>
#include <QStyleFactory>
#include "../ui/MainWindow.h"
int main(int argc, char** argv) {
    QApplication app(argc,argv);
    app.setApplicationName("SOFIT");
    app.setApplicationVersion(QStringLiteral(SOFIT_VERSION));
    app.setOrganizationName("SOFIT Community");
    app.setStyle(QStyleFactory::create("Fusion"));
    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#202833"));
    palette.setColor(QPalette::WindowText, QColor("#ebf1f7"));
    palette.setColor(QPalette::Base, QColor("#141a22"));
    palette.setColor(QPalette::AlternateBase, QColor("#1b2430"));
    palette.setColor(QPalette::Text, QColor("#ebf1f7"));
    palette.setColor(QPalette::Button, QColor("#293544"));
    palette.setColor(QPalette::ButtonText, QColor("#ebf1f7"));
    palette.setColor(QPalette::Highlight, QColor("#77d0e6"));
    palette.setColor(QPalette::HighlightedText, QColor("#101923"));
    palette.setColor(QPalette::ToolTipBase, QColor("#293544"));
    palette.setColor(QPalette::ToolTipText, QColor("#ebf1f7"));
    palette.setColor(QPalette::PlaceholderText, QColor("#a0aebd"));
    for (const auto role : {QPalette::Text, QPalette::ButtonText, QPalette::WindowText})
        palette.setColor(QPalette::Disabled, role, QColor("#7e8e9f"));
    app.setPalette(palette);
    MainWindow window;
    window.show();
    return app.exec();
}
