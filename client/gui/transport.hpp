#pragma once
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QObject>
#include <functional>
class Transport : public QObject {
public:
 using Callback=std::function<void(QJsonDocument,QString)>;
 explicit Transport(QObject* parent=nullptr);
 void local(int port,const QString& tokenFile,const QString& method,const QString& path,const QJsonObject& body,Callback done);
 void room(const QString& base,const QString& token,const QString& method,const QString& path,const QJsonObject& body,Callback done);
private:
 QNetworkAccessManager manager;
 void send(const QUrl&,const QByteArray&,const QByteArray&,const QString&,const QJsonObject&,Callback);
};
