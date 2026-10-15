#include "transport.hpp"
#include <QFile>
#include <QJsonObject>
#include <QHostAddress>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#define S(x) QStringLiteral(x)
Transport::Transport(QObject* parent):QObject(parent),manager(this){manager.setProxy(QNetworkProxy::NoProxy);}
void Transport::local(int port,const QString& tokenFile,const QString& method,const QString& path,const QJsonObject& body,Callback done){
 QFile f(tokenFile);
 if(port<1 || port>65535 || !f.open(QIODevice::ReadOnly) || f.size()>512){done({},S("本地 Token 不可读；请使用受控凭证副本，GUI 无需管理员运行"));return;}
 const auto token=f.readAll().trimmed();
 if(token.isEmpty() || token.contains('\r') || token.contains('\n')){done({},S("本地凭证格式无效"));return;}
 send(QUrl(S("http://127.0.0.1:%1%2").arg(port).arg(path)),"X-ZT1-Auth",token,method,body,std::move(done));
}
void Transport::room(const QString& base,const QString& token,const QString& method,const QString& path,const QJsonObject& body,Callback done){
 QUrl url(base);QHostAddress ip(url.host());
 if(!url.isValid() || !url.userInfo().isEmpty() || !url.query().isEmpty() || !url.fragment().isEmpty() ||
    (!url.path().isEmpty() && url.path()!=S("/")) ||
    (url.scheme()!=S("https") && !(url.scheme()==S("http") && ip.isLoopback()))){done({},S("服务器须为 HTTPS origin；本机测试允许 loopback HTTP"));return;}
 if(token.size()<32 || token.size()>256 || token.contains(QChar('\r')) || token.contains(QChar('\n'))){done({},S("GameLink 凭证格式无效"));return;}
 url.setPath(path);
 send(url,"Authorization",QByteArray("Bearer ")+token.toUtf8(),method,body,std::move(done));
}
void Transport::send(const QUrl& url,const QByteArray& header,const QByteArray& token,const QString& method,const QJsonObject& body,Callback done){
 QNetworkRequest req(url);req.setRawHeader(header,token);req.setHeader(QNetworkRequest::ContentTypeHeader,S("application/json"));
 req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);req.setTransferTimeout(2000);
 auto* reply=method==S("GET") ? manager.get(req) : manager.sendCustomRequest(req,method.toUtf8(),QJsonDocument(body).toJson(QJsonDocument::Compact));
 connect(reply,&QNetworkReply::readyRead,reply,[reply]{if(reply->bytesAvailable()>1024*1024)reply->abort();});
 connect(reply,&QNetworkReply::finished,this,[reply,done=std::move(done)]{
  const auto status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  const auto bytes=reply->readAll();const auto err=reply->error();reply->deleteLater();
  if(err!=QNetworkReply::NoError || status<200 || status>=300){done({},S("请求失败 HTTP %1；连接/认证/房间状态需检查").arg(status));return;}
  if(status==204){done(QJsonDocument(QJsonObject{}),{});return;}
  QJsonParseError parse;const auto doc=QJsonDocument::fromJson(bytes,&parse);
  if(bytes.size()>1024*1024 || parse.error!=QJsonParseError::NoError){done({},S("JSON 响应无效或过大"));return;}
  done(doc,{});
 });
}
