#include "window.hpp"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTableWidget>
#include <QStyle>
#include <QColor>
#include <QVBoxLayout>
#define S(x) QStringLiteral(x)
namespace {
QStringList assignedIPs(const QJsonArray& networks,const QString& id){
 QStringList out;for(const auto& v:networks){const auto n=v.toObject();if(n[S("id")].toString()==id || n[S("nwid")].toString()==id)
 for(const auto& a:n[S("assignedAddresses")].toArray())out<<a.toString().section(QLatin1Char('/'),0,0);}
 return out;
}
gamelink::Peer parsePeer(const QJsonObject& p){
 gamelink::Peer out;out.nodeId=p[S("address")].toString().toStdString();out.role=p[S("role")].toString().toStdString();
 out.schemaKnown=p[S("versionMajor")].toInt(-1)==1 && p[S("tunneled")].isBool() && p[S("paths")].isArray();
 out.tunneled=p[S("tunneled")].toBool();
 for(const auto& v:p[S("paths")].toArray()){
  const auto path=v.toObject();
  if(!v.isObject() || !path[S("preferred")].isBool() || !path[S("active")].isBool() || !path[S("expired")].isBool() ||
     !path[S("lastReceive")].isDouble() || !path[S("address")].isString()){out.schemaKnown=false;continue;}
  out.paths.push_back({path[S("address")].toString().toStdString(),path[S("preferred")].toBool(),path[S("active")].toBool(),
   path[S("expired")].toBool(),static_cast<qint64>(path[S("lastReceive")].toDouble())});
 }
 return out;
}
}
Window::Window(gamelink::EnvironmentInspector inspector,std::shared_ptr<gamelink::InstallerPlatform> installerPlatform,gamelink::CredentialLocator locator):environmentInspector(std::move(inspector)),credentialLocator(std::move(locator)),installer(std::move(installerPlatform),this),transport(this),settings(),udp(this){
 buildUi();
 const QDir profileDir(QCoreApplication::applicationDirPath()+S("/profiles"));
 for(const auto& file:profileDir.entryList({S("*.json")},QDir::Files)){
  QFile f(profileDir.filePath(file));if(!f.open(QIODevice::ReadOnly))continue;
  const auto p=QJsonDocument::fromJson(f.readAll()).object();
  if(p[S("schema_version")].toInt()!=1 || p[S("steam_api_interception")].toBool(true))continue;
  const auto id=p[S("id")].toString();const auto mode=p[S("mode")].toString();
  if(id.isEmpty() || (mode!=S("virtual_lan") && mode!=S("virtual_ip") && mode!=S("observe_only")))continue;
  profiles[id]=p;games->addItem(p[S("name")].toString()+S(" · 待验证"),id);
 }
 connect(games,&QComboBox::currentIndexChanged,this,[this]{chooseGame();});chooseGame();
 connect(target,&QComboBox::currentIndexChanged,this,[this]{stopProbe();probeHealthy=false;render();});
 connect(&pollTimer,&QTimer::timeout,this,[this]{pollLocal();pollRoom();});pollTimer.start(2000);
 connect(&expiryTimer,&QTimer::timeout,this,[this]{render();});expiryTimer.start(500);
 connect(&udp,&QUdpSocket::readyRead,this,[this]{datagrams();});
 connect(&probeTimer,&QTimer::timeout,this,[this]{probeStep();});
 connect(&environmentTimer,&QTimer::timeout,this,[this]{refreshEnvironment();});environmentTimer.start(5000);
 connect(&installer,&gamelink::ZeroTierInstaller::busyChanged,this,[this](bool busy){
  updateInstallerButtons();
  if(busy){++serviceGeneration;installerStatus->setText(S("Windows Installer 正在运行，请在安装向导中完成或取消；请勿关闭 GameLink。"));localFailure(S("服务管理"),S("安装/卸载进行中，旧链路证据已清除"));}
 });
 connect(&installer,&gamelink::ZeroTierInstaller::completed,this,[this](const gamelink::InstallerResult& result){
  const auto message=S("ZeroTier 服务管理：")+result.message;refreshInstaller();installerStatus->setText(message);log(message);refreshEnvironment();pollLocal();
 });
 joiningAge.start();refreshEnvironment();refreshInstaller();
 // Migrate only the old, absent placeholder. Keep all explicitly chosen paths.
 if(ztTokenFile->text().isEmpty() || (QDir::cleanPath(ztTokenFile->text())==QDir::cleanPath(gamelink::legacyGameLinkTokenPath()) && !QFileInfo::exists(ztTokenFile->text())))detectCredentials();
 else pollLocal();
 log(S("MVP 启动；严格直连不可用；NAT 原因仅凭 local API 无法确定"));
}
void Window::refreshEnvironment(){
 environment=environmentInspector();
 installationStatus->setText(S("安装检测：")+gamelink::installationDescription(environment));
 serviceStatus->setText(S("Windows 服务：")+gamelink::serviceDescription(environment.service));
 portStatus->setText(environment.portDetail);
 useDetectedPort->setEnabled(!installer.busy() && !networkMutationBusy && !credentialBusy && environment.detectedPort>0 && environment.detectedPort!=ztPort->text().toInt());
}
void Window::localFailure(const QString& stage,const QString& reason){
 sample={};sampleAge.invalidate();probeHealthy=false;stopProbe();udp.close();
 localApiMessage=stage+S("：")+reason;
 if(lastApiEvent!=localApiMessage){lastApiEvent=localApiMessage;log(S("本地 API ")+localApiMessage);}
 render();
}
void Window::log(const QString& s){
 events->appendPlainText(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)+QLatin1Char(' ')+s);
 lastEvent->setText(s.section(QLatin1Char('\n'),0,0));lastEvent->setToolTip(s);
}
void Window::save(){settings.setValue(S("server"),serverUrl->text());settings.setValue(S("ztTokenFile"),ztTokenFile->text());settings.setValue(S("ztPort"),ztPort->text());settings.setValue(S("network"),manualNetwork->text());if(pathConfigured)settings.setValue(S("gamePath/")+games->currentData().toString(),gamePath->text());}
void Window::chooseGame(){
 gamePath->setText(settings.value(S("gamePath/")+games->currentData().toString()).toString());pathConfigured=!gamePath->text().isEmpty();
 const auto mode=profiles.value(games->currentData().toString())[S("mode")].toString();
 gameDetails->setText((mode==S("virtual_lan")?S("虚拟局域网 · 在游戏内使用 LAN 房间"):mode==S("virtual_ip")?S("虚拟 IP · 在游戏内手动连接伙伴地址"):S("仅观察 · 沿用游戏原有 Steam / UDP 联机"))+S("\n兼容性未验证，请以实际游戏测试为准。"));
 roomGameSummary->setText(games->currentData().toString().isEmpty()?S("未加载游戏配置，请在「我的游戏」中选择。"):S("当前游戏：")+profiles.value(games->currentData().toString())[S("name")].toString()+S("。创建房间无需先指定或启动游戏程序。"));
}
gamelink::Policy Window::selectedPolicy()const{
 const auto selected=sessionPolicy.isEmpty() ? (policy->currentIndex()==1?S("direct_required"):policy->currentIndex()==2?S("relay_allowed"):S("preferred_direct")) : sessionPolicy;
 return selected==S("direct_required")?gamelink::Policy::DirectRequired:selected==S("relay_allowed")?gamelink::Policy::RelayAllowed:gamelink::Policy::PreferredDirect;
}
void Window::api(const QString& method,const QString& path,const QJsonObject& body,Transport::Callback cb){transport.room(serverUrl->text(),serverToken->text(),method,path,body,std::move(cb));}
void Window::roomMessage(const QString& message,const QString& state){
 roomFeedback->setText(message);roomFeedback->setProperty("state",state);
 roomFeedback->style()->unpolish(roomFeedback);roomFeedback->style()->polish(roomFeedback);
 log(message);updateRoomControls();
 if(auto* stack=findChild<QStackedWidget*>(S("pageStack"));stack && stack->currentIndex()==RoomPage)
  if(auto* scroll=qobject_cast<QScrollArea*>(stack->currentWidget()))scroll->ensureWidgetVisible(roomFeedback);
}
void Window::updateRoomControls(){
 const bool available=!installer.busy() && !networkMutationBusy && !mutationBusy;
 createRoomButton->setEnabled(available && roomId.isEmpty());joinRoomButton->setEnabled(available && roomId.isEmpty());
 leaveRoomButton->setEnabled(available && !roomId.isEmpty());
 createRoomButton->setText(mutationBusy && roomOperation==S("创建房间")?S("正在创建…"):S("创建房间"));
 joinRoomButton->setText(mutationBusy && roomOperation==S("加入房间")?S("正在加入…"):S("加入房间"));
 leaveRoomButton->setText(mutationBusy && roomOperation==S("离开房间")?S("正在离开…"):S("离开房间"));
 // Keep a room request bound to the service and credentials the user submitted.
 serverUrl->setEnabled(!mutationBusy);serverToken->setEnabled(!mutationBusy);
 games->setEnabled(!mutationBusy);policy->setEnabled(!mutationBusy);roomCode->setEnabled(!mutationBusy);
}
void Window::roomAction(bool create){
 if(installer.busy()){roomMessage(S("ZeroTier 安装/卸载进行中，请等待安装向导结束。"));return;}
 if(networkMutationBusy || mutationBusy || !roomId.isEmpty()){roomMessage(S("请先离开当前房间或等待请求完成"));return;}
 if(policy->currentIndex()==1){roomMessage(S("严格直连暂不支持，请在设置中选择「优先直连」或「允许中继」。"));return;}
 if(games->currentData().toString().isEmpty()){roomMessage(S("未加载游戏配置，请先在「我的游戏」中选择。"));return;}
 QString path=S("/api/v1/rooms");QJsonObject b;
 if(create){b={{S("gameId"),games->currentData().toString()},{S("policy"),policy->currentIndex()==2?S("relay_allowed"):S("preferred_direct")}};}
 else{
  const auto parts=roomCode->text().trimmed().split(QLatin1Char(':'));
  if(parts.size()!=2 || !QRegularExpression(S("^[0-9a-f]{16}$")).match(parts[0]).hasMatch() || !QRegularExpression(S("^[0-9a-f]{32}$")).match(parts[1]).hasMatch()){roomMessage(S("房间码格式为 16 位十六进制 id:32 位邀请密钥"));return;}
  path+=QLatin1Char('/')+parts[0]+S("/join");b={{S("joinKey"),parts[1]}};invite=parts[1];
 }
 if(serverToken->text().trimmed().isEmpty()){roomMessage(S("尚未填写 GameLink 房间访问凭证。请打开「房间服务设置」配置已部署的 GameLink Server 和个人凭证；不是 ZeroTier 本地 Token，凭证重启后需重新填写。"));return;}
 mutationBusy=true;roomOperation=create?S("创建房间"):S("加入房间");
 roomMessage(S("正在%1，请稍候…").arg(roomOperation),S("busy"));
 api(S("POST"),path,b,[this](QJsonDocument doc,QString err){
  mutationBusy=false;updateRoomControls();
  if(!err.isEmpty()){
   QString help;
   if(err.contains(S("[CONNECTION_REFUSED]")) || err.contains(S("[REQUEST_TIMEOUT]")))help=S("\n请核对 GameLink 房间服务是否部署及可达；默认 127.0.0.1:8080 指这台电脑，ZeroTier 管理后台不能代替房间服务。");
   else if(err.contains(S("[AUTH_FAILED]")))help=S("\n请在房间服务设置中核对 GameLink 个人访问凭证；不要使用 ZeroTier 本地或控制器管理 Token。");
   roomMessage(roomOperation+S("失败：")+err+help);return;
  }
  if(!doc.isObject()){roomMessage(S("房间服务返回格式无效，请核对是否连接到 GameLink Server。"));return;}
  if(!applyRoom(doc.object()))return;
  roomMessage(roomOperation+S("成功；请点击「连接虚拟网络」，并等待授权与 IP 分配。"),S("success"));pollRoom();
 });
}
bool Window::applyRoom(const QJsonObject& r){
 const auto id=r[S("id")].toString(),n=r[S("networkId")].toString();
 if(!QRegularExpression(S("^[0-9a-f]{16}$")).match(id).hasMatch() || !QRegularExpression(S("^[0-9a-f]{16}$")).match(n).hasMatch()){roomMessage(S("服务器房间数据不合法：缺少有效的房间 ID 或 Network ID，请核对 GameLink Server。"));return false;}
 roomId=id;network=n;sessionPolicy=r[S("policy")].toString();
 if(!r[S("joinKey")].toString().isEmpty())invite=r[S("joinKey")].toString();
 roomInfo->setText(S("房间 %1 · 网络 %2 · %3 · 有效至 %4").arg(roomId,network,sessionPolicy,r[S("expiresAt")].toString()));
 joiningAge.restart();probeHealthy=false;sample={};sampleAge.invalidate();log(S("房间已授权；请显式加入 ZeroTier 网络"));pollLocal();render();return true;
}
void Window::pollRoom(){
 if(roomId.isEmpty() || roomBusy)return;roomBusy=true;const auto expected=roomId;
 api(S("GET"),S("/api/v1/rooms/")+roomId+S("/peers"),{},[this,expected](QJsonDocument doc,QString err){
  roomBusy=false;if(expected!=roomId)return;
  if(!err.isEmpty()){if(err.contains(S("HTTP 404"))){clearRoom();log(S("服务端房间不存在；已清除本地房间引用，本地 ZeroTier membership 保留"));return;}log(S("房间成员更新失败；VPS 失联不代表游戏通道已断开"));roomPeers={};target->clear();stopProbe();udp.close();probeHealthy=false;return;}
  roomPeers=doc.object()[S("peers")].toArray();
  const auto selected=target->currentData().toString();QStringList choices;
  for(const auto& v:roomPeers){const auto p=v.toObject();if(p[S("nodeId")].toString()==node || !p[S("authorized")].toBool())continue;for(const auto& a:p[S("virtualIps")].toArray())if(QHostAddress(a.toString()).protocol()==QAbstractSocket::IPv4Protocol)choices<<p[S("nodeId")].toString()+QLatin1Char('|')+a.toString();}
  QStringList old;for(int i=0;i<target->count();++i)old<<target->itemData(i).toString();
  if(old!=choices){target->clear();for(const auto& c:choices)target->addItem(c,c);const int i=target->findData(selected);if(i>=0)target->setCurrentIndex(i);}
  render();
 });
}
void Window::leaveRoom(){
 if(roomId.isEmpty() || mutationBusy || networkMutationBusy || installer.busy())return;mutationBusy=true;roomOperation=S("离开房间");
 roomMessage(S("正在离开房间，请稍候…"),S("busy"));
 api(S("POST"),S("/api/v1/rooms/")+roomId+S("/leave"),{},[this](QJsonDocument,QString err){mutationBusy=false;updateRoomControls();if(!err.isEmpty()){roomMessage(S("离开房间失败：")+err);return;}clearRoom();roomInfo->setText(S("已离开；Controller 撤销有传播延迟，本地 ZT membership 保留"));roomMessage(S("已离开房间，本机 ZeroTier 网络配置保留。"),S("success"));});
}
void Window::clearRoom(){
 roomId.clear();invite.clear();sessionPolicy.clear();network.clear();roomPeers={};target->clear();stopProbe();udp.close();sample={};probeHealthy=false;roomInfo->setText(S("没有房间"));render();
}
void Window::pollLocal(){
 if(installer.busy() || networkMutationBusy || credentialBusy)return;
 if(localBusy)return;localBusy=true;const int port=ztPort->text().toInt();const auto token=ztTokenFile->text();const auto expectedNetwork=network;const auto generation=serviceGeneration;QElapsedTimer batchClock;batchClock.start();
 // Sample peers before status.clock so lastReceive isn't compared to an earlier clock.
 transport.local(port,token,S("GET"),S("/peer"),{},[this,port,token,expectedNetwork,batchClock,generation](QJsonDocument peers,QString err){
  if(installer.busy() || generation!=serviceGeneration){localBusy=false;return;}
  if(!err.isEmpty() || !peers.isArray()){localBusy=false;localFailure(S("/peer"),err.isEmpty()?S("[API_SCHEMA_UNSUPPORTED] 预期 peers JSON 数组；请检查端口/版本"):err);return;}
  transport.local(port,token,S("GET"),S("/status"),{},[this,port,token,expectedNetwork,peers,batchClock,generation](QJsonDocument status,QString err2){
   if(installer.busy() || generation!=serviceGeneration){localBusy=false;return;}
   if(!err2.isEmpty() || !status.isObject()){localBusy=false;localFailure(S("/status"),err2.isEmpty()?S("[API_SCHEMA_UNSUPPORTED] 预期 status JSON 对象；请检查端口/版本"):err2);return;}
   const auto info=status.object();
   if(!QRegularExpression(S("^[0-9a-f]{10}$")).match(info[S("address")].toString()).hasMatch() || !info[S("online")].isBool() || !info[S("clock")].isDouble()){
    localBusy=false;localFailure(S("/status"),S("[API_SCHEMA_UNSUPPORTED] status 缺少有效 Node ID、online 或 clock 字段"));return;
   }
   transport.local(port,token,S("GET"),S("/network"),{},[this,expectedNetwork,info,peers,batchClock,port,token,generation](QJsonDocument ns,QString err3){
    localBusy=false;if(installer.busy() || generation!=serviceGeneration || expectedNetwork!=network || port!=ztPort->text().toInt() || token!=ztTokenFile->text())return;
    if(!err3.isEmpty() || !ns.isArray()){localFailure(S("/network"),err3.isEmpty()?S("[API_SCHEMA_UNSUPPORTED] 预期 network JSON 数组；请检查端口/版本"):err3);return;}
    const auto newNode=info[S("address")].toString();const auto key=newNode+QLatin1Char('|')+network+QLatin1Char('|')+serverUrl->text()+QLatin1Char('|')+ztTokenFile->text()+QLatin1Char('|')+ztPort->text()+QLatin1Char('|')+assignedIPs(ns.array(),network).join(S(","));
    if(key!=contextKey){contextKey=key;joiningAge.restart();probeHealthy=false;stopProbe();udp.close();}
    sampledPort=port;sampledTokenFile=token;node=newNode;networks=ns.array();const bool wasReady=sample.serviceAvailable && sample.online && sample.joined;sample={};
    apiVersion=info[S("version")].toString();localApiMessage=S("可访问，认证成功");if(lastApiEvent!=localApiMessage){lastApiEvent=localApiMessage;log(S("本地 API 已恢复可访问"));}
    sample.serviceAvailable=true;sample.online=info[S("online")].isBool() && info[S("online")].toBool();
    sample.tcpFallback=info[S("tcpFallbackActive")].toBool();sample.clockMs=static_cast<qint64>(info[S("clock")].toDouble());
    for(const auto& v:networks){const auto n=v.toObject();if(n[S("id")].toString()==network || n[S("nwid")].toString()==network)
     sample.joined=n[S("status")].toString()==S("OK") && !n[S("allowDefault")].toBool() && !n[S("allowGlobal")].toBool() && !n[S("allowDNS")].toBool();}
    if(sample.online && sample.joined && !wasReady)joiningAge.restart();
    // Only peer JSON is cached; local /status config and tokens are not logged/stored.
    setProperty("peerJSON",peers.toJson(QJsonDocument::Compact));
    sampleAge=batchClock;sample.joiningElapsedMs=joiningAge.elapsed();
    if(!sample.online || !sample.joined){stopProbe();udp.close();probeHealthy=false;}
    if(bindIP->text().isEmpty()){const auto ips=assignedIPs(networks,network);for(const auto& ip:ips)if(QHostAddress(ip).protocol()==QAbstractSocket::IPv4Protocol){bindIP->setText(ip);break;}}
    render();
   });
  });
 });
}
gamelink::Sample Window::peerSample(const QString& id)const{
 auto s=sample;s.peer.reset();s.ageMs=sampleAge.isValid()?sampleAge.elapsed():6001;s.joiningElapsedMs=joiningAge.elapsed();
 const auto list=QJsonDocument::fromJson(property("peerJSON").toByteArray()).array();
 for(const auto& v:list)if(v.toObject()[S("address")].toString()==id){s.peer=parsePeer(v.toObject());break;}
 return s;
}
void Window::render(){
 updateRoomControls();
 renderZeroTier();
 const bool fresh=localSampleFresh();
 const auto nodeState=fresh ? (sample.online?S("ONLINE"):S("OFFLINE（API 可访问）")) : S("未知 / 状态过期");
 nodeValue->setText(fresh?node:S("等待检测"));
 const auto virtualIPs=fresh?assignedIPs(networks,network).join(S(", ")):QString{};
 ipValue->setText(virtualIPs.isEmpty()?S("尚未分配"):virtualIPs);
 roomValue->setText(roomId.isEmpty()?S("尚未加入"):roomId);
 home->setText(S("节点 %1 · Node %2\n连接策略：%3").arg(nodeState,fresh?node:S("未知"),sessionPolicy.isEmpty()?policy->currentText():sessionPolicy));
 connectionBadge->setText(fresh?(sample.online?S("节点在线"):S("节点离线")):S("等待连接"));
 const auto badgeState=fresh?(sample.online?S("online"):S("offline")):S("unknown");
 if(connectionBadge->property("state").toString()!=badgeState){connectionBadge->setProperty("state",badgeState);connectionBadge->style()->unpolish(connectionBadge);connectionBadge->style()->polish(connectionBadge);}
 sidebarStatus->setText((fresh?(sample.online?S("●  ZeroTier 节点在线"):S("●  ZeroTier 节点离线")):S("●  本地 API 未就绪"))+QLatin1Char('\n')+zeroTierNetworkStatus->text());
 localApiStatus->setText(S("本地 API：")+(localApiMessage.isEmpty()?S("尚未检测"):localApiMessage)+
  (fresh && !apiVersion.isEmpty()?S(" · ZeroTier ")+apiVersion:QString{})+
  (!fresh && sample.serviceAvailable?S("；最近样本已过期"):QString{}));
 peersTable->setRowCount(roomPeers.size());QString states;int row=0;
 for(const auto& v:roomPeers){const auto p=v.toObject();const auto id=p[S("nodeId")].toString();const bool self=id==node;const auto decision=gamelink::classify(peerSample(id));
  QStringList ips;for(const auto& ip:p[S("virtualIps")].toArray())ips<<ip.toString();
  const QStringList cells={p[S("userId")].toString()+S(" / ")+id,ips.join(S(", ")),self?S("SELF"):QString::fromLatin1(gamelink::name(decision.state)),QString::fromStdString(decision.physicalPath),self?S("本机"):QString::fromStdString(decision.reason)};
  for(int c=0;c<cells.size();++c){
   auto* item=new QTableWidgetItem(cells[c]);item->setToolTip(cells[c]);
   if(c==2){item->setForeground(QColor(self?S("#637491"):decision.state==gamelink::State::Direct?S("#197054"):decision.state==gamelink::State::Relay?S("#a66a1f"):S("#637491")));auto font=item->font();font.setBold(true);item->setFont(font);}
   peersTable->setItem(row,c,item);
  }++row;
  if(!self)states+=id+QLatin1Char(' ')+cells[2]+QLatin1Char(' ')+cells[3]+QLatin1Char(' ')+cells[4]+QLatin1Char('\n');
 }
 if(states!=previousStates){previousStates=states;if(!states.isEmpty())log(states.trimmed());}
 const auto id=target->currentData().toString().section(QLatin1Char('|'),0,0);targetState=gamelink::classify(peerSample(id)).state;
 if(!sample.serviceAvailable || !sample.online || !sample.joined || !sampleAge.isValid() || sampleAge.elapsed()>6000){probeHealthy=false;stopProbe();udp.close();}
 if(probeHealthy && (!probeDone.isValid() || probeDone.elapsed()>10000))probeHealthy=false;
}
void Window::launch(){
 if(installer.busy() || networkMutationBusy){log(S("ZeroTier 操作进行中，暂不能从 GameLink 启动游戏。"));return;}
 const auto profile=profiles.value(games->currentData().toString());const bool observe=profile[S("mode")].toString()==S("observe_only");
 if(!roomId.isEmpty() && !observe){ // All room peers require a ready path; probe currently covers only selected target.
  for(const auto& v:roomPeers){const auto id=v.toObject()[S("nodeId")].toString();if(id==node)continue;const auto state=gamelink::classify(peerSample(id)).state;if(state==gamelink::State::Disconnected || state==gamelink::State::Connecting || state==gamelink::State::Failed || state==gamelink::State::NatTraversal){log(S("至少一个房间玩家网络未就绪"));return;}}
 }
 render();
 const auto gate=gamelink::launchGate(selectedPolicy(),targetState,probeHealthy,observe);
 if(!gate.canLaunch){log(QString::fromStdString(gate.reason));return;}
 const QFileInfo exe(gamePath->text());if(!exe.isAbsolute() || !exe.isFile()){log(S("请手动指定有效游戏程序路径"));return;}
 if(QProcess::startDetached(exe.absoluteFilePath(),{},exe.absolutePath()))log(S("已启动游戏；配置未验证，游戏内 LAN/IP 仍需手动选择。")+QString::fromStdString(gate.reason));else log(S("游戏启动失败"));
}
void Window::bindEcho(){
 udp.close();stopProbe();probeHealthy=false;const QHostAddress ip(bindIP->text());bool portOk=false;const int port=probePort->text().toInt(&portOk);
 if(!sample.joined || !sample.online || !sampleAge.isValid() || sampleAge.elapsed()>6000 || !portOk || port<1024 || port>65535 ||
   ip.protocol()!=QAbstractSocket::IPv4Protocol || !assignedIPs(networks,network).contains(ip.toString()) || !QNetworkInterface::allAddresses().contains(ip)){
  log(S("只允许绑定当前已加入网络的本机虚拟 IPv4 和有效端口"));return;
 }
 if(!udp.bind(ip,static_cast<quint16>(port),QUdpSocket::DontShareAddress)){log(S("UDP echo 绑定失败；检查地址/端口占用"));return;}
 activeProbePort=static_cast<quint16>(port);echoBudget=0;log(S("UDP echo 已绑定选定虚拟 IP；未自动开放防火墙"));
}
void Window::startProbe(){
 if(selectedPolicy()==gamelink::Policy::DirectRequired){log(S("严格直连不支持，拒绝发送探测"));return;}
 if(udp.state()!=QAbstractSocket::BoundState){log(S("先启用本机 UDP echo"));return;}
 probeTarget=QHostAddress(target->currentData().toString().section(QLatin1Char('|'),1,1));
 if(probeTarget.protocol()!=QAbstractSocket::IPv4Protocol){log(S("选择已授权房间成员虚拟 IPv4"));return;}
 stopProbe();probeHealthy=false;rtts.clear();probeRunning=true;probeStep();probeTimer.start(750);
}
void Window::stopProbe(){probeTimer.stop();probeRunning=false;probeNonce.clear();}
void Window::probeStep(){
 if(!probeRunning)return;
 if(!probeNonce.isEmpty()){rtts.push_back(std::nullopt);probeNonce.clear();}
 if(rtts.size()>=10){stopProbe();const auto m=gamelink::summarize(rtts);probeHealthy=m.received>=8;probeDone.restart();
  probeInfo->setText(S("RTT %1 ms · jitter %2 ms · loss %3% · %4/%5 收包；仅证明 echo 连通性")
   .arg(m.rttMs,0,'f',1).arg(m.jitterMs,0,'f',1).arg(m.lossPercent,0,'f',1).arg(static_cast<qulonglong>(m.received)).arg(static_cast<qulonglong>(m.sent)));return;}
 probeNonce=QByteArray::number(QRandomGenerator::system()->generate64(),16)+QByteArray::number(QRandomGenerator::system()->generate64(),16);
 probeClock.restart();udp.writeDatagram(QByteArray("GL1Q:")+probeNonce,probeTarget,activeProbePort);
}
void Window::datagrams(){
 int count=0;while(udp.hasPendingDatagrams() && ++count<=128){const auto d=udp.receiveDatagram(128);const auto payload=d.data();
  if(payload.startsWith("GL1R:") && probeRunning && !probeNonce.isEmpty() && payload.mid(5)==probeNonce && d.senderAddress()==probeTarget && d.senderPort()==activeProbePort){rtts.push_back(static_cast<double>(probeClock.nsecsElapsed())/1000000.0);probeNonce.clear();continue;}
  if(!payload.startsWith("GL1Q:") || payload.size()<21 || payload.size()>40 || d.senderPort()!=activeProbePort)continue;
  bool allowed=false;for(const auto& v:roomPeers){const auto p=v.toObject();if(!p[S("authorized")].toBool())continue;for(const auto& ip:p[S("virtualIps")].toArray())if(QHostAddress(ip.toString())==d.senderAddress())allowed=true;}
  const int second=static_cast<int>(QDateTime::currentSecsSinceEpoch()%1000000);if(echoBudget/100!=second)echoBudget=second*100;
  if(!allowed || echoBudget%100>=32)continue;++echoBudget;
  udp.writeDatagram(QByteArray("GL1R:")+payload.mid(5),d.senderAddress(),d.senderPort());
 }
}
