#ifndef QJSONRPCSERVICE_P_H
#define QJSONRPCSERVICE_P_H

#include <QHash>
#include <QHostAddress>
#include <QPointer>
#include <QLocalSocket>
#include <QTcpSocket>

#include "qjsondocument.h"
#include "qjsonrpcservice.h"

class QJsonRpcSocketPrivate
{
public:
    QJsonRpcSocketPrivate() : format(QJsonRpcSocket::Plain) {}
    int QJsonRpcSocket::findJsonDocumentEnd(const QByteArray &jsonData);
    int QJsonRpcSocket::findJsonDocumentStart(const QByteArray &jsonData, int startIndex);
    void writeData(const QJsonRpcMessage &message);

    QPointer<QIODevice> device;
    QByteArray buffer;
    QHash<int, QJsonRpcServiceReply*> replies;
    QJsonRpcSocket::WireFormat format;

};

class QJsonRpcServerPrivate
{
public:
    QJsonRpcServerPrivate() : format(QJsonRpcSocket::Plain) {}
    QList<QJsonRpcSocket*> clients;
    QJsonRpcSocket::WireFormat format;

};

class QLocalServer;
class QJsonRpcLocalServerPrivate : public QJsonRpcServerPrivate
{
public:
    QJsonRpcLocalServerPrivate() : server(0) {}
    QLocalServer *server;
    QHash<QLocalSocket*, QJsonRpcSocket*> socketLookup;
};

class QTcpServer;
class QJsonRpcTcpServerPrivate : public QJsonRpcServerPrivate
{
public:
    QJsonRpcTcpServerPrivate() : server(0) {}
    QTcpServer *server;
    QHash<QTcpSocket*, QJsonRpcSocket*> socketLookup;
};

#endif
