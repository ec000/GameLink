#include "transport.hpp"
#include "window.hpp"
#include "zerotier_environment.hpp"
#include <QFile>
#include <QComboBox>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
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
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    }
    void init() { QSettings settings; settings.clear(); settings.sync(); }

    void sidebarNavigationAndSmallWindowRemainUsable() {
        Window window([] { return gamelink::EnvironmentReport{}; });
        auto* stack = window.findChild<QStackedWidget*>(S("pageStack")); QVERIFY(stack);
        QCOMPARE(stack->count(), 5);
        window.setWindowTitle(S("GameLink · UI preview / MOCK environment"));
        window.show(); QCoreApplication::processEvents();
        const QStringList snapshots = {S("overview"), S("games"), S("room"), S("diagnostics"), S("settings")};
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
        auto* join = window.findChild<QPushButton*>(S("joinNetworkButton")); QVERIFY(join); join->click();
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
