#pragma once
#include "transport.hpp"
#include "p2p.hpp"
#include "zerotier_environment.hpp"
#include "zerotier_credentials.hpp"
#include "zerotier_installer.hpp"
#include <QMainWindow>
#include <QSettings>
#include <QElapsedTimer>
#include <QUdpSocket>
#include <QTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QHash>
#include <optional>
class QLabel;class QLineEdit;class QComboBox;class QPlainTextEdit;class QTableWidget;class QPushButton;class QCloseEvent;
class Window : public QMainWindow {
public:
 enum Page { OverviewPage, GamesPage, RoomPage, ZeroTierPage, DiagnosticsPage, SettingsPage, PageCount };
 explicit Window(gamelink::EnvironmentInspector inspector=gamelink::inspectZeroTierEnvironment,
                 std::shared_ptr<gamelink::InstallerPlatform> installerPlatform=gamelink::nativeInstallerPlatform(),
                 gamelink::CredentialLocator credentialLocator=gamelink::zeroTierCredentialPaths);
protected:
 void closeEvent(QCloseEvent*) override;
private:
 gamelink::EnvironmentInspector environmentInspector;gamelink::EnvironmentReport environment;
 gamelink::CredentialLocator credentialLocator;bool credentialBusy{};
 QPushButton *detectCredentialButton,*chooseCredentialButton;
 QLabel* credentialStatus;
 gamelink::ZeroTierInstaller installer;gamelink::ProductInventory installerInventory;
 quint64 serviceGeneration{};
 QLabel* installerStatus;QLineEdit* installerPath;QComboBox* installedProducts;
 QPushButton *installZeroTier,*uninstallZeroTier,*chooseInstaller,*refreshProducts;
 Transport transport;QSettings settings;QTimer pollTimer,expiryTimer,probeTimer,environmentTimer;QElapsedTimer sampleAge,joiningAge,probeClock,probeDone;
 QLabel *home,*roomInfo,*roomFeedback,*roomGameSummary,*probeInfo,*capability,*installationStatus,*serviceStatus,*localApiStatus,*portStatus;
 QPushButton *createRoomButton,*joinRoomButton,*leaveRoomButton;
 QString roomOperation;
 QLabel *nodeValue,*ipValue,*roomValue,*connectionBadge,*sidebarStatus,*lastEvent,*gameDetails;
 QLabel *zeroTierNodeStatus,*zeroTierNetworkStatus,*zeroTierNetworkId,*networkOperationStatus;
 int sampledPort{};QString sampledTokenFile;
 QTableWidget* networksTable;
 QPushButton *joinZeroTier,*leaveZeroTier,*roomJoinNetwork;
 QPushButton* useDetectedPort;QString localApiMessage,apiVersion,lastApiEvent;
 QLineEdit *serverUrl,*serverToken,*ztTokenFile,*ztPort,*roomCode,*manualNetwork,*gamePath,*bindIP,*probePort;
 QComboBox *policy,*games,*target;QPlainTextEdit* events;QTableWidget* peersTable;
 QJsonArray roomPeers,networks;QHash<QString,QJsonObject> profiles;
 QString node,network,roomId,invite,previousStates,contextKey,sessionPolicy;
 gamelink::Sample sample;bool localBusy{},roomBusy{},mutationBusy{},networkMutationBusy{},probeRunning{},probeHealthy{},pathConfigured{};
 gamelink::State targetState=gamelink::State::Disconnected;
 QUdpSocket udp;QByteArray probeNonce;QHostAddress probeTarget;quint16 activeProbePort{};
 std::vector<std::optional<double>> rtts;int echoBudget{};
 void buildUi();void refreshEnvironment();void localFailure(const QString&,const QString&);
 void detectCredentials();void tryCredential(QStringList,int,QString,quint64,QString={});
 void refreshInstaller();void updateInstallerButtons();void selectInstaller();void installService();void uninstallService();
 void log(const QString&);void save();void pollLocal();void pollRoom();void render();void roomAction(bool create);bool applyRoom(const QJsonObject&);
 void roomMessage(const QString&,const QString& state=QStringLiteral("error"));void updateRoomControls();
 void localJoin();void localLeave();void renderZeroTier();bool localSampleFresh()const;QString networkState(const QJsonObject&,bool)const;
 void leaveRoom();void clearRoom();void chooseGame();void launch();void bindEcho();void startProbe();void probeStep();void datagrams();void stopProbe();
 void api(const QString&,const QString&,const QJsonObject&,Transport::Callback);
 gamelink::Policy selectedPolicy()const;gamelink::Sample peerSample(const QString&)const;
};
