#include "window.hpp"
#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#define S(x) QStringLiteral(x)
namespace {
class DropDown : public QComboBox {
protected:
 void paintEvent(QPaintEvent* event) override {
  QComboBox::paintEvent(event);
  QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
  p.setPen(QPen(QColor(S("#697b98")),1.6,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
  const qreal x=width()-18,y=height()/2.0;
  QPainterPath arrow;arrow.moveTo(x-4,y-2);arrow.lineTo(x,y+2);arrow.lineTo(x+4,y-2);p.drawPath(arrow);
 }
};
QLabel* text(const QString& value, const char* role = "body") {
 auto* label = new QLabel(value);
 label->setProperty("role", QString::fromLatin1(role));
 label->setTextFormat(Qt::PlainText);
 label->setWordWrap(true);
 label->setTextInteractionFlags(Qt::TextSelectableByMouse);
 return label;
}
QVBoxLayout* card(QBoxLayout* parent, const QString& title, const QString& subtitle = {}) {
 auto* frame = new QFrame;
 frame->setObjectName(S("card"));
 auto* layout = new QVBoxLayout(frame);
 layout->setContentsMargins(22,20,22,20);
 layout->setSpacing(12);
 if(!title.isEmpty()) layout->addWidget(text(title,"cardTitle"));
 if(!subtitle.isEmpty()) layout->addWidget(text(subtitle,"muted"));
 parent->addWidget(frame);
 return layout;
}
QPushButton* action(const QString& title, QBoxLayout* parent, std::function<void()> callback,
                    const char* role = "secondary", const char* name = nullptr) {
 auto* button = new QPushButton(title);
 button->setProperty("role",QString::fromLatin1(role));
 if(name) button->setObjectName(QString::fromLatin1(name));
 button->setCursor(Qt::PointingHandCursor);
 parent->addWidget(button);
 QObject::connect(button,&QPushButton::clicked,button,std::move(callback));
 return button;
}
QHBoxLayout* row(QVBoxLayout* parent) {
 auto* layout = new QHBoxLayout;layout->setSpacing(10);parent->addLayout(layout);return layout;
}
QFormLayout* form(QVBoxLayout* parent) {
 auto* layout = new QFormLayout;layout->setHorizontalSpacing(20);layout->setVerticalSpacing(14);
 layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
 layout->setLabelAlignment(Qt::AlignLeft|Qt::AlignVCenter);
 layout->setRowWrapPolicy(QFormLayout::WrapLongRows);
 parent->addLayout(layout);return layout;
}
QIcon navigationIcon(int kind, const QColor& color) {
 // Local vector strokes stay crisp at high DPI and need no icon-font dependency.
 QPixmap pixmap(48,48);pixmap.setDevicePixelRatio(2);pixmap.fill(Qt::transparent);
 QPainter p(&pixmap);p.setRenderHint(QPainter::Antialiasing);
 p.setPen(QPen(color,1.7,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
 if(kind==0){QPainterPath path;path.moveTo(3,11);path.lineTo(12,3);path.lineTo(21,11);path.moveTo(6,9);path.lineTo(6,21);path.lineTo(18,21);path.lineTo(18,9);path.moveTo(10,21);path.lineTo(10,15);path.lineTo(14,15);path.lineTo(14,21);p.drawPath(path);}
 else if(kind==1){p.drawRoundedRect(QRectF(3,7,18,12),4,4);p.drawLine(7,11,7,15);p.drawLine(5,13,9,13);p.drawEllipse(QPointF(16,12),.8,.8);p.drawEllipse(QPointF(18,15),.8,.8);}
 else if(kind==2){p.drawEllipse(QPointF(9,7),3,3);p.drawArc(QRectF(3,12,12,10),0,180*16);p.drawArc(QRectF(13,4,6,6),-80*16,160*16);p.drawArc(QRectF(13,12,8,9),0,90*16);}
 else if(kind==4){p.drawLine(3,19,21,19);QPainterPath path;path.moveTo(3,14);path.lineTo(7,14);path.lineTo(10,5);path.lineTo(14,17);path.lineTo(17,10);path.lineTo(21,10);p.drawPath(path);}
 else if(kind==3){p.drawRoundedRect(QRectF(7,2,10,6),1,1);p.drawLine(12,8,12,13);p.drawLine(5,13,19,13);p.drawLine(5,13,5,17);p.drawLine(19,13,19,17);p.drawRect(QRectF(2,17,6,5));p.drawRect(QRectF(16,17,6,5));}
 else {p.drawEllipse(QPointF(12,12),7,7);p.drawEllipse(QPointF(12,12),2.4,2.4);for(int i=0;i<8;++i){p.save();p.translate(12,12);p.rotate(i*45);p.drawLine(0,8,0,10);p.restore();}}
 return QIcon(pixmap);
}
}

void Window::buildUi(){
 setWindowTitle(S("GameLink v%1 · 游戏联机").arg(QCoreApplication::applicationVersion()));resize(1180,840);setMinimumSize(960,680);
 auto font = QApplication::font();
#ifdef Q_OS_WIN
 // Keep Chinese UI text in a sans-serif family rather than the system's
 // legacy SimSun fallback for Latin-only Segoe fonts.
 font.setFamilies({S("Segoe UI Variable"),S("Segoe UI"),S("Microsoft YaHei UI"),S("Microsoft YaHei"),S("SimHei")});
#endif
 font.setPointSize(10);setFont(font);
 setStyleSheet(S(R"(
 QMainWindow, QWidget#shell { background:#f6f7fb; color:#202939; }
 QWidget { color:#202939; font-size:13px; }
 QWidget#sidebar { background:#eef1f8; border-right:1px solid #e0e5ef; }
 QLabel { background:transparent; border:0; }
 QLabel[role="brand"] { font-size:24px; font-weight:700; color:#243558; }
 QLabel#brandMark { background:#3864e8; color:white; border-radius:13px; font-size:24px; font-weight:700; }
 QLabel[role="eyebrow"] { font-size:11px; color:#6a7a94; font-weight:600; }
 QLabel[role="pageTitle"] { font-size:28px; font-weight:700; color:#17233b; }
 QLabel[role="cardTitle"] { font-size:16px; font-weight:600; color:#25334c; }
 QLabel[role="muted"] { color:#69758b; font-size:12px; }
 QLabel[role="metric"] { font-size:21px; font-weight:600; color:#223559; }
 QLabel[role="heroTitle"] { font-size:20px; font-weight:600; color:#244485; }
 QLabel[role="badge"] { background:#e8edf7; color:#526481; border-radius:12px; padding:6px 12px; font-size:12px; font-weight:600; }
 QLabel[role="badge"][state="online"] { background:#e3f4ed; color:#197054; }
 QLabel[role="badge"][state="offline"] { background:#fff1dc; color:#946018; }
 QLabel#sidebarStatus { color:#637491; font-size:12px; }
 QLabel#lastEvent { color:#5a6b84; font-size:12px; padding:4px 0; }
 QFrame#card { background:white; border:1px solid #e1e6f0; border-radius:16px; }
 QFrame#hero { background:#eaf0ff; border:1px solid #d7e2ff; border-radius:16px; }
 QFrame#notice { background:#fff6e6; border:1px solid #f3e3c4; border-radius:12px; }
 QPushButton { min-height:20px; padding:10px 16px; border:1px solid #dce2ee; border-radius:8px; background:#fff; font-weight:500; }
 QPushButton:hover { background:#f1f5ff; border-color:#bccbf0; }
 QPushButton:pressed { background:#e3ebfc; }
 QPushButton:focus { border:1px solid #3864e8; }
 QPushButton:disabled { background:#f4f6fa; color:#9aa5b5; border-color:#e7ebf2; }
 QPushButton[role="primary"] { background:#3864e8; color:#fff; border-color:#3864e8; font-weight:600; }
 QPushButton[role="primary"]:hover { background:#2b55d4; border-color:#2b55d4; }
 QPushButton[role="primary"]:pressed { background:#2147ba; }
 QPushButton[role="primary"]:disabled { background:#dce5fb; color:#8b9ac0; border-color:#dce5fb; }
 QPushButton[role="nav"] { text-align:left; padding:12px 16px; border:1px solid transparent; background:transparent; color:#56657d; font-size:14px; }
 QPushButton[role="nav"]:hover { background:#e4eaf6; }
 QPushButton[role="nav"]:checked { background:#fff; border-color:#dee5f2; color:#315ed8; font-weight:600; }
 QLineEdit, QComboBox { background:#fbfcff; border:1px solid #dce2ee; border-radius:7px; padding:9px 11px; min-height:19px; selection-background-color:#3864e8; }
 QLineEdit:focus, QComboBox:focus { border:1px solid #3864e8; background:white; }
 QLineEdit:disabled { color:#8b96a9; }
 QComboBox { padding-right:28px; }
 QComboBox::drop-down { border:0; width:26px; }
 QComboBox::down-arrow { width:0; height:0; }
 QComboBox QAbstractItemView { background:white; selection-background-color:#eaf0ff; selection-color:#244485; border:1px solid #dce2ee; padding:4px; }
 QScrollArea { border:0; background:transparent; }
 QScrollArea > QWidget > QWidget { background:transparent; }
 QScrollBar:vertical { background:transparent; width:10px; margin:4px 1px; }
 QScrollBar::handle:vertical { background:#cbd3e3; border-radius:4px; min-height:28px; }
 QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }
 QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background:transparent; }
 QTableWidget { background:white; alternate-background-color:#f8faff; border:0; gridline-color:#edf0f6; selection-background-color:#eaf0ff; selection-color:#223559; }
 QTableWidget::item { padding:9px 6px; border-bottom:1px solid #edf0f6; }
 QHeaderView::section { background:#f3f6fb; color:#6b7890; border:0; padding:11px 6px; font-size:12px; font-weight:600; }
 QTableCornerButton::section { background:#f3f6fb; border:0; }
 QPlainTextEdit { background:#f8faff; color:#52637d; border:1px solid #e3e8f2; border-radius:8px; padding:10px; selection-background-color:#3864e8; }
 QToolTip { color:#25334c; background:white; border:1px solid #dce2ee; padding:6px; }
 )"));
 auto* shell=new QWidget;shell->setObjectName(S("shell"));setCentralWidget(shell);
 auto* root=new QHBoxLayout(shell);root->setContentsMargins(0,0,0,0);root->setSpacing(0);
 auto* sidebar=new QWidget;sidebar->setObjectName(S("sidebar"));sidebar->setFixedWidth(214);root->addWidget(sidebar);
 auto* side=new QVBoxLayout(sidebar);side->setContentsMargins(18,28,18,22);side->setSpacing(8);
 auto* brand=new QHBoxLayout;brand->setSpacing(10);
 auto* mark=text(S("G"));mark->setObjectName(S("brandMark"));mark->setFixedSize(42,42);mark->setAlignment(Qt::AlignCenter);
 brand->addWidget(mark);brand->addWidget(text(S("GameLink"),"brand"));side->addLayout(brand);
 side->addWidget(text(S("让联机回到一起"),"muted"));side->addSpacing(30);
 side->addWidget(text(S("工作空间 / WORKSPACE"),"eyebrow"));side->addSpacing(6);
 auto* stack=new QStackedWidget;stack->setObjectName(S("pageStack"));
 auto* navigation=new QButtonGroup(this);navigation->setObjectName(S("navigation"));navigation->setExclusive(true);
 const QStringList names={S("联机概览"),S("我的游戏"),S("联机房间"),S("ZeroTier 状态"),S("网络诊断"),S("设置")};
 const QStringList subtitles={S("查看本机环境与连接状态，准备下一场游戏。"),S("选择游戏、配置启动路径，与伙伴一起出发。"),S("创建或加入房间，查看每一位玩家的连接。"),S("管理本机虚拟网络，查看授权、IP 与服务状态。"),S("检查玩家之间的链路，查看延迟与运行日志。"),S("管理房间服务、本地 ZeroTier 与连接偏好。")};
 const QStringList sections={S("OVERVIEW"),S("GAME LIBRARY"),S("YOUR ROOM"),S("ZEROTIER"),S("DIAGNOSTICS"),S("PREFERENCES")};
 auto* content=new QVBoxLayout;content->setContentsMargins(0,0,0,0);content->setSpacing(0);root->addLayout(content,1);
 QVector<QVBoxLayout*> pages;
 for(int i=0;i<PageCount;++i){
  auto* nav=new QPushButton(names[i]);nav->setObjectName(S("navigation%1").arg(i));nav->setProperty("role",S("nav"));nav->setCheckable(true);nav->setIcon(navigationIcon(i,QColor(S("#647796"))));nav->setIconSize(QSize(22,22));nav->setCursor(Qt::PointingHandCursor);navigation->addButton(nav,i);side->addWidget(nav);
  auto* scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto* page=new QWidget;page->setObjectName(S("page%1").arg(i));auto* layout=new QVBoxLayout(page);layout->setContentsMargins(30,27,30,24);layout->setSpacing(18);
  layout->addWidget(text(sections[i],"eyebrow"));auto* heading=text(names[i],"pageTitle");heading->setObjectName(S("pageTitle%1").arg(i));layout->addWidget(heading);layout->addWidget(text(subtitles[i],"muted"));layout->addSpacing(4);
  scroll->setWidget(page);stack->addWidget(scroll);pages<<layout;
 }
 connect(navigation,&QButtonGroup::idClicked,stack,&QStackedWidget::setCurrentIndex);
 connect(stack,&QStackedWidget::currentChanged,this,[navigation](int i){navigation->button(i)->setChecked(true);});navigation->button(0)->setChecked(true);
 side->addStretch();sidebarStatus=text(S("●  等待本机状态"));sidebarStatus->setObjectName(S("sidebarStatus"));side->addWidget(sidebarStatus);side->addSpacing(8);
 auto* clientVersion=text(S("GameLink 客户端 v")+QCoreApplication::applicationVersion(),"muted");clientVersion->setObjectName(S("clientVersionLabel"));side->addWidget(clientVersion);side->addWidget(text(S("ZeroTier 游戏联机客户端"),"muted"));
 content->addWidget(stack,1);
 lastEvent=text(S("就绪 · 请先在设置中配置本地凭证"));lastEvent->setObjectName(S("lastEvent"));lastEvent->setContentsMargins(30,0,30,14);lastEvent->setMaximumHeight(70);content->addWidget(lastEvent);

 auto* h=pages[0];auto* hero=card(h,{});hero->parentWidget()->setObjectName(S("hero"));
 auto* heroTop=row(hero);heroTop->addWidget(text(S("你的联机工作台"),"heroTitle"),1);
 connectionBadge=text(S("等待检测"),"badge");connectionBadge->setObjectName(S("connectionBadge"));connectionBadge->setWordWrap(false);heroTop->addWidget(connectionBadge);
 home=text(S("等待本地 ZeroTier 状态"),"muted");home->setObjectName(S("homeStatus"));hero->addWidget(home);
 auto* shortcut=row(hero);action(S("进入联机房间"),shortcut,[stack]{stack->setCurrentIndex(RoomPage);},"primary");action(S("选择游戏"),shortcut,[stack]{stack->setCurrentIndex(GamesPage);});shortcut->addStretch();
 auto* metrics=row(h);
 auto metric=[metrics](const QString& title,const QString& initial){auto* c=card(metrics,title);auto* value=text(initial,"metric");c->addWidget(value);return value;};
 nodeValue=metric(S("本机 NODE ID"),S("等待检测"));nodeValue->setObjectName(S("nodeValue"));ipValue=metric(S("虚拟 IP"),S("尚未分配"));ipValue->setObjectName(S("ipValue"));roomValue=metric(S("当前房间"),S("尚未加入"));roomValue->setObjectName(S("roomValue"));roomValue->setProperty("role",S("body"));
 auto* z=pages[ZeroTierPage];
 auto* ztSummary=card(z,S("节点与目标网络"));
 zeroTierNodeStatus=text(S("等待检测"));zeroTierNodeStatus->setObjectName(S("ztNodeStatus"));ztSummary->addWidget(zeroTierNodeStatus);
 zeroTierNetworkId=text(S("尚未选择网络"),"muted");zeroTierNetworkId->setObjectName(S("ztTargetNetwork"));ztSummary->addWidget(zeroTierNetworkId);
 zeroTierNetworkStatus=text(S("未加入"),"metric");zeroTierNetworkStatus->setObjectName(S("ztNetworkStatus"));ztSummary->addWidget(zeroTierNetworkStatus);
 credentialStatus=text({},"muted");credentialStatus->setObjectName(S("ztCredentialStatus"));ztSummary->addWidget(credentialStatus);
 auto* credentialActions=row(ztSummary);detectCredentialButton=action(S("检测本机凭据"),credentialActions,[this]{detectCredentials();},"secondary","ztDetectCredentialsButton");credentialActions->addStretch();
 auto* controls=card(z,S("虚拟网络"),S("Network ID 与房间 ID 不同。有房间时加入房间指定网络；退网仅作用于表格中选中的网络，房间与控制器授权保留。"));
 auto* networkForm=form(controls);manualNetwork=new QLineEdit(settings.value(S("network")).toString());manualNetwork->setObjectName(S("manualNetwork"));manualNetwork->setPlaceholderText(S("输入 16 位 Network ID"));networkForm->addRow(S("Network ID"),manualNetwork);
 auto* networkActions=row(controls);joinZeroTier=action(S("加入网络"),networkActions,[this]{localJoin();},"primary","ztJoinButton");leaveZeroTier=action(S("离开选中网络"),networkActions,[this]{localLeave();},"secondary","ztLeaveButton");action(S("刷新状态"),networkActions,[this]{refreshEnvironment();pollLocal();},"secondary","ztRefreshButton");networkActions->addStretch();
 networkOperationStatus=text(S("加入后等待控制器授权与 IP 分配；不会自动修改默认路由或 DNS。"),"muted");networkOperationStatus->setObjectName(S("ztOperationStatus"));controls->addWidget(networkOperationStatus);
 networksTable=new QTableWidget(0,4);networksTable->setObjectName(S("ztNetworksTable"));networksTable->setHorizontalHeaderLabels({S("Network ID"),S("网络名称"),S("入网状态"),S("虚拟 IP")});networksTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);networksTable->verticalHeader()->hide();networksTable->verticalHeader()->setDefaultSectionSize(56);networksTable->setWordWrap(true);networksTable->setShowGrid(false);networksTable->setAlternatingRowColors(true);networksTable->setEditTriggers(QAbstractItemView::NoEditTriggers);networksTable->setSelectionBehavior(QAbstractItemView::SelectRows);networksTable->setSelectionMode(QAbstractItemView::SingleSelection);networksTable->setMinimumHeight(190);controls->addWidget(networksTable);
 connect(networksTable,&QTableWidget::itemSelectionChanged,this,[this]{renderZeroTier();});
 connect(manualNetwork,&QLineEdit::textEdited,this,[this]{save();renderZeroTier();});
 auto* quickStatus=card(h,S("ZeroTier 网络"),S("节点在线与入网成功分别显示。授权、IP 与已加入网络可在独立状态页查看。"));auto* quickActions=row(quickStatus);action(S("打开 ZeroTier 状态"),quickActions,[stack]{stack->setCurrentIndex(ZeroTierPage);},"secondary","openZeroTierButton");quickActions->addStretch();
 auto* environmentCard=card(z,S("ZeroTier 环境"),S("安装、服务与 API 独立检测；节点在线不代表玩家已直连。"));
 auto statusRow=[environmentCard](const QString& title,QLabel*& value,const char* name){auto* r=row(environmentCard);auto* label=text(title,"muted");label->setFixedWidth(90);r->addWidget(label,0,Qt::AlignTop);value=text({});value->setObjectName(QString::fromLatin1(name));r->addWidget(value,1);};
 statusRow(S("安装检测"),installationStatus,"installationStatus");statusRow(S("系统服务"),serviceStatus,"serviceStatus");statusRow(S("本地 API"),localApiStatus,"localApiStatus");
 portStatus=text({},"muted");environmentCard->addWidget(portStatus);
 auto* checks=row(environmentCard);action(S("重新检测"),checks,[this]{refreshEnvironment();pollLocal();},"secondary","refreshEnvironmentButton");useDetectedPort=action(S("应用检测端口"),checks,[this]{if(!installer.busy() && !networkMutationBusy && environment.detectedPort){ztPort->setText(QString::number(environment.detectedPort));save();pollLocal();}},"secondary","useDetectedPortButton");checks->addStretch();
 capability=text(S("优先直连允许回退中继。严格直连暂不可用，可在设置中查看连接策略。"),"muted");environmentCard->addWidget(capability);h->addStretch();

 auto* g=pages[1];auto* library=card(g,S("游戏配置"),S("当前提供 3 个候选配置，实际游戏联机兼容性仍待验证。"));
 games=new DropDown;games->setObjectName(S("gameSelector"));library->addWidget(games);
 gameDetails=text(S("选择一个游戏查看连接方式。"),"muted");gameDetails->setObjectName(S("gameDetails"));library->addWidget(gameDetails);
 auto* game=card(g,S("启动程序"),S("手动指定已安装的游戏程序，路径按游戏单独保存。"));
 gamePath=new QLineEdit;gamePath->setObjectName(S("gamePath"));gamePath->setPlaceholderText(S("选择游戏可执行文件，例如 C:\\Games\\game.exe"));game->addWidget(gamePath);
 connect(gamePath,&QLineEdit::textEdited,this,[this]{pathConfigured=true;});
 auto* gameActions=row(game);action(S("浏览文件"),gameActions,[this]{const auto p=QFileDialog::getOpenFileName(this,S("选择游戏程序"));if(!p.isEmpty()){gamePath->setText(p);pathConfigured=true;save();}});
 action(S("检测进程"),gameActions,[this]{
#ifdef Q_OS_WIN
  if(gamePath->text().trimmed().isEmpty()){log(S("请先选择游戏程序，再检测进程"));return;}
  auto* p=new QProcess(this);connect(p,&QProcess::finished,this,[this,p]{const auto output=QString::fromLocal8Bit(p->readAllStandardOutput());log(output.contains(QFileInfo(gamePath->text()).fileName(),Qt::CaseInsensitive)?S("检测到候选进程；网络支持仍需实测"):S("未检测到所选进程"));p->deleteLater();});p->start(S("tasklist.exe"),{S("/FO"),S("CSV"),S("/NH")});
#else
  log(S("进程检测仅支持 Windows"));
#endif
 });gameActions->addStretch();action(S("启动游戏"),gameActions,[this]{launch();},"primary","launchGameButton");
 auto* gameNotice=card(g,S("联机提示"));gameNotice->addWidget(text(S("Warcraft III 使用传统 LAN 模式；DST 通过虚拟 IP 连接。Overcooked! 2 仅观察 Steam / UDP，不替换 Steam 联机。自动安装扫描和 LAN 广播尚未验证。"),"muted"));g->addStretch();

 auto* r=pages[2];auto* roomCards=row(r);
 auto* create=card(roomCards,S("发起一场游戏"),S("使用所选游戏与连接策略创建房间。"));create->addWidget(text(S("还未选好游戏？在「我的游戏」中切换配置。"),"muted"));create->addStretch();auto* cr=row(create);action(S("创建房间"),cr,[this]{roomAction(true);},"primary","createRoomButton");cr->addStretch();
 auto* join=card(roomCards,S("加入伙伴的房间"),S("粘贴伙伴分享的完整房间码。"));roomCode=new QLineEdit;roomCode->setObjectName(S("roomCode"));roomCode->setPlaceholderText(S("房间 ID:邀请密钥"));join->addWidget(roomCode);auto* jr=row(join);action(S("加入房间"),jr,[this]{roomAction(false);},"secondary","joinRoomButton");jr->addStretch();
 auto* current=card(r,S("当前会话"));roomInfo=text(S("尚未加入房间 · 创建或输入房间码开始联机。"));roomInfo->setObjectName(S("roomStatus"));current->addWidget(roomInfo);
 auto* roomActions=row(current);roomJoinNetwork=action(S("连接虚拟网络"),roomActions,[this]{localJoin();},"primary","joinNetworkButton");action(S("复制邀请"),roomActions,[this]{if(roomId.isEmpty()){log(S("请先创建或加入房间"));return;}QApplication::clipboard()->setText(roomId+QLatin1Char(':')+invite);log(S("已复制邀请信息；请仅分享给可信玩家"));});roomActions->addStretch();action(S("离开房间"),roomActions,[this]{leaveRoom();},"secondary","leaveRoomButton");
 auto* members=card(r,S("房间成员"),S("DIRECT 直连 · RELAY 中继 · UNKNOWN 证据不足；状态以新鲜链路数据为准。"));
 peersTable=new QTableWidget(0,5);peersTable->setObjectName(S("peersTable"));peersTable->setHorizontalHeaderLabels({S("玩家 / Node ID"),S("虚拟 IP"),S("状态"),S("物理路径"),S("判定依据")});peersTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);peersTable->verticalHeader()->hide();peersTable->verticalHeader()->setDefaultSectionSize(60);peersTable->setShowGrid(false);peersTable->setAlternatingRowColors(true);peersTable->setEditTriggers(QAbstractItemView::NoEditTriggers);peersTable->setSelectionBehavior(QAbstractItemView::SelectRows);peersTable->setMinimumHeight(215);members->addWidget(peersTable);r->addStretch();

 auto* d=pages[DiagnosticsPage];auto* probe=card(d,S("链路探测"),S("双方先启用 UDP echo，再选择成员发送 10 次探测。"));auto* pf=form(probe);
 bindIP=new QLineEdit;bindIP->setPlaceholderText(S("本机已分配的虚拟 IPv4"));bindIP->setObjectName(S("bindIP"));pf->addRow(S("本机虚拟 IP"),bindIP);
 probePort=new QLineEdit(S("39755"));pf->addRow(S("UDP 端口"),probePort);target=new DropDown;target->setObjectName(S("probeTarget"));pf->addRow(S("目标成员"),target);
 auto* probeActions=row(probe);action(S("启用本机 Echo"),probeActions,[this]{bindEcho();});action(S("开始探测"),probeActions,[this]{startProbe();},"primary");probeActions->addStretch();
 probeInfo=text(S("尚未探测 · 防火墙规则需按诊断文档手动添加。"),"muted");probeInfo->setObjectName(S("probeInfo"));probe->addWidget(probeInfo);
 auto* logs=card(d,S("事件日志"),S("显示最近 1,000 条事件；凭证与邀请密钥不会记录。"));events=new QPlainTextEdit;events->setObjectName(S("events"));events->setReadOnly(true);events->setMaximumBlockCount(1000);events->setMinimumHeight(210);logs->addWidget(events);d->addStretch();

 auto* st=pages[SettingsPage];
 auto* about=card(st,S("关于 GameLink"),S("此版本号用于识别 GameLink 客户端；ZeroTier 版本在「ZeroTier 状态」页显示。"));
 auto* aboutVersion=text(S("GameLink 客户端 v")+QCoreApplication::applicationVersion(),"metric");aboutVersion->setObjectName(S("aboutClientVersion"));about->addWidget(aboutVersion);
 auto* server=card(st,S("房间服务"),S("配置 GameLink 控制平面；远程地址需要 HTTPS。"));auto* sf=form(server);
 serverUrl=new QLineEdit(settings.value(S("server"),S("http://127.0.0.1:8080")).toString());serverUrl->setObjectName(S("serverUrl"));sf->addRow(S("服务地址"),serverUrl);
 serverToken=new QLineEdit;serverToken->setEchoMode(QLineEdit::Password);serverToken->setObjectName(S("serverToken"));serverToken->setPlaceholderText(S("输入访问凭证，仅保留在内存中"));sf->addRow(S("访问凭证"),serverToken);
 auto* local=card(z,S("本地 ZeroTier"),S("使用独立安装的官方客户端与受保护的 Token 文件。"));auto* lf=form(local);
 ztTokenFile=new QLineEdit(settings.value(S("ztTokenFile"),gamelink::legacyGameLinkTokenPath()).toString());ztTokenFile->setObjectName(S("ztTokenFile"));lf->addRow(S("Token 文件"),ztTokenFile);
 ztPort=new QLineEdit(settings.value(S("ztPort"),S("9993")).toString());ztPort->setObjectName(S("ztPort"));lf->addRow(S("API 端口"),ztPort);
 auto* tokenActions=row(local);chooseCredentialButton=action(S("选择 Token 文件"),tokenActions,[this]{
  const auto path=QFileDialog::getOpenFileName(this,S("选择本机 ZeroTier authtoken.secret"),ztTokenFile->text(),S("凭据文件 (*.secret);;所有文件 (*)"));
  if(!path.isEmpty() && !installer.busy() && !networkMutationBusy && !credentialBusy){ztTokenFile->setText(path);save();pollLocal();}
 },"secondary","ztChooseCredentialsButton");tokenActions->addStretch();
 local->addWidget(text(S("自动检测兼容 Windows 的 ZeroTier\\One\\authtoken.secret 和旧版 ZeroTier\\authtoken.secret。若没有当前用户可读的凭据，请先打开官方 ZeroTier One 托盘程序，完成其权限提示后重新检测。仅保存路径，不复制或显示 Token。"),"muted"));
 auto* service=card(z,S("ZeroTier 服务管理"),S("通过官方 MSI 安装/修复或卸载整个 ZeroTier 产品（含服务与虚拟网卡）。操作时由 Windows 请求管理员授权。"));
 auto* serviceForm=form(service);
 installerPath=new QLineEdit;installerPath->setReadOnly(true);installerPath->setObjectName(S("ztMsiPath"));installerPath->setPlaceholderText(S("优先使用本地缓存的官方 MSI，也可手动选择更新版本"));serviceForm->addRow(S("官方安装包"),installerPath);
 installedProducts=new DropDown;installedProducts->setObjectName(S("ztInstalledProducts"));serviceForm->addRow(S("已安装版本"),installedProducts);
 connect(installedProducts,&QComboBox::currentIndexChanged,this,[this]{updateInstallerButtons();});
 installerStatus=text(S("等待 Windows Installer 产品检测"),"muted");installerStatus->setObjectName(S("ztInstallerStatus"));service->addWidget(installerStatus);
 auto* serviceFiles=row(service);chooseInstaller=action(S("选择 MSI"),serviceFiles,[this]{selectInstaller();},"secondary","ztChooseMsiButton");refreshProducts=action(S("刷新安装列表"),serviceFiles,[this]{refreshInstaller();refreshEnvironment();},"secondary","ztRefreshProductsButton");serviceFiles->addStretch();
 auto* serviceActions=row(service);installZeroTier=action(S("安装 / 修复服务"),serviceActions,[this]{installService();},"primary","ztInstallButton");uninstallZeroTier=action(S("卸载 ZeroTier"),serviceActions,[this]{uninstallService();},"secondary","ztUninstallButton");serviceActions->addStretch();
 service->addWidget(text(S("安装包需通过可信签名、ZeroTier 发布者和 MSI 产品校验。GameLink 不自动下载执行程序，也不在此处更改 planet / moon。"),"muted"));
 auto* localSettingsActions=row(z);action(S("保存本地配置并刷新"),localSettingsActions,[this]{save();refreshEnvironment();pollLocal();log(S("本地 ZeroTier 配置已保存，正在刷新"));},"secondary","ztSaveSettingsButton");localSettingsActions->addStretch();z->addStretch();
 auto* preferences=card(st,S("连接偏好"));auto* prefs=form(preferences);policy=new DropDown;policy->setObjectName(S("policySelector"));policy->addItems({S("优先直连 / Preferred Direct"),S("严格直连 / Direct Required · 暂不支持"),S("允许中继 / Relay Allowed")});prefs->addRow(S("新房间策略"),policy);
 preferences->addWidget(text(S("严格直连暂不支持，选中后会拒绝加入与启动。不会自动修改默认网关、DNS 或防火墙；自动启动尚未实现。"),"muted"));
 auto* settingsActions=row(st);action(S("保存并刷新"),settingsActions,[this]{save();pollLocal();log(S("设置已保存，正在刷新本地状态"));},"primary");action(S("导出事件日志"),settingsActions,[this]{const auto p=QFileDialog::getSaveFileName(this,S("导出日志"),S("gamelink-events.txt"));if(!p.isEmpty()){QFile f(p);if(f.open(QIODevice::WriteOnly))f.write(events->toPlainText().toUtf8());}});settingsActions->addStretch();st->addStretch();
}
