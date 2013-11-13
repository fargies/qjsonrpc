
#include <QTcpServer>

#include "qjsonrpcsocket.h"
#include "qjsonrpcabstractserver_p.h"

class QJsonRpcTcpServerPrivate : public QJsonRpcAbstractServerPrivate
{
public:
    QJsonRpcTcpServerPrivate() : server(0) {}
    QTcpServer *server;
    QHash<QTcpSocket*, QJsonRpcSocket*> socketLookup;
};

