#include <QTcpSocket>

#include "qjsonrpctcpserver.h"
#include "qjsonrpctcpserver_p.h"

QJsonRpcTcpServer::QJsonRpcTcpServer(QObject *parent)
    : QJsonRpcAbstractServer(*new QJsonRpcTcpServerPrivate, parent)
{
}

QJsonRpcTcpServer::QJsonRpcTcpServer(QJsonRpcTcpServerPrivate &dd, QObject *parent)
    : QJsonRpcAbstractServer(dd, parent)
{
}

QJsonRpcTcpServer::~QJsonRpcTcpServer()
{
    Q_D(QJsonRpcTcpServer);
    foreach (QTcpSocket *socket, d->socketLookup.keys())
        socket->deleteLater();
    d->socketLookup.clear();
}

bool QJsonRpcTcpServer::listen(const QHostAddress &address, quint16 port)
{
    Q_D(QJsonRpcTcpServer);
    if (!d->server) {
        d->server = new QTcpServer(this);
        connect(d->server, SIGNAL(newConnection()), this, SLOT(_q_processIncomingConnection()));
    }

    return d->server->listen(address, port);
}

void QJsonRpcTcpServerPrivate::_q_processIncomingConnection()
{
    Q_Q(QJsonRpcTcpServer);
    QTcpSocket *tcpSocket = server->nextPendingConnection();
    if (!tcpSocket) {
        qDebug() << Q_FUNC_INFO << "nextPendingConnection is null";
        return;
    }

    QIODevice *device = qobject_cast<QIODevice*>(tcpSocket);
    QJsonRpcSocket *socket = new QJsonRpcSocket(device, q);
#if QT_VERSION >= 0x050100 || QT_VERSION <= 0x050000
    socket->setWireFormat(format);
#endif

    QObject::connect(socket, SIGNAL(messageReceived(QJsonRpcMessage)),
                          q, SLOT(_q_processMessage(QJsonRpcMessage)));
    clients.append(socket);
    QObject::connect(tcpSocket, SIGNAL(disconnected()), q, SLOT(_q_clientDisconnected()));
    socketLookup.insert(tcpSocket, socket);
}

void QJsonRpcTcpServerPrivate::_q_clientDisconnected()
{
    Q_Q(QJsonRpcTcpServer);
    QTcpSocket *tcpSocket = static_cast<QTcpSocket*>(q->sender());
    if (tcpSocket) {
        if (socketLookup.contains(tcpSocket)) {
            QJsonRpcSocket *socket = socketLookup.take(tcpSocket);
            clients.removeAll(socket);
            socket->deleteLater();
        }

        tcpSocket->deleteLater();
    }
}

QString QJsonRpcTcpServer::errorString() const
{
    Q_D(const QJsonRpcTcpServer);
    return d->server->errorString();
}

#include "moc_qjsonrpctcpserver.cpp"
