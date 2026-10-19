#include "window.hpp"
#include <QApplication>
#include <QCommandLineParser>
#include <QComboBox>
#include <QStackedWidget>
#include <QTimer>
#include <QLabel>
#include <QLineEdit>
#include <QFileInfo>
#include <QPushButton>
#include <QTableWidget>
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
            const auto* tabs = window.findChild<QStackedWidget*>(QStringLiteral("pageStack"));
            const bool profilesReady = games && games->findData(QStringLiteral("warcraft3")) >= 0 &&
                games->findData(QStringLiteral("dst")) >= 0 && games->findData(QStringLiteral("overcooked2")) >= 0;
            const bool detectionReady = window.findChild<QLabel*>(QStringLiteral("installationStatus")) &&
                window.findChild<QLabel*>(QStringLiteral("serviceStatus")) && window.findChild<QLabel*>(QStringLiteral("localApiStatus"));
            const bool installerReady = window.findChild<QPushButton*>(QStringLiteral("ztInstallButton")) &&
                window.findChild<QPushButton*>(QStringLiteral("ztUninstallButton")) && window.findChild<QLabel*>(QStringLiteral("ztInstallerStatus"));
            bool bundledReady = true;
#ifdef Q_OS_WIN
            const auto bundled = QCoreApplication::applicationDirPath()+QStringLiteral("/prerequisites/ZeroTier-One.msi");
            if (QFileInfo::exists(bundled)) {
                const auto* path = window.findChild<QLineEdit*>(QStringLiteral("ztMsiPath"));
                bundledReady = path && QFileInfo(path->text()).canonicalFilePath() == QFileInfo(bundled).canonicalFilePath();
            }
#endif
            const bool networkReady = window.findChild<QTableWidget*>(QStringLiteral("ztNetworksTable")) &&
                window.findChild<QPushButton*>(QStringLiteral("ztJoinButton")) && window.findChild<QPushButton*>(QStringLiteral("ztLeaveButton"));
            const auto* sidebarVersion = window.findChild<QLabel*>(QStringLiteral("clientVersionLabel"));
            const auto* aboutVersion = window.findChild<QLabel*>(QStringLiteral("aboutClientVersion"));
            const auto expectedVersion = QStringLiteral("GameLink 客户端 v")+QCoreApplication::applicationVersion();
            const bool versionReady = sidebarVersion && aboutVersion && sidebarVersion->text() == expectedVersion &&
                aboutVersion->text() == expectedVersion && window.windowTitle().contains(QStringLiteral("v")+QCoreApplication::applicationVersion());
            app.exit(versionReady && networkReady && bundledReady && installerReady && detectionReady && profilesReady && tabs && tabs->count() == Window::PageCount && QSslSocket::supportsSsl() ? 0 : 1);
        });
    } else {
        window.show();
    }
    return app.exec();
}
