#include "transport.hpp"
#include "window.hpp"
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
#include <QTabWidget>
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
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    }
    void init() { QSettings settings; settings.clear(); settings.sync(); }

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
                       [&](QJsonDocument, QString error) { QVERIFY(!error.isEmpty()); ++callbacks; });
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
        auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget()); QVERIFY(tabs); tabs->setCurrentIndex(2);
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
