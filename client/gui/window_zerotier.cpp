#include "window.hpp"
#include <QHeaderView>
#include <QFileInfo>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QTableWidget>
#define S(x) QStringLiteral(x)
namespace {
bool validNetwork(const QString& id){return QRegularExpression(S("^[0-9a-f]{16}$")).match(id).hasMatch();}
QString idOf(const QJsonObject& n){return n[S("id")].toString(n[S("nwid")].toString());}
QString addresses(const QJsonObject& n){QStringList out;for(const auto& v:n[S("assignedAddresses")].toArray())out<<v.toString();return out.join(S(", "));}
}
bool Window::localSampleFresh()const{
 return !credentialBusy && sample.serviceAvailable && sampleAge.isValid() && sampleAge.elapsed()<=6000 &&
        sampledPort==ztPort->text().toInt() && sampledTokenFile==ztTokenFile->text();
}
QString Window::networkState(const QJsonObject& n,bool fresh)const{
 if(!fresh)return S("状态过期，请刷新");
 const auto status=n[S("status")].toString();
 if(status==S("ACCESS_DENIED"))return S("等待授权");
 if(status==S("REQUESTING_CONFIGURATION"))return S("等待配置");
 if(status==S("NOT_FOUND"))return S("网络不存在");
 if(status==S("PORT_ERROR"))return S("虚拟网卡异常");
 if(status==S("CLIENT_TOO_OLD"))return S("需要升级 ZeroTier");
 if(status==S("AUTHENTICATION_REQUIRED"))return S("需要额外身份验证");
 if(status!=S("OK"))return S("未知入网状态");
 if(!sample.online)return S("已加入 · 节点离线");
 if(addresses(n).isEmpty())return S("等待分配 IP");
 if(n[S("allowDefault")].toBool() || n[S("allowGlobal")].toBool() || n[S("allowDNS")].toBool())return S("已加入 · 请审核路由 / DNS");
 return S("已连接");
}
void Window::renderZeroTier(){
 const bool fresh=localSampleFresh();
 const auto targetId=roomId.isEmpty()?manualNetwork->text().trimmed().toLower():network;
 zeroTierNodeStatus->setText(fresh?S("Node %1 · %2 · ZeroTier %3").arg(node,sample.online?(sample.tcpFallback?S("在线 · TCP 回退"):S("在线")):S("离线"),apiVersion):S("本地 API 未就绪或状态已过期，请检查下方凭证与服务状态。"));
 QString credentialMessage;
 if(credentialBusy)credentialMessage=S("正在检测本机凭据并验证本地 API…");
 else if(!fresh && (localApiMessage.contains(S("[TOKEN_")) || localApiMessage.contains(S("[AUTH_FAILED]"))))
  credentialMessage=S("GameLink 尚未取得有效的本机 API 凭据，无法读取网络状态；这不代表 ZeroTier 已离线。请点击「检测本机凭据」，或在下方选择 Token 文件。若仍失败，先打开官方 ZeroTier One 完成权限提示再检测。");
 credentialStatus->setText(credentialMessage);credentialStatus->setVisible(!credentialMessage.isEmpty());
 zeroTierNetworkId->setText(targetId.isEmpty()?S("输入 Network ID 或先加入 GameLink 房间"):S("%1 · Network ID %2").arg(roomId.isEmpty()?S("手动网络"):S("房间 ")+roomId,targetId));
 QString targetState=fresh?S("未加入"):S("状态未知，请刷新");
 for(const auto& v:networks)if(idOf(v.toObject())==targetId){targetState=networkState(v.toObject(),fresh);break;}
 if(targetId.isEmpty())targetState=S("尚未选择网络");
 if(networkMutationBusy)targetState=S("网络操作进行中");
 zeroTierNetworkStatus->setText(targetState);
 const auto fingerprint=networks.isEmpty()?QByteArray("[]"):QJsonDocument(networks).toJson(QJsonDocument::Compact);
 const auto tableKey=fingerprint+QByteArray::number(fresh)+QByteArray::number(sample.online);
 if(networksTable->property("snapshot").toByteArray()!=tableKey){
  const int selectedRow=networksTable->currentRow();
  const auto selected=selectedRow>=0 && networksTable->item(selectedRow,0)?networksTable->item(selectedRow,0)->text():QString{};
  QSignalBlocker blocker(networksTable);networksTable->setRowCount(0);
  for(const auto& v:networks){const auto n=v.toObject();const auto id=idOf(n);if(!validNetwork(id))continue;
   const int row=networksTable->rowCount();networksTable->insertRow(row);
   const QStringList cells={id,n[S("name")].toString(),networkState(n,fresh),fresh?addresses(n):S("状态过期")};
   for(int col=0;col<cells.size();++col)networksTable->setItem(row,col,new QTableWidgetItem(cells[col]));
   if(id==selected)networksTable->selectRow(row);
  }
  networksTable->setProperty("snapshot",tableKey);
 }
 const bool available=!installer.busy() && !networkMutationBusy && !mutationBusy && !credentialBusy;
 const int row=networksTable->currentRow();
 leaveZeroTier->setEnabled(available && fresh && row>=0 && networksTable->item(row,0));
 joinZeroTier->setEnabled(available && fresh && validNetwork(targetId));
 roomJoinNetwork->setEnabled(available && fresh && validNetwork(targetId));
 manualNetwork->setEnabled(available && roomId.isEmpty());
 const bool credentialsEnabled=!installer.busy() && !networkMutationBusy && !credentialBusy;
 ztPort->setEnabled(credentialsEnabled);ztTokenFile->setEnabled(credentialsEnabled);
 detectCredentialButton->setEnabled(credentialsEnabled);chooseCredentialButton->setEnabled(credentialsEnabled);
 useDetectedPort->setEnabled(credentialsEnabled && environment.detectedPort>0 && environment.detectedPort!=ztPort->text().toInt());
 updateInstallerButtons();
}
void Window::detectCredentials(){
 if(installer.busy() || networkMutationBusy || credentialBusy)return;
 QStringList candidates{ztTokenFile->text()};candidates.append(credentialLocator());
 QStringList paths;
 for(const auto& candidate:candidates){
  if(candidate.isEmpty())continue;
  const auto path=QDir::fromNativeSeparators(candidate);
  if(QFileInfo(path).isAbsolute() && !paths.contains(QDir::cleanPath(path)))paths<<QDir::cleanPath(path);
 }
 credentialBusy=true;++serviceGeneration;
 localFailure(S("凭据检测"),S("正在验证本机 API"));
 tryCredential(paths,ztPort->text().toInt(),ztTokenFile->text(),serviceGeneration);
}
void Window::tryCredential(QStringList paths,int port,QString original,quint64 generation,QString lastError){
 if(generation!=serviceGeneration || installer.busy() || port!=ztPort->text().toInt() || original!=ztTokenFile->text()){
  credentialBusy=false;render();return;
 }
 // Only read accessible existing files. Never copy service credentials or request elevation.
 while(!paths.isEmpty() && (!QFileInfo(paths.first()).isFile() || !QFileInfo(paths.first()).isReadable()))paths.removeFirst();
 if(paths.isEmpty()){
  credentialBusy=false;
  localFailure(S("凭据检测"),lastError.isEmpty()?S("[TOKEN_MISSING] 未找到当前用户可读的本机凭据；请打开官方 ZeroTier One 完成权限提示，或在下方选择已有 Token 文件。"):lastError);
  return;
 }
 const auto candidate=paths.takeFirst();
 transport.local(port,candidate,S("GET"),S("/status"),{},[this,paths,port,original,generation,candidate](QJsonDocument doc,QString error){
  if(generation!=serviceGeneration || installer.busy() || port!=ztPort->text().toInt() || original!=ztTokenFile->text()){
   credentialBusy=false;render();return;
  }
  if(error.isEmpty()){
   const auto info=doc.object();
   if(!doc.isObject() || !QRegularExpression(S("^[0-9a-f]{10}$")).match(info[S("address")].toString()).hasMatch() || !info[S("online")].isBool() || !info[S("clock")].isDouble())error=S("[API_SCHEMA_UNSUPPORTED] 本机 API 状态格式不兼容，请核对端口与版本");
  }
  if(!error.isEmpty()){
   // A stale per-user copy must not hide another valid official credential.
   if(error.contains(S("[AUTH_FAILED]")) || error.contains(S("[TOKEN_"))){tryCredential(paths,port,original,generation,error);return;}
   credentialBusy=false;localFailure(S("凭据检测"),error);return;
  }
  credentialBusy=false;ztTokenFile->setText(candidate);save();
  log(S("已检测并验证本机 ZeroTier 凭据，正在读取网络状态。"));render();pollLocal();
 });
}
void Window::localJoin(){
 if(installer.busy() || networkMutationBusy || mutationBusy){log(S("ZeroTier 或房间操作进行中，请等待结束。"));return;}
 if(selectedPolicy()==gamelink::Policy::DirectRequired){log(S("拒绝严格直连 join：unsupported"));return;}
 const auto n=roomId.isEmpty()?manualNetwork->text().trimmed().toLower():network;
 if(!validNetwork(n)){log(S("网络 ID 无效：请输入 16 位十六进制 Network ID，不是房间码。"));return;}
 if(!localSampleFresh()){log(S("请先刷新本地 ZeroTier 状态，确认 API 凭证与端口可用。"));return;}
 if(!roomId.isEmpty()){
  bool owns=false;for(const auto& v:roomPeers)if(v.toObject()[S("nodeId")].toString()==node && v.toObject()[S("authorized")].toBool())owns=true;
  if(!owns){log(S("本机 Node ID 与服务器身份绑定不符或成员数据未就绪"));return;}
 }
 for(const auto& v:networks)if(idOf(v.toObject())==n){
  network=n;joiningAge.restart();networkOperationStatus->setText(S("已存在此网络，保留原配置；入网状态：")+networkState(v.toObject(),true));log(networkOperationStatus->text());render();return;
 }
 const auto port=ztPort->text().toInt();const auto token=ztTokenFile->text();const auto expectedRoom=roomId;
 const auto generation=++serviceGeneration;
 networkMutationBusy=true;sample={};sampleAge.invalidate();probeHealthy=false;stopProbe();udp.close();
 networkOperationStatus->setText(S("正在请求加入网络 ")+n);render();
 transport.local(port,token,S("POST"),S("/network/")+n,
  {{S("allowManaged"),true},{S("allowDefault"),false},{S("allowGlobal"),false},{S("allowDNS"),false}},
  [this,n,port,token,expectedRoom,generation](QJsonDocument,QString err){
   networkMutationBusy=false;
   const bool same=generation==serviceGeneration && port==ztPort->text().toInt() && token==ztTokenFile->text() && expectedRoom==roomId;
   if(!same)networkOperationStatus->setText(S("入网请求已结束，但会话或本地配置已变化；请刷新确认实际网络状态。"));
   else if(!err.isEmpty())networkOperationStatus->setText(S("加入网络失败：")+err);
   else{network=n;joiningAge.restart();save();networkOperationStatus->setText(S("已请求加入 ")+n+S("；等待控制器授权、配置与 IP，详见入网状态。"));}
   log(networkOperationStatus->text());render();pollLocal();
  });
}
void Window::localLeave(){
 if(installer.busy() || networkMutationBusy || mutationBusy || !localSampleFresh())return;
 const int row=networksTable->currentRow();if(row<0 || !networksTable->item(row,0))return;
 const auto id=networksTable->item(row,0)->text();if(!validNetwork(id))return;
 const auto port=ztPort->text().toInt();const auto token=ztTokenFile->text();const auto generation=serviceGeneration;const auto expectedNode=node;
 const auto message=S("离开本机 ZeroTier 网络 %1？\n\n该网络上的连接将中断。GameLink 房间和控制器授权保留，之后可重新加入；其他网络不受影响。").arg(id);
 if(QMessageBox::question(this,S("离开 ZeroTier 网络"),message,QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)return;
 if(installer.busy() || networkMutationBusy || mutationBusy || !localSampleFresh() || generation!=serviceGeneration || port!=ztPort->text().toInt() || token!=ztTokenFile->text() || node!=expectedNode){log(S("确认期间本地状态发生变化，请刷新后重新选择退网。"));return;}
 bool exists=false;for(const auto& v:networks)if(idOf(v.toObject())==id)exists=true;
 if(!exists){log(S("所选网络已不在本机网络列表中，请刷新。"));return;}
 const auto operationGeneration=++serviceGeneration;networkMutationBusy=true;sample={};sampleAge.invalidate();probeHealthy=false;stopProbe();udp.close();
 networkOperationStatus->setText(S("正在请求离开网络 ")+id);render();
 transport.local(port,token,S("DELETE"),S("/network/")+id,{},[this,id,port,token,operationGeneration](QJsonDocument,QString err){
  networkMutationBusy=false;
  if(operationGeneration!=serviceGeneration || port!=ztPort->text().toInt() || token!=ztTokenFile->text())networkOperationStatus->setText(S("退网请求已结束，但本地配置已变化；请刷新确认实际网络状态。"));
  else networkOperationStatus->setText(err.isEmpty()?S("已请求离开本机网络 ")+id+S("；正在刷新确认。房间和控制器授权保留。"):S("离开网络失败：")+err);
  log(networkOperationStatus->text());render();pollLocal();
 });
}
