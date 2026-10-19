#include "transport.hpp"
#include "window.hpp"
#include "zerotier_environment.hpp"
#include <QFile>
#include <QDir>
#include <QFontMetrics>
#include <QComboBox>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QStackedWidget>
#include <QScrollArea>
#include <QScrollBar>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTest>
#include <QTimer>

#define S(x) QStringLiteral(x)

struct Request {
    QByteArray method, path, body;
    QHash<QByteArray, QByteArray> headers;
};

// Only localhost. Implements enough HTTP for the real Qt network stack to be exercised.
class HttpFixture : public QObject {
public:
    QTcpServer server;
    std::function<void(const Request&, QTcpSocket*)> handler;
    QList<Request> requests;

    HttpFixture() {
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (server.hasPendingConnections()) {
                auto* socket = server.nextPendingConnection();
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                    if (socket->property("handled").toBool()) return;
                    auto bytes = socket->property("buffer").toByteArray() + socket->readAll();
                    socket->setProperty("buffer", bytes);
                    const auto split = bytes.indexOf("\r\n\r\n");
                    if (split < 0) return;
                    const auto lines = bytes.left(split).split('\n');
                    const auto first = lines.first().trimmed().split(' ');
                    if (first.size() < 2) return;
                    Request request{first[0], first[1], {}, {}};
                    for (const auto& line : lines.mid(1)) {
                        const auto colon = line.indexOf(':');
                        if (colon > 0) request.headers[line.left(colon).trimmed().toLower()] = line.mid(colon + 1).trimmed();
                    }
                    const int length = request.headers.value("content-length").toInt();
                    if (bytes.size() - split - 4 < length) return;
                    request.body = bytes.mid(split + 4, length);
                    socket->setProperty("handled", true);
                    requests.append(request);
                    if (handler) handler(request, socket);
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }
    bool listen() { return server.listen(QHostAddress::LocalHost, 0); }
    QString origin() const { return S("http://127.0.0.1:%1").arg(server.serverPort()); }
    static void reply(QTcpSocket* socket, const QByteArray& data, int status = 200,
                      const QByteArray& extraHeaders = {}) {
        socket->write(QByteArray("HTTP/1.1 ") + QByteArray::number(status) + " Fixture\r\nContent-Type: application/json\r\nContent-Length: " +
                      QByteArray::number(data.size()) + "\r\nConnection: close\r\n" + extraHeaders + "\r\n" + data);
        socket->disconnectFromHost();
    }
};

class MockInstallerPlatform final : public gamelink::InstallerPlatform {
public:
    const QString productCode = S("{12345678-1234-1234-1234-123456789ABC}");
    gamelink::ProductInventory installed{true, {{productCode, S("ZeroTier One"), S("MOCK 1.0")}}, {}};
    gamelink::InstallerPackage package{true, S("C:/Mock/ZeroTier.msi"), productCode, S("ZeroTier One"), S("MOCK 1.0"), S("ZeroTier, Inc."), {}};
    int calls = 0;
    gamelink::InstallerAction action = gamelink::InstallerAction::Install;
    gamelink::InstallerDone pending;
    gamelink::InstallerPackage inspect(const QString& path) override {
        auto result = package;
        if (path != package.path) { result.valid = false; result.error = S("[MOCK_MSI_INVALID] unsigned/invalid test package"); }
        return result;
    }
    gamelink::ProductInventory inventory() override { return installed; }
    void execute(gamelink::InstallerAction requested, const QString&, const QString&, quintptr, QObject*, gamelink::InstallerDone done) override {
        ++calls; action = requested; pending = std::move(done);
    }
    void finish(gamelink::InstallerResult result) { auto callback = std::move(pending); if (callback) callback(result); }
};

class QtClientTests : public QObject {
    Q_OBJECT
    QTemporaryDir settingsDirectory;
    const QString apiToken = S("test-gamelink-token-0123456789-abcdef");
    const QByteArray localToken = "test-local-zerotier-token-0123456789";
private slots:
    void initTestCase() {
        QVERIFY(settingsDirectory.isValid());
        QCoreApplication::setOrganizationName(S("GameLinkTests"));
        QCoreApplication::setApplicationName(S("QtClientTests"));
        QCoreApplication::setApplicationVersion(QString::fromLatin1(GAMELINK_VERSION));
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    }
    void init() { QSettings settings; settings.clear(); settings.sync(); }

    void officialCredentialsMigrateMissingPlaceholderAndSkipStaleCopy() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto paths=gamelink::windowsCredentialPaths(directory.path(),{});
        QCOMPARE(paths.size(),2);
        QVERIFY(paths[0].endsWith(S("/ZeroTier/One/authtoken.secret")));
        QVERIFY(paths[1].endsWith(S("/ZeroTier/authtoken.secret")));
        QVERIFY(gamelink::windowsCredentialPaths({},{}).isEmpty());
        for(int i=0;i<paths.size();++i){
            QVERIFY(QDir().mkpath(QFileInfo(paths[i]).absolutePath()));
            QFile file(paths[i]);QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(i==0?QByteArray("stale-local-token"):localToken);file.close();
        }
        HttpFixture fixture;QVERIFY(fixture.listen());
        fixture.handler=[&](const Request& request,QTcpSocket* socket){
            QCOMPARE(request.method,QByteArray("GET"));
            if(request.headers.value("x-zt1-auth")!=localToken){HttpFixture::reply(socket,"private-rejection",403);return;}
            if(request.path=="/status")HttpFixture::reply(socket,R"({"address":"52f959093f","online":true,"clock":100000,"version":"1.16.2"})");
            else if(request.path=="/peer")HttpFixture::reply(socket,"[]");
            else if(request.path=="/network")HttpFixture::reply(socket,R"([{"id":"17beb7d23da4da9f","name":"ec_network","status":"OK","assignedAddresses":["10.44.198.25/24"]}])");
            else QFAIL("Unexpected request");
        };
        QSettings settings;settings.setValue(S("ztPort"),fixture.server.serverPort());
        settings.setValue(S("ztTokenFile"),gamelink::legacyGameLinkTokenPath());settings.sync();
        Window window([]{return gamelink::EnvironmentReport{};},gamelink::nativeInstallerPlatform(),[paths]{return paths;});
        auto* token=window.findChild<QLineEdit*>(S("ztTokenFile"));
        auto* node=window.findChild<QLabel*>(S("ztNodeStatus"));
        auto* table=window.findChild<QTableWidget*>(S("ztNetworksTable"));
        auto* events=window.findChild<QPlainTextEdit*>(S("events"));
        QVERIFY(token && node && table && events);
        QTRY_VERIFY(node->text().contains(S("52f959093f")));
        QCOMPARE(token->text(),paths[1]);QCOMPARE(table->rowCount(),1);
        QCOMPARE(table->item(0,0)->text(),S("17beb7d23da4da9f"));
        QVERIFY(table->item(0,3)->text().contains(S("10.44.198.25")));
        settings.sync();QCOMPARE(settings.value(S("ztTokenFile")).toString(),paths[1]);
        for(const auto& key:settings.allKeys())QVERIFY(!settings.value(key).toString().contains(QString::fromUtf8(localToken)));
        QVERIFY(!events->toPlainText().contains(QString::fromUtf8(localToken)));
        QVERIFY(!events->toPlainText().contains(S("stale-local-token")));
        QVERIFY(!events->toPlainText().contains(S("private-rejection")));
    }

    void credentialDetectionPreservesCustomPathUntilExplicitAndDoesNotAcceptRejectedToken() {
        HttpFixture fixture;QVERIFY(fixture.listen());
        QTemporaryFile tokenFile;QVERIFY(tokenFile.open());tokenFile.write(localToken);tokenFile.flush();
        bool reject=true;
        fixture.handler=[&](const Request& request,QTcpSocket* socket){
            QCOMPARE(request.method,QByteArray("GET"));
            if(reject){HttpFixture::reply(socket,"sensitive response",401);return;}
            if(request.path=="/status")HttpFixture::reply(socket,R"({"address":"52f959093f","online":false,"clock":100000})");
            else HttpFixture::reply(socket,"[]");
        };
        const auto custom=settingsDirectory.filePath(S("custom-missing-token"));
        QSettings settings;settings.setValue(S("ztTokenFile"),custom);settings.setValue(S("ztPort"),fixture.server.serverPort());settings.sync();
        Window window([]{return gamelink::EnvironmentReport{};},gamelink::nativeInstallerPlatform(),[&]{return QStringList{tokenFile.fileName()};});
        auto* token=window.findChild<QLineEdit*>(S("ztTokenFile"));
        auto* detect=window.findChild<QPushButton*>(S("ztDetectCredentialsButton"));
        auto* status=window.findChild<QLabel*>(S("localApiStatus"));
        auto* guide=window.findChild<QLabel*>(S("ztCredentialStatus"));
        QVERIFY(token && detect && status && guide);
        QCOMPARE(token->text(),custom);QVERIFY(fixture.requests.isEmpty());
        QVERIFY(status->text().contains(S("[TOKEN_MISSING]")));
        QVERIFY(guide->text().contains(S("不代表 ZeroTier 已离线")));
        detect->click();QTRY_VERIFY(status->text().contains(S("[AUTH_FAILED]")));
        QCOMPARE(token->text(),custom);QVERIFY(detect->isEnabled());
        settings.sync();QCOMPARE(settings.value(S("ztTokenFile")).toString(),custom);
        reject=false;detect->click();
        QTRY_VERIFY(status->text().contains(S("认证成功")));
        QCOMPARE(token->text(),tokenFile.fileName());
        QVERIFY(window.findChild<QLabel*>(S("ztNodeStatus"))->text().contains(S("离线")));
    }

    void sidebarNavigationAndSmallWindowRemainUsable() {
        Window window([] { return gamelink::EnvironmentReport{}; });
        auto* stack = window.findChild<QStackedWidget*>(S("pageStack")); QVERIFY(stack);
        QCOMPARE(stack->count(), int(Window::PageCount));
        window.setWindowTitle(S("GameLink · UI preview / MOCK environment"));
        window.show(); QCoreApplication::processEvents();
        // Catch a fontless platform backend before accepting square-glyph previews.
        const QFontMetrics fontMetrics(window.font());
        QVERIFY(fontMetrics.inFontUcs4(U'G'));
        QVERIFY(fontMetrics.inFontUcs4(U'联'));
        const QStringList snapshots = {S("overview"), S("games"), S("room"), S("zerotier"), S("diagnostics"), S("settings")};
        for (int i = 0; i < stack->count(); ++i) {
            auto* navigation = window.findChild<QPushButton*>(S("navigation%1").arg(i)); QVERIFY(navigation);
            QTest::mouseClick(navigation, Qt::LeftButton);
            QCOMPARE(stack->currentIndex(), i); QVERIFY(navigation->isChecked());
            auto* heading = window.findChild<QLabel*>(S("pageTitle%1").arg(i)); QVERIFY(heading);
            QVERIFY(heading->isVisible());
            heading->setText(heading->text() + S(" · UI 预览"));
            QCoreApplication::processEvents();
            QVERIFY(window.grab().save(QCoreApplication::applicationDirPath() + S("/ui-%1.png").arg(snapshots[i])));
            window.resize(960, 680); QCoreApplication::processEvents();
            auto* scroll = qobject_cast<QScrollArea*>(stack->currentWidget()); QVERIFY(scroll);
            // Vertical scrolling is intentional; fields and actions must fit horizontally.
            QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
            const auto inputs = scroll->findChildren<QLineEdit*>();
            for (const auto* input : inputs) {
                QVERIFY(input->width() >= 150);
                const auto position = input->mapTo(scroll->widget(), QPoint(0,0));
                QVERIFY(position.x() >= 0);
                QVERIFY(position.x() + input->width() <= scroll->widget()->width());
            }
            QVERIFY(window.grab().save(QCoreApplication::applicationDirPath() + S("/ui-%1-small.png").arg(snapshots[i])));
            window.resize(1180, 840); QCoreApplication::processEvents();
        }
        auto* homeNavigation = window.findChild<QPushButton*>(S("navigation0")); homeNavigation->click();
        auto* home = window.findChild<QLabel*>(S("homeStatus")); QVERIFY(home);
        QVERIFY(home->isVisible());
    }

    void installerIdentityAndExitCodesAreSpecific() {
        using namespace gamelink;
        QVERIFY(isZeroTierProduct(S("ZeroTier One"), S("ZeroTier, Inc.")));
        QVERIFY(isZeroTierProduct(S("ZeroTier"), S("ZeroTier Inc.")));
        QVERIFY(!isZeroTierProduct(S("ZeroTier One helper"), S("ZeroTier, Inc.")));
        QVERIFY(!isZeroTierProduct(S("ZeroTier One"), S("Unrelated Publisher")));
        QVERIFY(!isZeroTierProduct(S("ZeroTier One"), S("ZeroTier! Inc")));
        QVERIFY(!isZeroTierProduct(S("ZeroTier One"), S("ZeroTier Inc 假冒")));
        QVERIFY(!isZeroTierProduct(S("Other App"), S("ZeroTier, Inc.")));
        QVERIFY(isProductCode(S("{12345678-1234-1234-1234-123456789ABC}")));
        QVERIFY(!isProductCode(S("{12345678-1234-1234-1234-123456789ABC} /quiet")));
        QCOMPARE(windowsInstallerResult(0).status, InstallerStatus::Success);
        QCOMPARE(windowsInstallerResult(3010).status, InstallerStatus::RebootRequired);
        QCOMPARE(windowsInstallerResult(1641).status, InstallerStatus::RebootRequired);
        QCOMPARE(windowsInstallerResult(1223).status, InstallerStatus::Cancelled);
        QCOMPARE(windowsInstallerResult(1602).status, InstallerStatus::Cancelled);
        for (const auto code : {5U,1603U,1605U,1618U,1619U,1620U,1625U,1638U,9999U}) {
            const auto result = windowsInstallerResult(code);
            QCOMPARE(result.status, InstallerStatus::Failed); QCOMPARE(result.code, code);
            QVERIFY(result.message.contains(QString::number(code)));
        }
        QVERIFY(windowsInstallerResult(1618).message.contains(S("另一个安装")));
        QVERIFY(windowsInstallerResult(1625).message.contains(S("策略")));
    }

    void installerRejectsInvalidRequestsAndSerializesOperations() {
        using namespace gamelink;
        auto platform = std::make_shared<MockInstallerPlatform>(); ZeroTierInstaller installer(platform);
        QList<InstallerResult> results; QList<bool> busy;
        connect(&installer,&ZeroTierInstaller::completed,this,[&](const InstallerResult& r){ results.append(r); });
        connect(&installer,&ZeroTierInstaller::busyChanged,this,[&](bool value){ busy.append(value); });
        installer.install(S("unsigned.msi")); QCOMPARE(platform->calls,0); QVERIFY(!installer.busy());
        QVERIFY(results.last().message.contains(S("MOCK_MSI_INVALID")));
        installer.uninstall(S("arbitrary uninstall command")); QCOMPARE(platform->calls,0);
        installer.uninstall(S("{00000000-0000-0000-0000-000000000000}")); QCOMPARE(platform->calls,0);
        installer.install(platform->package.path); QCOMPARE(platform->calls,1); QVERIFY(installer.busy());
        installer.uninstall(platform->productCode); QCOMPARE(platform->calls,1);
        platform->finish(windowsInstallerResult(1223)); QVERIFY(!installer.busy()); QCOMPARE(results.last().status,InstallerStatus::Cancelled);
        installer.uninstall(platform->productCode); QCOMPARE(platform->calls,2); QCOMPARE(platform->action,InstallerAction::Uninstall);
        platform->finish(windowsInstallerResult(3010)); QVERIFY(!installer.busy()); QCOMPARE(results.last().status,InstallerStatus::RebootRequired);
        installer.install(platform->package.path); platform->finish(windowsInstallerResult(0));
        QCOMPARE(results.last().status,InstallerStatus::Success);
        QCOMPARE(busy,QList<bool>({true,false,true,false,true,false}));
        platform->installed.error=S("[MOCK_MSI_ENUM] access denied");
        installer.uninstall(platform->productCode); QCOMPARE(platform->calls,3);
        QVERIFY(results.last().message.contains(S("MOCK_MSI_ENUM")));
        // A delayed platform completion must not access a destroyed manager.
        auto temporary = std::make_unique<ZeroTierInstaller>(platform); temporary->install(platform->package.path);
        QVERIFY(temporary->busy()); temporary.reset(); platform->finish(windowsInstallerResult(0));
    }

    void nativeInstallerRejectsUnsignedPackagesWithoutElevation() {
        using namespace gamelink;
        auto platform=nativeInstallerPlatform();
        const auto invalid=platform->inspect(S("relative/not-an-installer.exe")); QVERIFY(!invalid.valid);
        const auto inventory=platform->inventory();
#ifdef Q_OS_WIN
        QVERIFY(inventory.supported);
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto path=directory.filePath(S("unsigned.msi")); QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("NOT AN MSI"); file.close();
        const auto package=platform->inspect(path); QVERIFY(!package.valid); QVERIFY(package.error.contains(S("MSI_SIGNATURE")));
        bool done=false;
        platform->execute(InstallerAction::Install,path,{},0,this,[&](InstallerResult result){
            QCOMPARE(result.status,InstallerStatus::Failed); QVERIFY(result.message.contains(S("MSI_SIGNATURE"))); done=true;
        });
        QVERIFY(done); // Rejected before ShellExecute/UAC, no real installation.
#else
        QVERIFY(!inventory.supported); QVERIFY(invalid.error.contains(S("PLATFORM_UNSUPPORTED")));
#endif
    }

    void installerUiConfirmsAndKeepsStateUntilCompletion() {
        using namespace gamelink;
        auto platform=std::make_shared<MockInstallerPlatform>();
        Window window([]{ return EnvironmentReport{}; },platform); window.show();
        auto* stack=window.findChild<QStackedWidget*>(S("pageStack")); QVERIFY(stack); stack->setCurrentIndex(Window::ZeroTierPage);
        auto* uninstall=window.findChild<QPushButton*>(S("ztUninstallButton")); QVERIFY(uninstall); QVERIFY(uninstall->isEnabled());
        auto* install=window.findChild<QPushButton*>(S("ztInstallButton")); QVERIFY(install); QVERIFY(!install->isEnabled());
        auto* choose=window.findChild<QPushButton*>(S("ztChooseMsiButton")); QVERIFY(choose); QVERIFY(choose->isEnabled());
        auto* status=window.findChild<QLabel*>(S("ztInstallerStatus")); QVERIFY(status);
        auto answer=[](QMessageBox::StandardButton button) {
            QTimer::singleShot(0,[button]{ auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget()); QVERIFY(box); auto* target=box->button(button); QVERIFY(target); target->click(); });
        };
        answer(QMessageBox::No); uninstall->click(); QCOMPARE(platform->calls,0);
        answer(QMessageBox::Yes); uninstall->click(); QCOMPARE(platform->calls,1);
        QCOMPARE(platform->action,InstallerAction::Uninstall); QVERIFY(!choose->isEnabled()); QVERIFY(!uninstall->isEnabled());
        QVERIFY(status->text().contains(S("Windows Installer 正在运行")));
        answer(QMessageBox::Ok); QVERIFY(!window.close()); // Keeps the operation and locked package alive.
        platform->installed.products.clear(); platform->finish(windowsInstallerResult(3010));
        QVERIFY(choose->isEnabled()); QVERIFY(!uninstall->isEnabled()); QVERIFY(status->text().contains(S("重启")));
        auto* scroll=qobject_cast<QScrollArea*>(stack->currentWidget()); QVERIFY(scroll); scroll->ensureWidgetVisible(choose->parentWidget());
        status->setText(S("MOCK 服务管理测试 · 未操作真实 ZeroTier\n")+status->text());
        QCoreApplication::processEvents();
        QVERIFY(window.grab().save(QCoreApplication::applicationDirPath()+S("/zerotier-installer-fixture.png")));
        QVERIFY(window.close());
    }

    void nativeOfficialMsiIsAcceptedWithoutExecuting() {
#ifdef Q_OS_WIN
        const auto path=qEnvironmentVariable("GAMELINK_TEST_ZT_MSI");
        if(path.isEmpty())QSKIP("Optional pinned official MSI fixture not configured; no installation is executed.");
        const auto package=gamelink::nativeInstallerPlatform()->inspect(path);
        QVERIFY2(package.valid,qPrintable(package.error));
        QCOMPARE(package.name,S("ZeroTier One")); QCOMPARE(package.version,S("1.16.2"));
        QVERIFY(gamelink::isProductCode(package.productCode));
        QVERIFY(package.publisher.contains(S("ZeroTier"),Qt::CaseInsensitive));
        qInfo().noquote()<<S("Read-only official MSI identity: %1 %2; %3; %4").arg(package.name,package.version,package.publisher,package.productCode);
#else
        QSKIP("Authenticode and Windows Installer metadata require Windows; platform-independent mocks run separately.");
#endif
    }

    void credentialsAndJoinPayloadStayOnExpectedEndpoints() {
        HttpFixture fixture; QVERIFY(fixture.listen());
        fixture.handler = [](const Request&, QTcpSocket* socket) { HttpFixture::reply(socket, "{}"); };
        QTemporaryFile tokenFile(settingsDirectory.path() + S("/token-XXXXXX"));
        QVERIFY(tokenFile.open()); tokenFile.write(localToken); tokenFile.flush();
        Transport transport;
        bool localDone = false;
        transport.local(fixture.server.serverPort(), tokenFile.fileName(), S("POST"), S("/network/abcdef1234000001"),
                        {{S("allowManaged"), true}, {S("allowDefault"), false}, {S("allowGlobal"), false}, {S("allowDNS"), false}},
                        [&](QJsonDocument, QString error) { QVERIFY(error.isEmpty()); localDone = true; });
        QTRY_VERIFY_WITH_TIMEOUT(localDone, 4000);
        QCOMPARE(fixture.requests.size(), 1);
        const auto local = fixture.requests[0];
        QCOMPARE(local.method, QByteArray("POST"));
        QCOMPARE(local.headers.value("x-zt1-auth"), localToken);
        QVERIFY(!local.headers.contains("authorization"));
        const auto join = QJsonDocument::fromJson(local.body).object();
        QVERIFY(join[S("allowManaged")].toBool());
        for (const auto& field : {S("allowDefault"), S("allowGlobal"), S("allowDNS")}) {
            QVERIFY(join[field].isBool()); QVERIFY(!join[field].toBool());
        }
        bool roomDone = false;
        transport.room(fixture.origin(), apiToken, S("GET"), S("/api/v1/health"), {},
                       [&](QJsonDocument, QString error) { QVERIFY(error.isEmpty()); roomDone = true; });
        QTRY_VERIFY_WITH_TIMEOUT(roomDone, 4000);
        QCOMPARE(fixture.requests.size(), 2);
        QCOMPARE(fixture.requests[1].headers.value("authorization"), QByteArray("Bearer ") + apiToken.toUtf8());
        QVERIFY(!fixture.requests[1].headers.contains("x-zt1-auth"));
        QVERIFY(!fixture.requests[1].body.contains(localToken));
    }

    void unsafeOriginsAndCredentialsFailBeforeNetwork() {
        Transport transport;
        for (const auto& origin : {S("http://example.com"), S("https://user:pass@example.com"), S("https://example.com/path"), S("https://example.com?token=x")}) {
            bool called = false;
            transport.room(origin, apiToken, S("GET"), S("/api/v1/health"), {},
                           [&](QJsonDocument, QString error) { QVERIFY(!error.isEmpty()); called = true; });
            QVERIFY(called);
        }
        bool called = false;
        transport.room(S("https://example.com"), apiToken + S("\r\nsecret"), S("GET"), S("/api/v1/health"), {},
                       [&](QJsonDocument, QString error) { QVERIFY(!error.isEmpty()); called = true; });
        QVERIFY(called);
    }

    void redirectDoesNotForwardCredentials() {
        HttpFixture sink, redirect; QVERIFY(sink.listen()); QVERIFY(redirect.listen());
        redirect.handler = [&](const Request&, QTcpSocket* socket) {
            HttpFixture::reply(socket, "{}", 302, QByteArray("Location: ") + sink.origin().toUtf8() + "/stolen\r\n");
        };
        Transport transport; bool done = false;
        transport.room(redirect.origin(), apiToken, S("GET"), S("/api/v1/health"), {},
                       [&](QJsonDocument, QString error) { QVERIFY(!error.isEmpty()); done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 4000);
        QCOMPARE(sink.requests.size(), 0);
    }

    void totalDeadlineStopsContinuousSlowResponse() {
        HttpFixture fixture; QVERIFY(fixture.listen());
        fixture.handler = [](const Request&, QTcpSocket* socket) {
            socket->write("HTTP/1.1 200 OK\r\nContent-Length: 1000000\r\n\r\n");
            auto* drip = new QTimer(socket);
            QObject::connect(drip, &QTimer::timeout, socket, [socket] { socket->write(" "); });
            drip->start(50);
        };
        Transport transport; int callbacks = 0; QElapsedTimer elapsed; elapsed.start();
        transport.room(fixture.origin(), apiToken, S("GET"), S("/api/v1/health"), {},
                       [&](QJsonDocument, QString error) { QVERIFY(error.contains(S("[REQUEST_TIMEOUT]"))); ++callbacks; });
        QTRY_COMPARE_WITH_TIMEOUT(callbacks, 1, 5000);
        QVERIFY(elapsed.elapsed() < 4500);
    }

    void malformedOversizedAndErrorBodiesAreNotExposed() {
        HttpFixture fixture; QVERIFY(fixture.listen()); Transport transport;
        const QByteArray secret = "server-sensitive-error-payload";
        for (int kind = 0; kind < 3; ++kind) {
            fixture.handler = [=](const Request&, QTcpSocket* socket) {
                HttpFixture::reply(socket, kind == 0 ? QByteArray("not json") : kind == 1 ? QByteArray(1024 * 1024 + 10, 'x') : secret,
                                   kind == 2 ? 503 : 200);
            };
            bool done = false;
            transport.room(fixture.origin(), apiToken, S("GET"), S("/api/v1/health"), {},
                           [&](QJsonDocument, QString error) {
                               QVERIFY(!error.isEmpty()); QVERIFY(!error.contains(QString::fromUtf8(secret))); done = true;
                           });
            QTRY_VERIFY_WITH_TIMEOUT(done, 4000);
        }
    }

    void serviceEvidenceAndInstallationAreIndependent() {
        using namespace gamelink;
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto executable = directory.filePath(S("ZeroTier test service.exe"));
        QFile binary(executable); QVERIFY(binary.open(QIODevice::WriteOnly)); binary.close();
        QFile portFile(directory.filePath(S("zerotier-one.port")));
        QVERIFY(portFile.open(QIODevice::WriteOnly)); portFile.write("33199\n"); portFile.close();
        ServiceInfo service; service.registered = true; service.state = ServiceState::Stopped; service.executable = executable;
        auto report = inspectInstallation(service, {}, directory.path());
        QCOMPARE(report.installation, InstallationState::Installed);
        QCOMPARE(report.service.state, ServiceState::Stopped);
        QCOMPARE(report.detectedPort, 33199);
        QVERIFY(!QFile::exists(directory.filePath(S("authtoken.secret")))); // Detection does not require a token.
        QVERIFY(QFile::remove(executable));
        QCOMPARE(inspectInstallation(service, {}, directory.path()).installation, InstallationState::BrokenRegistration);
        service.executable.clear();
        QCOMPARE(inspectInstallation(service, {}, directory.path()).installation, InstallationState::Registered);
        service.registered = false; service.state = ServiceState::NotFound;
        QCOMPARE(inspectInstallation(service, {}, directory.path()).installation, InstallationState::DataOnly);
        const auto missing = directory.filePath(S("missing"));
        QCOMPARE(inspectInstallation(service, {}, missing).installation, InstallationState::NotDetected);
        service.state = ServiceState::AccessDenied;
        QCOMPARE(inspectInstallation(service, {}, missing).installation, InstallationState::Unknown);
        QVERIFY(portFile.open(QIODevice::WriteOnly | QIODevice::Truncate)); portFile.write("99999"); portFile.close();
        report = inspectInstallation(service, {}, directory.path());
        QCOMPARE(report.detectedPort, 0); QVERIFY(report.portDetail.contains(S("无效")));
    }

    void nativeServiceStateMappingAndReadOnlyQuery() {
        using namespace gamelink;
        QCOMPARE(windowsServiceState(4), ServiceState::Running);
        QCOMPARE(windowsServiceState(1), ServiceState::Stopped);
        QCOMPARE(windowsServiceState(2), ServiceState::Starting);
        QCOMPARE(windowsServiceState(3), ServiceState::Stopping);
        QCOMPARE(windowsServiceState(5), ServiceState::Changing);
        QCOMPARE(windowsServiceState(6), ServiceState::Changing);
        QCOMPARE(windowsServiceState(7), ServiceState::Paused);
        QCOMPARE(windowsServiceState(99), ServiceState::Unknown);
        QCOMPARE(windowsServiceError(5), ServiceState::AccessDenied);
        QCOMPARE(windowsServiceError(1060), ServiceState::NotFound);
        QCOMPARE(windowsServiceError(1722), ServiceState::Unknown);
        QCOMPARE(serviceExecutable(S("\"C:\\Program Files\\ZeroTier\\One\\zerotier-one_x64.exe\" -s -Tsecret")), S("C:\\Program Files\\ZeroTier\\One\\zerotier-one_x64.exe"));
        QCOMPARE(serviceExecutable(S("C:\\Program Files\\ZeroTier\\One\\zerotier-one_x64.exe -s")), S("C:\\Program Files\\ZeroTier\\One\\zerotier-one_x64.exe"));
        QVERIFY(serviceExecutable(S("\"unterminated")).isEmpty());
#ifdef Q_OS_WIN
        const auto missing = queryWindowsService(S("GameLinkInspectionMissing_9028d92f"));
        QCOMPARE(missing.state, ServiceState::NotFound); QVERIFY(!missing.registered);
        // Query a built-in service without creating, stopping, starting or reconfiguring anything.
        const auto eventLog = queryWindowsService(S("EventLog"));
        QVERIFY(eventLog.registered); QCOMPARE(eventLog.state, ServiceState::Running);
#else
        QCOMPARE(queryWindowsService(S("ZeroTierOneService")).state, ServiceState::Unsupported);
#endif
    }

    void localTokenAndPortErrorsAreDistinct() {
        Transport transport; QString error;
        auto attempt = [&](int port, const QString& file) {
            error.clear();
            transport.local(port, file, S("GET"), S("/status"), {},
                            [&](QJsonDocument, QString message) { error = message; });
        };
        attempt(0, S("missing")); QVERIFY(error.contains(S("[INVALID_PORT]")));
        attempt(9993, {}); QVERIFY(error.contains(S("[TOKEN_PATH_EMPTY]")));
        attempt(9993, settingsDirectory.filePath(S("no-such-token"))); QVERIFY(error.contains(S("[TOKEN_MISSING]")));
        attempt(9993, settingsDirectory.path()); QVERIFY(error.contains(S("[TOKEN_INVALID]")));
        QTemporaryFile empty(settingsDirectory.path() + S("/empty-XXXXXX")); QVERIFY(empty.open());
        attempt(9993, empty.fileName()); QVERIFY(error.contains(S("[TOKEN_INVALID]")));
        empty.write(QByteArray(513, 'x')); empty.flush();
        attempt(9993, empty.fileName()); QVERIFY(error.contains(S("[TOKEN_INVALID]")));
    }

    void localHttpErrorsAreDistinct() {
        HttpFixture fixture; QVERIFY(fixture.listen()); Transport transport;
        QTemporaryFile tokenFile(settingsDirectory.path() + S("/token-XXXXXX"));
        QVERIFY(tokenFile.open()); tokenFile.write(localToken); tokenFile.flush();
        for (const int status : {401, 403, 404, 302}) {
            fixture.handler = [status](const Request&, QTcpSocket* socket) { HttpFixture::reply(socket, "private upstream message", status); };
            bool done = false; QString error;
            transport.local(fixture.server.serverPort(), tokenFile.fileName(), S("GET"), S("/status"), {},
                            [&](QJsonDocument, QString message) { error = message; done = true; });
            QTRY_VERIFY_WITH_TIMEOUT(done, 4000);
            QVERIFY(error.contains(status == 404 ? S("[API_NOT_FOUND]") : status == 302 ? S("[REDIRECT_BLOCKED]") : S("[AUTH_FAILED]")));
            QVERIFY(!error.contains(S("private upstream message"))); QVERIFY(!error.contains(QString::fromUtf8(localToken)));
        }
        QTcpServer closed; QVERIFY(closed.listen(QHostAddress::LocalHost, 0)); const auto port = closed.serverPort(); closed.close();
        bool done = false; QString error;
        transport.local(port, tokenFile.fileName(), S("GET"), S("/status"), {},
                        [&](QJsonDocument, QString message) { error = message; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 4000);
        // On Windows Qt can retry a closed-port connection until our total
        // deadline wins. Report the observed condition, never invent a refusal.
        QVERIFY2(error.contains(S("[CONNECTION_REFUSED]")) || error.contains(S("[REQUEST_TIMEOUT]")), qPrintable(error));
    }

    void homeSeparatesRunningServiceFromCredentialsAndNodeOffline() {
        using namespace gamelink;
        HttpFixture fixture; QVERIFY(fixture.listen());
        EnvironmentReport environment;
        environment.installation = InstallationState::Installed; environment.executable = S("C:/Mock/ZeroTier/One/zerotier-one_x64.exe");
        environment.service.registered = true; environment.service.state = ServiceState::Running;
        environment.detectedPort = fixture.server.serverPort();
        QSettings settings; settings.setValue(S("ztPort"), QString::number(fixture.server.serverPort()));
        settings.setValue(S("ztTokenFile"), settingsDirectory.filePath(S("missing-file"))); settings.sync();
        bool reject = true, invalidStatus = false;
        fixture.handler = [&](const Request& request, QTcpSocket* socket) {
            if (reject) HttpFixture::reply(socket, "not-your-token", 401);
            else if (request.path == "/status") HttpFixture::reply(socket, invalidStatus ? QByteArray("{}") : QByteArray(R"({"address":"aaaaaaaaaa","online":false,"clock":100000,"version":"1.fixture"})"));
            else HttpFixture::reply(socket, "[]");
        };
        Window window([&] { return environment; });
        auto* installed = window.findChild<QLabel*>(S("installationStatus")); QVERIFY(installed);
        auto* service = window.findChild<QLabel*>(S("serviceStatus")); QVERIFY(service);
        auto* api = window.findChild<QLabel*>(S("localApiStatus")); QVERIFY(api);
        QVERIFY(installed->text().contains(S("已发现"))); QVERIFY(service->text().contains(S("Running")));
        QVERIFY(api->text().contains(S("[TOKEN_MISSING]")));
        auto* refresh = window.findChild<QPushButton*>(S("refreshEnvironmentButton")); QVERIFY(refresh);
        auto* port = window.findChild<QLineEdit*>(S("ztPort")); QVERIFY(port);
        port->setText(S("bad port")); refresh->click(); QVERIFY(api->text().contains(S("[INVALID_PORT]")));
        QCOMPARE(environment.detectedPort, static_cast<int>(fixture.server.serverPort())); // No auto-overwrite of user input.
        auto* usePort = window.findChild<QPushButton*>(S("useDetectedPortButton")); QVERIFY(usePort); QVERIFY(usePort->isEnabled());
        usePort->click(); QCOMPARE(port->text(), QString::number(fixture.server.serverPort()));
        QTemporaryFile tokenFile(settingsDirectory.path() + S("/token-XXXXXX"));
        QVERIFY(tokenFile.open()); tokenFile.write(localToken); tokenFile.flush();
        auto* token = window.findChild<QLineEdit*>(S("ztTokenFile")); QVERIFY(token); token->setText(tokenFile.fileName());
        refresh->click(); QTRY_VERIFY_WITH_TIMEOUT(api->text().contains(S("[AUTH_FAILED]")), 4000);
        QVERIFY(service->text().contains(S("Running")));
        reject = false; refresh->click();
        QTRY_VERIFY_WITH_TIMEOUT(api->text().contains(S("可访问，认证成功")), 4000);
        auto* home = window.findChild<QLabel*>(S("homeStatus")); QVERIFY(home);
        QVERIFY(home->text().contains(S("OFFLINE（API 可访问）"))); QVERIFY(home->text().contains(S("aaaaaaaaaa")));
        QVERIFY(service->text().contains(S("Running")));
        invalidStatus = true; refresh->click();
        QTRY_VERIFY_WITH_TIMEOUT(api->text().contains(S("[API_SCHEMA_UNSUPPORTED]")), 4000);
        QVERIFY(service->text().contains(S("Running")));
        auto* log = window.findChild<QPlainTextEdit*>(S("events")); QVERIFY(log);
        QVERIFY(!log->toPlainText().contains(QString::fromUtf8(localToken))); QVERIFY(!log->toPlainText().contains(S("not-your-token")));
        environment.service.state = ServiceState::Stopped; refresh->click();
        QVERIFY(service->text().contains(S("Stopped")));
        window.show(); installed->setText(S("MOCK 安装/服务/API 测试数据\n") + installed->text()); QCoreApplication::processEvents();
        QVERIFY(window.grab().save(QCoreApplication::applicationDirPath() + S("/zerotier-detection-fixture.png")));
    }

    void zeroTierPageJoinStatusAndTargetedLeave() {
        HttpFixture fixture; QVERIFY(fixture.listen());
        QTemporaryFile tokenFile(settingsDirectory.path()+S("/token-XXXXXX"));
        QVERIFY(tokenFile.open()); tokenFile.write(localToken); tokenFile.flush();
        const QString a=S("abcdef1234000001"), b=S("abcdef1234000002");
        QSettings settings; settings.setValue(S("ztPort"),QString::number(fixture.server.serverPort()));
        settings.setValue(S("ztTokenFile"),tokenFile.fileName()); settings.setValue(S("network"),a); settings.sync();
        bool joined=false, otherJoined=true, assigned=false, reject=false, failLeave=true;
        QString status=S("ACCESS_DENIED"); QPointer<QTcpSocket> pendingJoin;
        fixture.handler=[&](const Request& request,QTcpSocket* socket){
            QCOMPARE(request.headers.value("x-zt1-auth"),localToken);
            QVERIFY(!request.path.startsWith("/api/"));
            if(reject){HttpFixture::reply(socket,"{}",401);return;}
            if(request.path=="/peer")HttpFixture::reply(socket,"[]");
            else if(request.path=="/status")HttpFixture::reply(socket,R"({"address":"aaaaaaaaaa","online":true,"clock":100000,"version":"1.fixture"})");
            else if(request.path=="/network"){
                QJsonArray list;
                if(otherJoined)list.append(QJsonObject{{S("id"),b},{S("name"),S("Other network")},{S("status"),S("OK")},{S("assignedAddresses"),QJsonArray{S("10.99.0.2/24")}}});
                if(joined)list.append(QJsonObject{{S("id"),a},{S("name"),S("GameLink MOCK")},{S("status"),status},{S("assignedAddresses"),assigned?QJsonArray{S("10.77.0.1/24")}:QJsonArray{}}});
                HttpFixture::reply(socket,QJsonDocument(list).toJson(QJsonDocument::Compact));
            }else if(request.path=="/network/abcdef1234000001" && request.method=="POST"){
                auto config=QJsonDocument::fromJson(request.body).object();
                QVERIFY(config[S("allowManaged")].toBool());
                QVERIFY(!config[S("allowDefault")].toBool(true));QVERIFY(!config[S("allowGlobal")].toBool(true));QVERIFY(!config[S("allowDNS")].toBool(true));
                joined=true;pendingJoin=socket; // Keep mutation pending to exercise duplicate-operation guards.
            }else if(request.path=="/network/abcdef1234000002" && request.method=="DELETE"){
                if(failLeave)HttpFixture::reply(socket,"private fixture failure",500);
                else{otherJoined=false;HttpFixture::reply(socket,{},204);}
            }else HttpFixture::reply(socket,"{}",404);
        };
        auto platform=std::make_shared<MockInstallerPlatform>();
        Window window([]{return gamelink::EnvironmentReport{};},platform);
        auto* stack=window.findChild<QStackedWidget*>(S("pageStack"));stack->setCurrentIndex(Window::ZeroTierPage);
        auto* join=window.findChild<QPushButton*>(S("ztJoinButton"));QVERIFY(join);
        auto* leave=window.findChild<QPushButton*>(S("ztLeaveButton"));QVERIFY(leave);
        auto* refresh=window.findChild<QPushButton*>(S("ztRefreshButton"));QVERIFY(refresh);
        auto* table=window.findChild<QTableWidget*>(S("ztNetworksTable"));QVERIFY(table);
        auto* state=window.findChild<QLabel*>(S("ztNetworkStatus"));QVERIFY(state);
        auto* result=window.findChild<QLabel*>(S("ztOperationStatus"));QVERIFY(result);
        QTRY_COMPARE_WITH_TIMEOUT(table->rowCount(),1,4000);QTRY_VERIFY_WITH_TIMEOUT(join->isEnabled(),4000);
        QCOMPARE(state->text(),S("未加入"));
        join->click();QTRY_VERIFY_WITH_TIMEOUT(!pendingJoin.isNull(),4000);
        QVERIFY(!join->isEnabled());QVERIFY(!leave->isEnabled());
        QVERIFY(!window.findChild<QPushButton*>(S("ztUninstallButton"))->isEnabled());
        join->click();int postCount=0;for(const auto& r:fixture.requests)if(r.method=="POST")++postCount;QCOMPARE(postCount,1);
        HttpFixture::reply(pendingJoin,"{}");pendingJoin=nullptr;
        QTRY_COMPARE_WITH_TIMEOUT(state->text(),S("等待授权"),5000);
        status=S("REQUESTING_CONFIGURATION");refresh->click();QTRY_COMPARE_WITH_TIMEOUT(state->text(),S("等待配置"),5000);
        status=S("OK");refresh->click();QTRY_COMPARE_WITH_TIMEOUT(state->text(),S("等待分配 IP"),5000);
        assigned=true;refresh->click();QTRY_COMPARE_WITH_TIMEOUT(state->text(),S("已连接"),5000);
        window.setWindowTitle(S("GameLink · MOCK ZeroTier status / no real network"));window.show();QCoreApplication::processEvents();
        QVERIFY(window.grab().save(QCoreApplication::applicationDirPath()+S("/zerotier-status-fixture.png")));
        auto selectB=[&]{for(int i=0;i<table->rowCount();++i)if(table->item(i,0)->text()==b){table->selectRow(i);return true;}return false;};
        QVERIFY(selectB());QVERIFY(leave->isEnabled());
        auto answer=[](QMessageBox::StandardButton button){QTimer::singleShot(0,[button]{auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());QVERIFY(box);box->button(button)->click();});};
        answer(QMessageBox::No);leave->click();for(const auto& r:fixture.requests)QVERIFY(r.method!="DELETE");
        answer(QMessageBox::Yes);leave->click();QTRY_VERIFY_WITH_TIMEOUT(result->text().contains(S("离开网络失败")),5000);
        QTRY_VERIFY_WITH_TIMEOUT(leave->isEnabled(),5000);QCOMPARE(table->rowCount(),2);QVERIFY(otherJoined);QVERIFY(joined);
        failLeave=false;answer(QMessageBox::Yes);leave->click();QTRY_COMPARE_WITH_TIMEOUT(table->rowCount(),1,5000);
        QCOMPARE(table->item(0,0)->text(),a);QVERIFY(joined);QVERIFY(!otherJoined);
        for(const auto& r:fixture.requests)if(r.method=="DELETE")QCOMPARE(r.path,QByteArray("/network/abcdef1234000002"));
        reject=true;refresh->click();QTRY_VERIFY_WITH_TIMEOUT(window.findChild<QLabel*>(S("localApiStatus"))->text().contains(S("[AUTH_FAILED]")),5000);
        QVERIFY(!leave->isEnabled());QVERIFY(!join->isEnabled());QVERIFY(table->item(0,2)->text().contains(S("状态过期")));
        auto* log=window.findChild<QPlainTextEdit*>(S("events"));QVERIFY(!log->toPlainText().contains(QString::fromUtf8(localToken)));
        QVERIFY(!log->toPlainText().contains(S("private fixture failure")));QCOMPARE(platform->calls,0);
    }

    void guiRoomJoinPathChangesAndLeave() {
        HttpFixture fixture; QVERIFY(fixture.listen());
        QTemporaryFile tokenFile(settingsDirectory.path() + S("/token-XXXXXX"));
        QVERIFY(tokenFile.open()); tokenFile.write(localToken); tokenFile.flush();
        QSettings settings;
        settings.setValue(S("server"), fixture.origin());
        settings.setValue(S("ztPort"), QString::number(fixture.server.serverPort()));
        settings.setValue(S("ztTokenFile"), tokenFile.fileName()); settings.sync();
        bool joined = false, relay = false, malformed = false;
        fixture.handler = [&](const Request& request, QTcpSocket* socket) {
            const bool local = !request.path.startsWith("/api/");
            if (local) {
                QCOMPARE(request.headers.value("x-zt1-auth"), localToken);
                QVERIFY(!request.headers.contains("authorization"));
            } else {
                QCOMPARE(request.headers.value("authorization"), QByteArray("Bearer ") + apiToken.toUtf8());
                QVERIFY(!request.headers.contains("x-zt1-auth"));
            }
            if (request.path == "/peer") {
                const QByteArray paths = relay ? "[]" : R"([{"address":"203.0.113.2/9993","preferred":true,"active":true,"expired":false,"lastReceive":99990}])";
                HttpFixture::reply(socket, malformed ? QByteArray(R"([{"address":"bbbbbbbbbb","role":"LEAF","versionMajor":1,"tunneled":false,"paths":[{"preferred":true}]}])") : QByteArray(R"([{"address":"bbbbbbbbbb","role":"LEAF","versionMajor":1,"tunneled":false,"paths":)") + paths + "}]");
            } else if (request.path == "/status") {
                HttpFixture::reply(socket, R"({"address":"aaaaaaaaaa","online":true,"tcpFallbackActive":false,"clock":100000})");
            } else if (request.path == "/network") {
                HttpFixture::reply(socket, joined ? QByteArray(R"([{"id":"abcdef1234000001","status":"OK","assignedAddresses":["10.77.0.1/24"],"allowDefault":false,"allowGlobal":false,"allowDNS":false}])") : QByteArray("[]"));
            } else if (request.path == "/network/abcdef1234000001") {
                const auto config = QJsonDocument::fromJson(request.body).object();
                QVERIFY(!config[S("allowDefault")].toBool(true)); QVERIFY(!config[S("allowGlobal")].toBool(true)); QVERIFY(!config[S("allowDNS")].toBool(true));
                joined = true; HttpFixture::reply(socket, "{}");
            } else if (request.path == "/api/v1/rooms") {
                HttpFixture::reply(socket, R"({"id":"1111111111111111","networkId":"abcdef1234000001","policy":"preferred_direct","joinKey":"22222222222222222222222222222222","expiresAt":"2026-10-15T16:00:00Z"})", 201);
            } else if (request.path.endsWith("/peers")) {
                HttpFixture::reply(socket, R"({"peers":[{"userId":"alice","nodeId":"aaaaaaaaaa","authorized":true,"virtualIps":["10.77.0.1"]},{"userId":"bob","nodeId":"bbbbbbbbbb","authorized":true,"virtualIps":["10.77.0.2"]}]})");
            } else if (request.path.endsWith("/leave")) {
                HttpFixture::reply(socket, {}, 204);
            } else { HttpFixture::reply(socket, "{}", 404); }
        };
        Window window;
        auto* token = window.findChild<QLineEdit*>(S("serverToken")); QVERIFY(token); token->setText(apiToken);
        auto* games = window.findChild<QComboBox*>(S("gameSelector")); QVERIFY(games); QCOMPARE(games->count(), 3);
        auto* home = window.findChild<QLabel*>(S("homeStatus")); QVERIFY(home);
        QTRY_VERIFY_WITH_TIMEOUT(home->text().contains(S("aaaaaaaaaa")), 5000);
        auto* policy = window.findChild<QComboBox*>(S("policySelector")); QVERIFY(policy);
        auto* create = window.findChild<QPushButton*>(S("createRoomButton")); QVERIFY(create);
        policy->setCurrentIndex(1); create->click();
        for (const auto& request : fixture.requests) QVERIFY(request.path != "/api/v1/rooms");
        policy->setCurrentIndex(0); create->click();
        auto* table = window.findChild<QTableWidget*>(S("peersTable")); QVERIFY(table);
        QTRY_COMPARE_WITH_TIMEOUT(table->rowCount(), 2, 5000);
        auto* join = window.findChild<QPushButton*>(S("joinNetworkButton")); QVERIFY(join); QTRY_VERIFY_WITH_TIMEOUT(join->isEnabled(),5000); join->click();
        QTRY_VERIFY_WITH_TIMEOUT(joined, 5000);
        auto hasState = [table](const QString& state) {
            for (int row = 0; row < table->rowCount(); ++row)
                if (table->item(row, 2) && table->item(row, 2)->text() == state) return true;
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(hasState(S("DIRECT")), 7000);
        window.show();
        auto* tabs = window.findChild<QStackedWidget*>(QStringLiteral("pageStack")); QVERIFY(tabs); tabs->setCurrentIndex(2);
        // Explicitly labeled fixture screenshot; no real network or player evidence.
        window.setWindowTitle(S("GameLink · MOCK integration fixture"));
        auto* roomStatus = window.findChild<QLabel*>(S("roomStatus")); QVERIFY(roomStatus);
        roomStatus->setText(S("MOCK 测试数据 · 未连接真实 ZeroTier\n") + roomStatus->text());
        QCoreApplication::processEvents();
        QVERIFY(window.grab().save(QCoreApplication::applicationDirPath() + S("/qt-room-fixture.png")));
        relay = true; QTRY_VERIFY_WITH_TIMEOUT(hasState(S("RELAY")), 7000);
        malformed = true; QTRY_VERIFY_WITH_TIMEOUT(hasState(S("UNKNOWN")), 7000);
        auto* log = window.findChild<QPlainTextEdit*>(S("events")); QVERIFY(log);
        QVERIFY(!log->toPlainText().contains(apiToken)); QVERIFY(!log->toPlainText().contains(QString::fromUtf8(localToken)));
        QVERIFY(!log->toPlainText().contains(S("22222222222222222222222222222222")));
        auto* leave = window.findChild<QPushButton*>(S("leaveRoomButton")); QVERIFY(leave); leave->click();
        QTRY_COMPARE_WITH_TIMEOUT(table->rowCount(), 0, 5000);
        settings.sync(); QVERIFY(!settings.allKeys().contains(S("serverToken")));
    }
};
QTEST_MAIN(QtClientTests)
#include "qt_tests.moc"
