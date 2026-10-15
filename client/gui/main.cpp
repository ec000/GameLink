#include "window.hpp"
#include <QApplication>
#include <QCommandLineParser>
#include <QComboBox>
#include <QTabWidget>
#include <QTimer>
#include <QSslSocket>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("GameLink"));
    QCoreApplication::setApplicationName(QStringLiteral("GameLink"));
    QCommandLineParser parser;
    parser.addHelpOption();
    QCommandLineOption selfTest(QStringLiteral("self-test"),
        QStringLiteral("Check GUI runtime and bundled profiles, then exit without network changes."));
    parser.addOption(selfTest);
    parser.process(app);
    Window window;
    if (parser.isSet(selfTest)) {
        QTimer::singleShot(0, &app, [&] {
            const auto* games = window.findChild<QComboBox*>(QStringLiteral("gameSelector"));
            const auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
            app.exit(games && games->count() == 3 && tabs && tabs->count() == 5 && QSslSocket::supportsSsl() ? 0 : 1);
        });
    } else {
        window.show();
    }
    return app.exec();
}
