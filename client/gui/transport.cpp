#include "transport.hpp"
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QHostAddress>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#define S(x) QStringLiteral(x)
Transport::Transport(QObject* parent):QObject(parent),manager(this){manager.setProxy(QNetworkProxy::NoProxy);}
void Transport::local(int port,const QString& tokenFile,const QString& method,const QString& path,const QJsonObject& body,Callback done){
 if(port<1 || port>65535){done({},S("[INVALID_PORT] 本地 API 端口必须为 1–65535；请检查「ZeroTier 状态」页"));return;}
 if(tokenFile.trimmed().isEmpty()){done({},S("[TOKEN_PATH_EMPTY] 未配置本地 Token 文件路径；请在「ZeroTier 状态」页指定受保护的凭证副本"));return;}
 QFile f(tokenFile);
 if(!QFileInfo(f).exists()){done({},S("[TOKEN_MISSING] 未找到本地 Token 文件，或当前用户无法访问其路径；请核对「ZeroTier 状态」页中的路径与权限"));return;}
 if(!QFileInfo(f).isFile()){done({},S("[TOKEN_INVALID] Token 路径不是普通文件"));return;}
 if(!f.open(QIODevice::ReadOnly)){done({},S("[TOKEN_ACCESS_DENIED] 本地 Token 不可读；请检查当前用户的文件读取权限，GUI 无需管理员运行"));return;}
 const auto bytes=f.read(513);
 if(f.error()!=QFileDevice::NoError){done({},S("[TOKEN_READ_FAILED] 本地 Token 文件读取失败；请检查文件状态和读取权限"));return;}
 const auto token=bytes.trimmed();
 bool printable=true;for(const char c:token)if(static_cast<unsigned char>(c)<0x21 || static_cast<unsigned char>(c)>0x7e)printable=false;
 if(bytes.size()>512 || token.isEmpty() || !printable){done({},S("[TOKEN_INVALID] 本地凭证为空、过大或包含无效字符；请核对凭证副本"));return;}
 send(QUrl(S("http://127.0.0.1:%1%2").arg(port).arg(path)),"X-ZT1-Auth",token,method,body,std::move(done));
}
void Transport::room(const QString& base,const QString& token,const QString& method,const QString& path,const QJsonObject& body,Callback done){
 QUrl url(base);QHostAddress ip(url.host());
 if(!url.isValid() || !url.userInfo().isEmpty() || !url.query().isEmpty() || !url.fragment().isEmpty() ||
    (!url.path().isEmpty() && url.path()!=S("/")) ||
    (url.scheme()!=S("https") && !(url.scheme()==S("http") && ip.isLoopback()))){done({},S("[ROOM_URL_INVALID] 请在「设置 → 房间服务」填写 GameLink Server 的 HTTPS 站点根地址；本机测试允许回环 IP 的 HTTP 地址"));return;}
 if(token.size()<32 || token.size()>256 || token.contains(QLatin1Char('\r')) || token.contains(QLatin1Char('\n'))){done({},S("[ROOM_TOKEN_INVALID] GameLink 房间访问凭证格式无效，请在「设置 → 房间服务」重新填写个人凭证（重启后需重新填写）"));return;}
 url.setPath(path);
 send(url,"Authorization",QByteArray("Bearer ")+token.toUtf8(),method,body,std::move(done));
}
void Transport::send(const QUrl& url,const QByteArray& header,const QByteArray& token,const QString& method,const QJsonObject& body,Callback done){
 QNetworkRequest req(url);req.setRawHeader(header,token);req.setHeader(QNetworkRequest::ContentTypeHeader,S("application/json"));
 req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
 auto* reply=method==S("GET") ? manager.get(req) : manager.sendCustomRequest(req,method.toUtf8(),QJsonDocument(body).toJson(QJsonDocument::Compact));
 auto* deadline=new QTimer(reply);deadline->setSingleShot(true);
 connect(deadline,&QTimer::timeout,reply,[reply]{reply->setProperty("totalTimeout",true);reply->abort();});deadline->start(2000);
 connect(reply,&QNetworkReply::readyRead,reply,[reply]{if(reply->bytesAvailable()>1024*1024){reply->setProperty("responseTooLarge",true);reply->abort();}});
 connect(reply,&QNetworkReply::finished,this,[reply,done=std::move(done)]{
  const auto status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  const auto bytes=reply->isOpen()?reply->readAll():QByteArray{};const auto err=reply->error();
  const bool timeout=reply->property("totalTimeout").toBool() || err==QNetworkReply::TimeoutError;
  const bool tooLarge=reply->property("responseTooLarge").toBool() || bytes.size()>1024*1024;
  reply->deleteLater();
  // Never expose reply->errorString(), upstream response bodies, credentials or URLs.
  if(tooLarge){done({},S("[RESPONSE_TOO_LARGE] API 响应超过 1 MiB，已终止读取"));return;}
  if(timeout){done({},S("[REQUEST_TIMEOUT] 请求超时（2 秒）；请检查服务、API 端口和本机访问策略"));return;}
  if(status==401 || status==403){done({},S("[AUTH_FAILED] HTTP %1：认证或访问被拒绝；请检查对应的 Token 和访问权限").arg(status));return;}
  if(status==404){done({},S("[API_NOT_FOUND] HTTP 404：接口不存在；请检查 API 端口和版本兼容性"));return;}
  if(status>=300 && status<400){done({},S("[REDIRECT_BLOCKED] HTTP %1：已拒绝重定向，不会转发凭证").arg(status));return;}
  if(err==QNetworkReply::ConnectionRefusedError){done({},S("[CONNECTION_REFUSED] 连接被拒绝；可能是服务未运行或 API 端口不匹配，请结合服务检测结果核对"));return;}
  if(err==QNetworkReply::SslHandshakeFailedError){done({},S("[TLS_ERROR] TLS 握手失败；请检查服务器证书和 TLS 运行依赖"));return;}
  if(err!=QNetworkReply::NoError || status<200 || status>=300){done({},S("[NETWORK_ERROR] 请求失败 HTTP %1，网络错误码 %2；请检查服务与连接").arg(status).arg(static_cast<int>(err)));return;}
  if(status==204){done(QJsonDocument(QJsonObject{}),{});return;}
  QJsonParseError parse;const auto doc=QJsonDocument::fromJson(bytes,&parse);
  if(parse.error!=QJsonParseError::NoError){done({},S("[INVALID_JSON] API 返回无效 JSON；请检查是否使用了正确端口和兼容版本"));return;}
  done(doc,{});
 });
}
