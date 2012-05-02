#include <QLocalSocket>
#include <QLocalServer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QVarLengthArray>
#include <QStringList>
#include <QMetaMethod>
#include <QEventLoop>
#include <QTimer>

#include "json/qjsondocument.h"
#include "qjsonrpcservice_p.h"
#include "qjsonrpcservice.h"

QJsonRpcService::QJsonRpcService(QObject *parent)
    : QObject(parent)
{
}

QJsonRpcSocket *QJsonRpcService::senderSocket()
{
    if (m_socket)
        return m_socket.data();
    return 0;
}

void QJsonRpcService::cacheInvokableInfo()
{
    const QMetaObject *obj = metaObject();
    int startIdx = QObject::staticMetaObject.methodCount(); // skip QObject slots
    for (int idx = startIdx; idx < obj->methodCount(); ++idx) {
        const QMetaMethod method = obj->method(idx);
        if (method.methodType() == QMetaMethod::Slot &&
            method.access() == QMetaMethod::Public) {
            QByteArray signature = method.signature();
            QByteArray methodName = signature.left(signature.indexOf('('));
            m_invokableMethodHash.insert(methodName, idx);

            QList<int> parameterTypes;
            parameterTypes << QMetaType::type(method.typeName());

            foreach(QByteArray parameterType, method.parameterTypes()) {
                parameterTypes << QMetaType::type(parameterType);
            }
            m_parameterTypeHash[idx] = parameterTypes;
        }
    }
}

QJsonRpcMessage QJsonRpcService::dispatch(const QJsonRpcMessage &request) const
{
    if (!request.type() == QJsonRpcMessage::Request) {
        QJsonRpcMessage error =
                request.createErrorResponse(QJsonRpc::InvalidRequest, "invalid request");
        return error;
    }

    QByteArray method = request.method().section(".", -1).toLatin1();
    QVariantList arguments = request.params();
    if (!m_invokableMethodHash.contains(method)) {
        QJsonRpcMessage error =
            request.createErrorResponse(QJsonRpc::MethodNotFound, "invalid method called");
        return error;
    }

    int idx = -1;
    QList<int> parameterTypes;
    QList<int> indexes = m_invokableMethodHash.values(method);
    foreach (int methodIndex, indexes) {
        parameterTypes = m_parameterTypeHash.value(methodIndex);
        if (arguments.size() == parameterTypes.size() - 1) {
            idx = methodIndex;
            break;
        }
    }

    if (idx == -1) {
        QJsonRpcMessage error =
            request.createErrorResponse(QJsonRpc::InvalidParams, "invalid parameters");
        return error;
    }

    QVarLengthArray<void *, 10> parameters;
    parameters.reserve(parameterTypes.count());

    // first argument to metacall is the return value
    QMetaType::Type returnType = static_cast<QMetaType::Type>(parameterTypes[0]);
    void *returnData = QMetaType::construct(returnType);
    QVariant returnValue(returnType, returnData);
    if (returnType == QMetaType::QVariant)
        parameters.append(&returnValue);
    else
        parameters.append(returnValue.data());

    // compile arguments
    for (int i = 0; i < parameterTypes.size() - 1; ++i) {
        int parameterType = parameterTypes[i + 1];
        const QVariant &argument = arguments.at(i);
        if (argument.userType() != parameterType &&
            parameterType != QMetaType::QVariant)
            const_cast<QVariant*>(&argument)->convert(static_cast<QVariant::Type>(parameterType));
        parameters.append(const_cast<void *>(argument.constData()));
    }

    bool success =
        const_cast<QJsonRpcService*>(this)->qt_metacall(QMetaObject::InvokeMetaMethod, idx, parameters.data()) < 0;
    if (!success) {
        QString message = QString("dispatch for method '%1' failed").arg(method.constData());
        QJsonRpcMessage error =
            request.createErrorResponse(QJsonRpc::InvalidRequest, message);
        return error;
    }

    QVariant returnCopy(returnValue);
    QMetaType::destroy(returnType, returnData);
    return request.createResponse(returnCopy);
}


QJsonRpcServiceReply::QJsonRpcServiceReply(QObject *parent)
    : QObject(parent)
{
}

QJsonRpcMessage QJsonRpcServiceReply::response() const
{
    return m_response;
}














QJsonRpcServiceProvider::QJsonRpcServiceProvider()
{
}

QJsonRpcServiceProvider::~QJsonRpcServiceProvider()
{
    foreach (QJsonRpcService *service, m_services)
        service->deleteLater();
}

void QJsonRpcServiceProvider::addService(QJsonRpcService *service)
{
    const QMetaObject *mo = service->metaObject();
    for (int i = 0; i < mo->classInfoCount(); i++) {
        const QMetaClassInfo mci = mo->classInfo(i);
        if (mci.name() == QLatin1String("serviceName")) {
            service->cacheInvokableInfo();
            m_services.insert(mci.value(), service);
            return;
        }
    }

    qDebug() << Q_FUNC_INFO << "service added without serviceName classinfo, aborting";
}

void QJsonRpcServiceProvider::processMessage(QJsonRpcSocket *socket, const QJsonRpcMessage &message)
{
    switch (message.type()) {
        case QJsonRpcMessage::Request:
        case QJsonRpcMessage::Notification: {
            QString serviceName = message.method().section(".", 0, -2);
            if (serviceName.isEmpty() || !m_services.contains(serviceName)) {
                if (message.type() == QJsonRpcMessage::Request) {
                    QJsonRpcMessage error = message.createErrorResponse(QJsonRpc::MethodNotFound,
                                                                        QString("service '%1' not found").arg(serviceName));
                    socket->notify(error);
                }
            } else {
                QJsonRpcService *service = m_services.value(serviceName);
                service->m_socket = socket;
                QJsonRpcMessage response = service->dispatch(message);
                if (message.type() == QJsonRpcMessage::Request)
                    socket->notify(response);
            }
        }
        break;

        case QJsonRpcMessage::Response:
            // we don't handle responses in the provider
            break;

        default: {
            QJsonRpcMessage error = message.createErrorResponse(QJsonRpc::InvalidRequest,
                                                                QString("invalid request"));
            socket->notify(error);
            break;
        }
    };
}
























QJsonRpcSocket::QJsonRpcSocket(QIODevice *device, QObject *parent)
    : QObject(parent),
      d_ptr(new QJsonRpcSocketPrivate)
{
    Q_D(QJsonRpcSocket);
    connect(device, SIGNAL(readyRead()), this, SLOT(processIncomingData()));
    d->device = device;
}

QJsonRpcSocket::~QJsonRpcSocket()
{
}

bool QJsonRpcSocket::isValid() const
{
    Q_D(const QJsonRpcSocket);
    return d->device && d->device.data()->isOpen();
}

/*
void QJsonRpcSocket::sendMessage(const QList<QJsonRpcMessage> &messages)
{
    QJsonArray array;
    foreach (QJsonRpcMessage message, messages) {
        array.append(message.toObject());
    }

    QJsonDocument doc = QJsonDocument(array);
    m_device.data()->write(doc.toBinaryData());
}
*/

QJsonRpcMessage QJsonRpcSocket::sendMessageBlocking(const QJsonRpcMessage &message, int msecs)
{
    QJsonRpcServiceReply *reply = sendMessage(message);
    QScopedPointer<QJsonRpcServiceReply> replyPtr(reply);

    QEventLoop responseLoop;
    connect(reply, SIGNAL(finished()), &responseLoop, SLOT(quit()));
    QTimer::singleShot(msecs, &responseLoop, SLOT(quit()));
    responseLoop.exec();

    if (!reply->response().isValid())
        return message.createErrorResponse(QJsonRpc::TimeoutError, "request timed out");
    return reply->response();
}

QJsonRpcServiceReply *QJsonRpcSocket::sendMessage(const QJsonRpcMessage &message)
{
    Q_D(QJsonRpcSocket);
    if (!d->device) {
        qDebug() << Q_FUNC_INFO << "trying to send message with no device, aborting...";
        return 0;
    }

    QJsonDocument doc = QJsonDocument(message.toObject());
    if (d->format == QJsonRpcSocket::Binary)
        d->device.data()->write(doc.toBinaryData());
    else
        d->device.data()->write(doc.toJson());

    QJsonRpcServiceReply *reply = new QJsonRpcServiceReply;
    d->replies.insert(message.id(), reply);
    return reply;
}

void QJsonRpcSocket::notify(const QJsonRpcMessage &message)
{
    Q_D(QJsonRpcSocket);
    QJsonDocument doc = QJsonDocument(message.toObject());
    if (d->format == QJsonRpcSocket::Binary)
        d->device.data()->write(doc.toBinaryData());
    else
        d->device.data()->write(doc.toJson());
}

QJsonRpcMessage QJsonRpcSocket::invokeRemoteMethodBlocking(const QString &method, const QVariant &param1,
                                                           const QVariant &param2, const QVariant &param3,
                                                           const QVariant &param4, const QVariant &param5,
                                                           const QVariant &param6, const QVariant &param7,
                                                           const QVariant &param8, const QVariant &param9,
                                                           const QVariant &param10)
{
    QVariantList params;
    if (param1.isValid()) params.append(param1);
    if (param2.isValid()) params.append(param2);
    if (param3.isValid()) params.append(param3);
    if (param4.isValid()) params.append(param4);
    if (param5.isValid()) params.append(param5);
    if (param6.isValid()) params.append(param6);
    if (param7.isValid()) params.append(param7);
    if (param8.isValid()) params.append(param8);
    if (param9.isValid()) params.append(param9);
    if (param10.isValid()) params.append(param10);

    QJsonRpcMessage request = QJsonRpcMessage::createRequest(method, params);
    return sendMessageBlocking(request);
}

QJsonRpcServiceReply *QJsonRpcSocket::invokeRemoteMethod(const QString &method, const QVariant &param1,
                                                         const QVariant &param2, const QVariant &param3,
                                                         const QVariant &param4, const QVariant &param5,
                                                         const QVariant &param6, const QVariant &param7,
                                                         const QVariant &param8, const QVariant &param9,
                                                         const QVariant &param10)
{
    QVariantList params;
    if (param1.isValid()) params.append(param1);
    if (param2.isValid()) params.append(param2);
    if (param3.isValid()) params.append(param3);
    if (param4.isValid()) params.append(param4);
    if (param5.isValid()) params.append(param5);
    if (param6.isValid()) params.append(param6);
    if (param7.isValid()) params.append(param7);
    if (param8.isValid()) params.append(param8);
    if (param9.isValid()) params.append(param9);
    if (param10.isValid()) params.append(param10);

    QJsonRpcMessage request = QJsonRpcMessage::createRequest(method, params);
    return sendMessage(request);
}

QJsonRpcSocket::WireFormat QJsonRpcSocket::wireFormat() const
{
    Q_D(const QJsonRpcSocket);
    return d->format;
}

void QJsonRpcSocket::setWireFormat(WireFormat format)
{
    Q_D(QJsonRpcSocket);
    d->format = format;
}


void QJsonRpcSocket::processIncomingData()
{
    Q_D(QJsonRpcSocket);
    if (!d->device) {
        qDebug() << Q_FUNC_INFO << "called without device";
        return;
    }

    d->buffer.append(d->device.data()->readAll());
    while (!d->buffer.isEmpty()) {
        QJsonDocument document;
        if (d->format == QJsonRpcSocket::Binary)
            document = QJsonDocument::fromBinaryData(d->buffer);
        else
            document = QJsonDocument::fromJson(d->buffer);
        if (document.isEmpty())
            break;

        if (d->format == QJsonRpcSocket::Binary)
            d->buffer = d->buffer.mid(document.toBinaryData().size());
        else
            d->buffer = d->buffer.mid(document.toJson().size());

        if (document.isArray()) {
            qDebug() << "bulk support is current disabled";
            /*
            for (int i = 0; i < document.array().size(); ++i) {
                QJsonObject messageObject = document.array().at(i).toObject();
                if (!messageObject.isEmpty()) {
                    QJsonRpcMessage message(messageObject);
                    Q_EMIT messageReceived(message);
                }
            }
            */
        } else if (document.isObject()){
            QJsonRpcMessage message(document.object());
            Q_EMIT messageReceived(message);

            if (message.type() == QJsonRpcMessage::Response ||
                message.type() == QJsonRpcMessage::Error) {
                if (d->replies.contains(message.id())) {
                    QJsonRpcServiceReply *reply = d->replies.take(message.id());
                    reply->m_response = message;
                    reply->finished();
                }
            } else {
                processRequestMessage(message);
            }
        }
    }
}

void QJsonRpcSocket::processRequestMessage(const QJsonRpcMessage &message)
{
    Q_UNUSED(message)
    // we don't do anything the default case with requests and notifications,
    // these are only handled by the provider
}

QJsonRpcServiceSocket::QJsonRpcServiceSocket(QIODevice *device, QObject *parent)
    : QJsonRpcSocket(device, parent)
{
}

QJsonRpcServiceSocket::~QJsonRpcServiceSocket()
{
}

void QJsonRpcServiceSocket::processRequestMessage(const QJsonRpcMessage &message)
{
    QJsonRpcServiceProvider::processMessage(this, message);
}

QJsonRpcServer::QJsonRpcServer(QJsonRpcServerPrivate *dd, QObject *parent)
    : QObject(parent),
      d_ptr(dd)
{
}

QJsonRpcServer::~QJsonRpcServer()
{
    Q_D(QJsonRpcServer);
     foreach (QJsonRpcSocket *client, d->clients)
        client->deleteLater();
    d->clients.clear();
}

QJsonRpcSocket::WireFormat QJsonRpcServer::wireFormat() const
{
    Q_D(const QJsonRpcServer);
    return d->format;
}

void QJsonRpcServer::setWireFormat(QJsonRpcSocket::WireFormat format)
{
    Q_D(QJsonRpcServer);
    d->format = format;
}

void QJsonRpcServer::notifyConnectedClients(const QString &method, const QVariantList &params)
{
    QJsonRpcMessage notification = QJsonRpcMessage::createNotification(method, params);
    notifyConnectedClients(notification);
}

void QJsonRpcServer::notifyConnectedClients(const QJsonRpcMessage &message)
{
    Q_D(QJsonRpcServer);
    for (int i = 0; i < d->clients.size(); ++i)
        d->clients[i]->notify(message);
}

void QJsonRpcServer::processMessage(const QJsonRpcMessage &message)
{
    QJsonRpcSocket *socket = static_cast<QJsonRpcSocket*>(sender());
    if (!socket) {
        qDebug() << Q_FUNC_INFO << "called without service socket";
        return;
    }

    QJsonRpcServiceProvider::processMessage(socket, message);
}


//
// LOCAL
//
QJsonRpcLocalServer::QJsonRpcLocalServer(QObject *parent)
    : QJsonRpcServer(new QJsonRpcLocalServerPrivate, parent)
{
}

QJsonRpcLocalServer::~QJsonRpcLocalServer()
{
    Q_D(QJsonRpcLocalServer);
    foreach (QLocalSocket *socket, d->socketLookup.keys())
        socket->deleteLater();
    d->socketLookup.clear();
}

bool QJsonRpcLocalServer::listen(const QString &service)
{
    Q_D(QJsonRpcLocalServer);
    if (!d->server) {
        d->server = new QLocalServer(this);
        connect(d->server, SIGNAL(newConnection()), this, SLOT(processIncomingConnection()));
    }

    return d->server->listen(service);
}

void QJsonRpcLocalServer::processIncomingConnection()
{
    Q_D(QJsonRpcLocalServer);
    QLocalSocket *localSocket = d->server->nextPendingConnection();
    QIODevice *device = qobject_cast<QIODevice*>(localSocket);
    QJsonRpcSocket *socket = new QJsonRpcSocket(device, this);
    socket->setWireFormat(d->format);
    connect(socket, SIGNAL(messageReceived(QJsonRpcMessage)), this, SLOT(processMessage(QJsonRpcMessage)));
    d->clients.append(socket);
    connect(localSocket, SIGNAL(disconnected()), this, SLOT(clientDisconnected()));
    d->socketLookup.insert(localSocket, socket);
}

void QJsonRpcLocalServer::clientDisconnected()
{
    Q_D(QJsonRpcLocalServer);
    QLocalSocket *localSocket = static_cast<QLocalSocket*>(sender());
    if (localSocket) {
        if (d->socketLookup.contains(localSocket)) {
            QJsonRpcSocket *socket = d->socketLookup.take(localSocket);
            d->clients.removeAll(socket);
        }
        localSocket->deleteLater();
    }
}

QString QJsonRpcLocalServer::errorString() const
{
    Q_D(const QJsonRpcLocalServer);
    return d->server->errorString();
}

//
// TCP
//
QJsonRpcTcpServer::QJsonRpcTcpServer(QObject *parent)
    : QJsonRpcServer(new QJsonRpcTcpServerPrivate, parent)
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
        connect(d->server, SIGNAL(newConnection()), this, SLOT(processIncomingConnection()));
    }

    return d->server->listen(address, port);
}

void QJsonRpcTcpServer::processIncomingConnection()
{
    Q_D(QJsonRpcTcpServer);
    QTcpSocket *tcpSocket = d->server->nextPendingConnection();
    QIODevice *device = qobject_cast<QIODevice*>(tcpSocket);
    QJsonRpcSocket *socket = new QJsonRpcSocket(device, this);
    socket->setWireFormat(d->format);
    connect(socket, SIGNAL(messageReceived(QJsonRpcMessage)), this, SLOT(processMessage(QJsonRpcMessage)));
    d->clients.append(socket);
    connect(tcpSocket, SIGNAL(disconnected()), this, SLOT(clientDisconnected()));
    d->socketLookup.insert(tcpSocket, socket);
}

void QJsonRpcTcpServer::clientDisconnected()
{
    Q_D(QJsonRpcTcpServer);
    QTcpSocket *tcpSocket = static_cast<QTcpSocket*>(sender());
    if (tcpSocket) {
        if (d->socketLookup.contains(tcpSocket)) {
            QJsonRpcSocket *socket = d->socketLookup.take(tcpSocket);
            d->clients.removeAll(socket);
        }
        tcpSocket->deleteLater();
    }
}

QString QJsonRpcTcpServer::errorString() const
{
    Q_D(const QJsonRpcTcpServer);
    return d->server->errorString();
}


