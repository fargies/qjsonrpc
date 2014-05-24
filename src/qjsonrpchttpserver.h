#ifndef QJSONRPCHTTPSERVER_H
#define QJSONRPCHTTPSERVER_H

#include <QSslConfiguration>
#include <QTcpServer>
#include <QHash>

#include "qjsonrpcabstractserver.h"
#include "qjsonrpc_export.h"

class QJsonRpcHttpRequest;
class QJSONRPC_EXPORT QJsonRpcHttpServer : public QTcpServer,
                                           public QJsonRpcServiceProvider
{
    Q_OBJECT
public:
    QJsonRpcHttpServer(QObject *parent = 0);
    ~QJsonRpcHttpServer();

    QSslConfiguration sslConfiguration() const;
    void setSslConfiguration(const QSslConfiguration &config);

protected:
    virtual void incomingConnection(int socketDescriptor);

private Q_SLOTS:
    void processIncomingMessage(const QJsonRpcMessage &message);

private:
    QHash<QJsonRpcSocket *, QTcpSocket *> m_tcpSockets;
    QHash<QJsonRpcSocket *, QJsonRpcHttpRequest *> m_requests;
    QSslConfiguration m_sslConfiguration;

};

#endif
