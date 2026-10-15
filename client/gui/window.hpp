#pragma once
#include "transport.hpp"
#include "p2p.hpp"
#include <QMainWindow>
#include <QSettings>
#include <QElapsedTimer>
#include <QUdpSocket>
#include <QTimer>
#include <QJsonArray>
#include <QHash>
#include <optional>
class QLabel;class QLineEdit;class QComboBox;class QPlainTextEdit;class QTableWidget;class QPushButton;
class Window : public QMainWindow {
public:
 Window();
private:
 Transport transport;QSettings settings;QTimer pollTimer,expiryTimer,probeTimer;QElapsedTimer sampleAge,joiningAge,probeClock,probeDone;
 QLabel *home,*roomInfo,*probeInfo,*capability;
 QLineEdit *serverUrl,*serverToken,*ztTokenFile,*ztPort,*roomCode,*manualNetwork,*gamePath,*bindIP,*probePort;
 QComboBox *policy,*games,*target;QPlainTextEdit* events;QTableWidget* peersTable;
 QJsonArray roomPeers,networks;QHash<QString,QJsonObject> profiles;
 QString node,network,roomId,invite,previousStates,contextKey,sessionPolicy;
 gamelink::Sample sample;bool localBusy{},roomBusy{},mutationBusy{},probeRunning{},probeHealthy{},pathConfigured{};
 gamelink::State targetState=gamelink::State::Disconnected;
 QUdpSocket udp;QByteArray probeNonce;QHostAddress probeTarget;quint16 activeProbePort{};
 std::vector<std::optional<double>> rtts;int echoBudget{};
 void log(const QString&);void save();void pollLocal();void pollRoom();void render();void roomAction(bool create);void applyRoom(const QJsonObject&);
 void localJoin();void leaveRoom();void clearRoom();void chooseGame();void launch();void bindEcho();void startProbe();void probeStep();void datagrams();void stopProbe();
 void api(const QString&,const QString&,const QJsonObject&,Transport::Callback);
 gamelink::Policy selectedPolicy()const;gamelink::Sample peerSample(const QString&)const;
};
