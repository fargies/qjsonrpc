
#include <QTcpServer>

#include "qjsonrpcsocket.h"
#include "qjsonrpcabstractserver_p.h"

class QJsonRpcTcpServerPrivate : public QJsonRpcAbstractServerPrivate
{
    Q_DECLARE_PUBLIC(QJsonRpcTcpServer)
public:
    QJsonRpcTcpServerPrivate()
        : server(0)
    {
    }

    virtual void _q_processIncomingConnection();
    virtual void _q_clientDisconnected();

    QTcpServer *server;
    QHash<QTcpSocket*, QJsonRpcSocket*> socketLookup;
};

