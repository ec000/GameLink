#include "window.hpp"
#include <QApplication>
#include <QCommandLineParser>
#include <QComboBox>
#include <QTabWidget>
#include <QTimer>
#include <QLabel>
#include <QSslSocket>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("GameLink"));
    QCoreApplication::setApplicationName(QStringLiteral("GameLink"));
    QCoreApplication::setApplicationVersion(QString::fromLatin1(GAMELINK_VERSION));
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption selfTest(QStringLiteral("self-test"),
        QStringLiteral("Check GUI runtime and bundled profiles, then exit without network changes."));
    parser.addOption(selfTest);
    parser.process(app);
    Window window;
    if (parser.isSet(selfTest)) {
        QTimer::singleShot(0, &app, [&] {
            const auto* games = window.findChild<QComboBox*>(QStringLiteral("gameSelector"));
            const auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
            const bool profilesReady = games && games->findData(QStringLiteral("warcraft3")) >= 0 &&
                games->findData(QStringLiteral("dst")) >= 0 && games->findData(QStringLiteral("overcooked2")) >= 0;
            const bool detectionReady = window.findChild<QLabel*>(QStringLiteral("installationStatus")) &&
                window.findChild<QLabel*>(QStringLiteral("serviceStatus")) && window.findChild<QLabel*>(QStringLiteral("localApiStatus"));
            app.exit(detectionReady && profilesReady && tabs && tabs->count() == 5 && QSslSocket::supportsSsl() ? 0 : 1);
        });
    } else {
        window.show();
    }
    return app.exec();
}
