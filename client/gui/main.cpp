#include "window.hpp"
#include <QApplication>
int main(int argc,char** argv){
 QApplication app(argc,argv);
 QCoreApplication::setOrganizationName(QStringLiteral("GameLink"));QCoreApplication::setApplicationName(QStringLiteral("GameLink"));
 Window window;window.show();return app.exec();
}
