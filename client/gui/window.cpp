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
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>
#define S(x) QStringLiteral(x)
namespace {
QPushButton* button(const QString& title,QVBoxLayout* layout,std::function<void()> action){
 auto* b=new QPushButton(title);layout->addWidget(b);QObject::connect(b,&QPushButton::clicked,b,std::move(action));return b;
}
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
Window::Window():transport(this),settings(),udp(this){
 setWindowTitle(S("GameLink · ZeroTier P2P MVP"));resize(1040,760);
 auto* tabs=new QTabWidget;setCentralWidget(tabs);
 auto page=[tabs](const QString& title){auto* w=new QWidget;auto* l=new QVBoxLayout(w);tabs->addTab(w,title);return l;};
 auto* h=page(S("Home"));home=new QLabel(S("等待本地 ZeroTier 状态"));home->setWordWrap(true);h->addWidget(home);
 capability=new QLabel(S("Direct Required：不支持严格流量保证；模式选择会拒绝加入/启动。\n未配置全局网关、DNS 或游戏代理。"));capability->setWordWrap(true);h->addWidget(capability);h->addStretch();
 auto* g=page(S("Games"));games=new QComboBox;g->addWidget(games);gamePath=new QLineEdit;g->addWidget(gamePath);
 button(S("手动选择游戏程序"),g,[this]{const auto p=QFileDialog::getOpenFileName(this,S("选择游戏程序"));if(!p.isEmpty()){gamePath->setText(p);pathConfigured=true;save();}});
 button(S("检测所选游戏进程"),g,[this]{
#ifdef Q_OS_WIN
  auto* p=new QProcess(this);connect(p,&QProcess::finished,this,[this,p]{const auto text=QString::fromLocal8Bit(p->readAllStandardOutput());log(text.contains(QFileInfo(gamePath->text()).fileName(),Qt::CaseInsensitive)?S("检测到候选进程；网络支持仍需实测"):S("未检测到所选进程"));p->deleteLater();});
  p->start(S("tasklist.exe"),{S("/FO"),S("CSV"),S("/NH")});
#else
  log(S("进程检测仅实现 Windows tasklist；当前平台未支持"));
#endif
 });
 button(S("启动所选游戏"),g,[this]{launch();})->setObjectName(S("launchGameButton"));
 auto* gameNote=new QLabel(S("当前配置均为未验证候选。Warcraft III：传统 LAN 版本；DST：手动虚拟 IP；Overcooked! 2：仅观察 Steam/UDP。自动安装扫描与 LAN 广播实测尚未完成。"));gameNote->setWordWrap(true);g->addWidget(gameNote);g->addStretch();
 auto* r=page(S("Room"));roomInfo=new QLabel(S("没有房间"));roomInfo->setWordWrap(true);r->addWidget(roomInfo);
 roomCode=new QLineEdit;roomCode->setPlaceholderText(S("房间码 id:邀请密钥"));r->addWidget(roomCode);
 button(S("创建房间（当前游戏/策略）"),r,[this]{roomAction(true);})->setObjectName(S("createRoomButton"));button(S("加入房间"),r,[this]{roomAction(false);})->setObjectName(S("joinRoomButton"));
 button(S("加入房间的 ZeroTier 网络"),r,[this]{localJoin();})->setObjectName(S("joinNetworkButton"));button(S("离开房间"),r,[this]{leaveRoom();})->setObjectName(S("leaveRoomButton"));
 button(S("复制房间信息"),r,[this]{QApplication::clipboard()->setText(roomId+QLatin1Char(':')+invite);log(S("已复制邀请信息；请仅分享给可信玩家"));});
 peersTable=new QTableWidget(0,5);peersTable->setHorizontalHeaderLabels({S("玩家 / Node ID"),S("虚拟 IP"),S("状态"),S("物理路径"),S("依据")});peersTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);r->addWidget(peersTable);
 auto* d=page(S("Network Diagnostics"));auto* form=new QFormLayout;d->addLayout(form);
 bindIP=new QLineEdit;bindIP->setPlaceholderText(S("本机已分配虚拟 IPv4"));form->addRow(S("探测监听 IP"),bindIP);
 probePort=new QLineEdit(S("39755"));form->addRow(S("UDP echo 端口"),probePort);
 target=new QComboBox;form->addRow(S("目标成员虚拟 IP"),target);
 button(S("启用本机 UDP echo（双端均需启用）"),d,[this]{bindEcho();});button(S("探测目标：10 次 RTT / jitter / loss"),d,[this]{startProbe();});
 probeInfo=new QLabel(S("尚未探测；防火墙规则需管理员按诊断文档添加"));d->addWidget(probeInfo);
 events=new QPlainTextEdit;events->setReadOnly(true);events->setMaximumBlockCount(1000);d->addWidget(events);
 auto* st=page(S("Settings"));auto* sf=new QFormLayout;st->addLayout(sf);
 serverUrl=new QLineEdit(settings.value(S("server"),S("http://127.0.0.1:8080")).toString());sf->addRow(S("GameLink Server"),serverUrl);
 serverToken=new QLineEdit;serverToken->setEchoMode(QLineEdit::Password);sf->addRow(S("GameLink 凭证（仅内存）"),serverToken);
#ifdef Q_OS_WIN
 const auto tokenDefault=qEnvironmentVariable("LOCALAPPDATA")+S("/GameLink/zerotier-token.secret");
#else
 const auto tokenDefault=QDir::homePath()+S("/.config/gamelink/zerotier-token.secret");
#endif
 ztTokenFile=new QLineEdit(settings.value(S("ztTokenFile"),tokenDefault).toString());sf->addRow(S("本地 ZT Token 文件"),ztTokenFile);
 ztPort=new QLineEdit(settings.value(S("ztPort"),S("9993")).toString());sf->addRow(S("本地 API 端口"),ztPort);
 manualNetwork=new QLineEdit(settings.value(S("network")).toString());sf->addRow(S("手动网络 ID（诊断）"),manualNetwork);
 policy=new QComboBox;policy->addItems({S("Preferred Direct"),S("Direct Required — unsupported"),S("Relay Allowed")});sf->addRow(S("新房间策略"),policy);
 button(S("保存设置并刷新"),st,[this]{save();pollLocal();});
 button(S("导出脱敏事件日志"),st,[this]{const auto p=QFileDialog::getSaveFileName(this,S("导出日志"),S("gamelink-events.txt"));if(!p.isEmpty()){QFile f(p);if(f.open(QIODevice::WriteOnly))f.write(events->toPlainText().toUtf8());}});
 home->setObjectName(S("homeStatus"));roomInfo->setObjectName(S("roomStatus"));
 games->setObjectName(S("gameSelector"));policy->setObjectName(S("policySelector"));
 serverUrl->setObjectName(S("serverUrl"));serverToken->setObjectName(S("serverToken"));
 ztTokenFile->setObjectName(S("ztTokenFile"));ztPort->setObjectName(S("ztPort"));
 roomCode->setObjectName(S("roomCode"));events->setObjectName(S("events"));
 peersTable->setObjectName(S("peersTable"));target->setObjectName(S("probeTarget"));
 st->addWidget(new QLabel(S("自动启动暂未实现；不保存任何明文 Token，不自动修改网络/防火墙。")));st->addStretch();
 const QDir profileDir(QCoreApplication::applicationDirPath()+S("/profiles"));
 for(const auto& file:profileDir.entryList({S("*.json")},QDir::Files)){
  QFile f(profileDir.filePath(file));if(!f.open(QIODevice::ReadOnly))continue;
  const auto p=QJsonDocument::fromJson(f.readAll()).object();
  if(p[S("schema_version")].toInt()!=1 || p[S("steam_api_interception")].toBool(true))continue;
  const auto id=p[S("id")].toString();const auto mode=p[S("mode")].toString();
  if(id.isEmpty() || (mode!=S("virtual_lan") && mode!=S("virtual_ip") && mode!=S("observe_only")))continue;
  profiles[id]=p;games->addItem(p[S("name")].toString()+S(" · ")+mode+S(" · unverified"),id);
 }
 connect(games,&QComboBox::currentIndexChanged,this,[this]{chooseGame();});chooseGame();
 connect(target,&QComboBox::currentIndexChanged,this,[this]{stopProbe();probeHealthy=false;render();});
 connect(&pollTimer,&QTimer::timeout,this,[this]{pollLocal();pollRoom();});pollTimer.start(2000);
 connect(&expiryTimer,&QTimer::timeout,this,[this]{render();});expiryTimer.start(500);
 connect(&udp,&QUdpSocket::readyRead,this,[this]{datagrams();});
 connect(&probeTimer,&QTimer::timeout,this,[this]{probeStep();});
 joiningAge.start();pollLocal();log(S("MVP 启动；严格直连不可用；NAT 原因仅凭 local API 无法确定"));
}
void Window::log(const QString& s){events->appendPlainText(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)+QLatin1Char(' ')+s);}
void Window::save(){settings.setValue(S("server"),serverUrl->text());settings.setValue(S("ztTokenFile"),ztTokenFile->text());settings.setValue(S("ztPort"),ztPort->text());settings.setValue(S("network"),manualNetwork->text());if(pathConfigured)settings.setValue(S("gamePath/")+games->currentData().toString(),gamePath->text());}
void Window::chooseGame(){gamePath->setText(settings.value(S("gamePath/")+games->currentData().toString()).toString());pathConfigured=!gamePath->text().isEmpty();}
gamelink::Policy Window::selectedPolicy()const{
 const auto selected=sessionPolicy.isEmpty() ? (policy->currentIndex()==1?S("direct_required"):policy->currentIndex()==2?S("relay_allowed"):S("preferred_direct")) : sessionPolicy;
 return selected==S("direct_required")?gamelink::Policy::DirectRequired:selected==S("relay_allowed")?gamelink::Policy::RelayAllowed:gamelink::Policy::PreferredDirect;
}
void Window::api(const QString& method,const QString& path,const QJsonObject& body,Transport::Callback cb){transport.room(serverUrl->text(),serverToken->text(),method,path,body,std::move(cb));}
void Window::roomAction(bool create){
 if(mutationBusy || !roomId.isEmpty()){log(S("请先离开当前房间或等待请求完成"));return;}
 if(policy->currentIndex()==1){log(S("拒绝：Direct Required 无可靠流量控制"));return;}
 if(games->currentData().toString().isEmpty()){log(S("未加载游戏配置"));return;}
 QString path=S("/api/v1/rooms");QJsonObject b;
 if(create){b={{S("gameId"),games->currentData().toString()},{S("policy"),policy->currentIndex()==2?S("relay_allowed"):S("preferred_direct")}};}
 else{
  const auto parts=roomCode->text().trimmed().split(QLatin1Char(':'));
  if(parts.size()!=2 || !QRegularExpression(S("^[0-9a-f]{16}$")).match(parts[0]).hasMatch() || !QRegularExpression(S("^[0-9a-f]{32}$")).match(parts[1]).hasMatch()){log(S("房间码格式为 16 位十六进制 id:32 位邀请密钥"));return;}
  path+=QLatin1Char('/')+parts[0]+S("/join");b={{S("joinKey"),parts[1]}};invite=parts[1];
 }
 mutationBusy=true;
 api(S("POST"),path,b,[this](QJsonDocument doc,QString err){mutationBusy=false;if(!err.isEmpty()){log(err);return;}applyRoom(doc.object());pollRoom();});
}
void Window::applyRoom(const QJsonObject& r){
 const auto id=r[S("id")].toString(),n=r[S("networkId")].toString();
 if(!QRegularExpression(S("^[0-9a-f]{16}$")).match(id).hasMatch() || !QRegularExpression(S("^[0-9a-f]{16}$")).match(n).hasMatch()){log(S("服务器房间数据不合法"));return;}
 roomId=id;network=n;sessionPolicy=r[S("policy")].toString();
 if(!r[S("joinKey")].toString().isEmpty())invite=r[S("joinKey")].toString();
 roomInfo->setText(S("房间 %1 · 网络 %2 · %3 · 有效至 %4").arg(roomId,network,sessionPolicy,r[S("expiresAt")].toString()));
 joiningAge.restart();probeHealthy=false;sample={};log(S("房间已授权；请显式加入 ZeroTier 网络"));
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
void Window::localJoin(){
 if(selectedPolicy()==gamelink::Policy::DirectRequired){log(S("拒绝严格直连 join：unsupported"));return;}
 const auto n=roomId.isEmpty()?manualNetwork->text().trimmed():network;
 if(!QRegularExpression(S("^[0-9a-f]{16}$")).match(n).hasMatch()){log(S("网络 ID 无效"));return;}
 if(!roomId.isEmpty()){
  bool owns=false;for(const auto& v:roomPeers)if(v.toObject()[S("nodeId")].toString()==node && v.toObject()[S("authorized")].toBool())owns=true;
  if(!owns){log(S("本机 Node ID 与服务器身份绑定不符或成员数据未就绪"));return;}
 }
 for(const auto& v:networks){const auto existing=v.toObject();if(existing[S("id")].toString()==n || existing[S("nwid")].toString()==n){
  const bool unsafe=existing[S("allowDefault")].toBool() || existing[S("allowGlobal")].toBool() || existing[S("allowDNS")].toBool();
  network=n;joiningAge.restart();if(unsafe)log(S("已有网络启用 default/global/DNS；保留原配置，请用官方客户端审核。GameLink 拒绝自动更改"));else log(S("网络已存在；保留原配置"));return;
 }}
 transport.local(ztPort->text().toInt(),ztTokenFile->text(),S("POST"),S("/network/")+n,
  {{S("allowManaged"),true},{S("allowDefault"),false},{S("allowGlobal"),false},{S("allowDNS"),false}},
  [this,n](QJsonDocument,QString err){if(!err.isEmpty()){log(err);return;}network=n;joiningAge.restart();sample={};probeHealthy=false;log(S("已请求加入网络；不会自动修改其他网络或 leave 既有网络"));pollLocal();});
}
void Window::leaveRoom(){
 if(roomId.isEmpty() || mutationBusy)return;mutationBusy=true;
 api(S("POST"),S("/api/v1/rooms/")+roomId+S("/leave"),{},[this](QJsonDocument,QString err){mutationBusy=false;if(!err.isEmpty()){log(err);return;}clearRoom();roomInfo->setText(S("已离开；Controller 撤销有传播延迟，本地 ZT membership 保留"));});
}
void Window::clearRoom(){
 roomId.clear();invite.clear();sessionPolicy.clear();network.clear();roomPeers={};target->clear();stopProbe();udp.close();sample={};probeHealthy=false;roomInfo->setText(S("没有房间"));render();
}
void Window::pollLocal(){
 if(localBusy)return;localBusy=true;const int port=ztPort->text().toInt();const auto token=ztTokenFile->text();const auto expectedNetwork=network;QElapsedTimer batchClock;batchClock.start();
 // Sample peers before status.clock so lastReceive isn't compared to an earlier clock.
 transport.local(port,token,S("GET"),S("/peer"),{},[this,port,token,expectedNetwork,batchClock](QJsonDocument peers,QString err){
  if(!err.isEmpty() || !peers.isArray()){localBusy=false;sample={};probeHealthy=false;render();return;}
  transport.local(port,token,S("GET"),S("/status"),{},[this,port,token,expectedNetwork,peers,batchClock](QJsonDocument status,QString err2){
   if(!err2.isEmpty() || !status.isObject()){localBusy=false;sample={};probeHealthy=false;render();return;}
   const auto info=status.object();
   transport.local(port,token,S("GET"),S("/network"),{},[this,expectedNetwork,info,peers,batchClock](QJsonDocument ns,QString err3){
    localBusy=false;if(expectedNetwork!=network)return;
    if(!err3.isEmpty() || !ns.isArray()){sample={};probeHealthy=false;render();return;}
    const auto newNode=info[S("address")].toString();const auto key=newNode+QLatin1Char('|')+network+QLatin1Char('|')+serverUrl->text()+QLatin1Char('|')+ztTokenFile->text()+QLatin1Char('|')+ztPort->text()+QLatin1Char('|')+assignedIPs(ns.array(),network).join(S(","));
    if(key!=contextKey){contextKey=key;joiningAge.restart();probeHealthy=false;stopProbe();udp.close();}
    node=newNode;networks=ns.array();const bool wasReady=sample.serviceAvailable && sample.online && sample.joined;sample={};
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
 home->setText(S("ZeroTier %1 · Node %2\n虚拟 IP：%3\n房间：%4 · 策略：%5\nONLINE 仅代表服务状态；玩家路径见 Room。")
  .arg(sample.serviceAvailable && sample.online && sampleAge.isValid() && sampleAge.elapsed()<=6000?S("ONLINE"):S("DISCONNECTED / STALE"),node,assignedIPs(networks,network).join(S(", ")),roomId,
       sessionPolicy.isEmpty()?policy->currentText():sessionPolicy));
 peersTable->setRowCount(roomPeers.size());QString states;int row=0;
 for(const auto& v:roomPeers){const auto p=v.toObject();const auto id=p[S("nodeId")].toString();const bool self=id==node;const auto decision=gamelink::classify(peerSample(id));
  QStringList ips;for(const auto& ip:p[S("virtualIps")].toArray())ips<<ip.toString();
  const QStringList cells={p[S("userId")].toString()+S(" / ")+id,ips.join(S(", ")),self?S("SELF"):QString::fromLatin1(gamelink::name(decision.state)),QString::fromStdString(decision.physicalPath),self?S("本机"):QString::fromStdString(decision.reason)};
  for(int c=0;c<cells.size();++c)peersTable->setItem(row,c,new QTableWidgetItem(cells[c]));++row;
  if(!self)states+=id+QLatin1Char(' ')+cells[2]+QLatin1Char(' ')+cells[3]+QLatin1Char(' ')+cells[4]+QLatin1Char('\n');
 }
 if(states!=previousStates){previousStates=states;if(!states.isEmpty())log(states.trimmed());}
 const auto id=target->currentData().toString().section(QLatin1Char('|'),0,0);targetState=gamelink::classify(peerSample(id)).state;
 if(!sample.serviceAvailable || !sample.online || !sample.joined || !sampleAge.isValid() || sampleAge.elapsed()>6000){probeHealthy=false;stopProbe();udp.close();}
 if(probeHealthy && (!probeDone.isValid() || probeDone.elapsed()>10000))probeHealthy=false;
}
void Window::launch(){
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
 int count=0;while(udp.hasPendingDatagrams() && ++count<=128){const auto d=udp.receiveDatagram(128);const auto data=d.data();
  if(data.startsWith("GL1R:") && probeRunning && !probeNonce.isEmpty() && data.mid(5)==probeNonce && d.senderAddress()==probeTarget && d.senderPort()==activeProbePort){rtts.push_back(static_cast<double>(probeClock.nsecsElapsed())/1000000.0);probeNonce.clear();continue;}
  if(!data.startsWith("GL1Q:") || data.size()<21 || data.size()>40 || d.senderPort()!=activeProbePort)continue;
  bool allowed=false;for(const auto& v:roomPeers){const auto p=v.toObject();if(!p[S("authorized")].toBool())continue;for(const auto& ip:p[S("virtualIps")].toArray())if(QHostAddress(ip.toString())==d.senderAddress())allowed=true;}
  const int second=static_cast<int>(QDateTime::currentSecsSinceEpoch()%1000000);if(echoBudget/100!=second)echoBudget=second*100;
  if(!allowed || echoBudget%100>=32)continue;++echoBudget;
  udp.writeDatagram(QByteArray("GL1R:")+data.mid(5),d.senderAddress(),d.senderPort());
 }
}
