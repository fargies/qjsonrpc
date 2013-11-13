#include <QStringList>
#include <QTcpSocket>
#include <QHash>
#include <QDateTime>

#include "qjsondocument.h"
#include "qjsonrpcsocket.h"
#include "qjsonrpcmessage.h"
#include "qjsonrpchttpserver_p.h"
#include "qjsonrpchttpserver.h"
#include "qjsonrpctcpserver_p.h"

static const QString REQ_CONTENT_TYPE = "application/json";

class QJsonRpcHttpServerPrivate : public QJsonRpcTcpServerPrivate
{
public:
    QJsonRpcHttpServerPrivate() {}
    QHash<QTcpSocket*, QJsonRpcHttpRequest*> requests;
};

QJsonRpcHttpRequest::QJsonRpcHttpRequest(QAbstractSocket *socket, QObject *parent)
    : QIODevice(parent),
      m_requestSocket(socket),
      m_requestParser(0)
{
    // initialize request parser
    m_requestParser = (http_parser*)malloc(sizeof(http_parser));
    http_parser_init(m_requestParser, HTTP_REQUEST);
    m_requestParserSettings.on_message_begin = onMessageBegin;
    m_requestParserSettings.on_url = onUrl;
    m_requestParserSettings.on_header_field = onHeaderField;
    m_requestParserSettings.on_header_value = onHeaderValue;
    m_requestParserSettings.on_headers_complete = onHeadersComplete;
    m_requestParserSettings.on_body = onBody;
    m_requestParserSettings.on_message_complete = onMessageComplete;
    m_requestParser->data = this;

    m_requestSocket->setParent(this);
    connect(m_requestSocket, SIGNAL(readyRead()), this, SLOT(readIncomingData()));
    open(QIODevice::ReadWrite);
}

QJsonRpcHttpRequest::~QJsonRpcHttpRequest()
{
    free(m_requestParser);
}

bool QJsonRpcHttpRequest::isSequential() const
{
    return true;
}

qint64 QJsonRpcHttpRequest::readData(char *data, qint64 maxSize)
{
    int bytesRead = 0;
    if (!m_requestPayload.isEmpty()) {
        bytesRead = qMin(m_requestPayload.size(), (int)maxSize);
        memcpy(data, m_requestPayload.constData(), bytesRead);
        m_requestPayload.remove(0, bytesRead);
    }

    return bytesRead;
}

qint64 QJsonRpcHttpRequest::writeData(const char *data, qint64 maxSize)
{
    m_responseBuffer.append(data, (int)maxSize);
    QJsonDocument document = QJsonDocument::fromJson(m_responseBuffer);
    if (document.isObject()) {
        // determine the HTTP code to respond with
        int statusCode = 200;
        QJsonRpcMessage message(document.object());
        switch (message.type()) {
        case QJsonRpcMessage::Error:
            switch (message.errorCode()) {
            case QJsonRpc::InvalidRequest:
                statusCode = 400;
                break;

            case QJsonRpc::MethodNotFound:
                statusCode = 404;
                break;

            default:
                statusCode = 500;
                break;
            }
            break;

        case QJsonRpcMessage::Invalid:
            statusCode = 400;
            break;

        case QJsonRpcMessage::Notification:
        case QJsonRpcMessage::Response:
        case QJsonRpcMessage::Request:
            statusCode = 200;
            break;
        }

        QTextStream os(m_requestSocket);
#ifndef QT_NO_TEXTCODEC
        os.setAutoDetectUnicode(true);
#endif

        // header
        os << "HTTP/1.1 " << QByteArray::number(statusCode) << " OK\r\n";
        os << "Content-Type: application/json-rpc\r\n"
           << "\r\n";

        // body
        os << m_responseBuffer;
        m_requestSocket->close();

        // then clear the buffer
        m_responseBuffer.clear();
    }

    return maxSize;
}

void QJsonRpcHttpRequest::readIncomingData()
{
    QByteArray requestBuffer = m_requestSocket->readAll();
    http_parser_execute(m_requestParser, &m_requestParserSettings,
                        requestBuffer.constData(), requestBuffer.size());
}

int QJsonRpcHttpRequest::onBody(http_parser *parser, const char *at, size_t length)
{
    QJsonRpcHttpRequest *request = (QJsonRpcHttpRequest *)parser->data;
    request->m_requestPayload = QByteArray(at, length);

    return 0;
}

int QJsonRpcHttpRequest::onMessageComplete(http_parser *parser)
{
    QJsonRpcHttpRequest *request = (QJsonRpcHttpRequest *)parser->data;
    Q_EMIT request->readyRead();

    return 0;
}

int QJsonRpcHttpRequest::onHeadersComplete(http_parser *parser)
{
    int err = 0;
    QJsonRpcHttpRequest *request = (QJsonRpcHttpRequest *)parser->data;

    if (parser->method != HTTP_GET && parser->method != HTTP_POST) {
        // close the socket, cleanup, delete, etc..
        qDebug() << "invalid method: " << parser->method;
        err = 501;
    }

    // check headers
    // see: http://www.jsonrpc.org/historical/json-rpc-over-http.html#http-header
    if (!err && (!request->m_requestHeaders.contains("Content-Type") ||
        !request->m_requestHeaders.contains("Content-Length") ||
        !request->m_requestHeaders.contains("Accept"))) {
        // signal the error somehow
        qDebug() << "did not contain the right headers";
        err = 400;
    }

    if (!err && !request->m_requestHeaders.value("Content-Type").contains(REQ_CONTENT_TYPE)) {
        // signal the error
        qDebug("didn't contain contentType");
        err = 400;
    }
    if (!err && !request->m_requestHeaders.value("Accept").contains(REQ_CONTENT_TYPE)) {
        qWarning("didn't contain acceptType");
    }

    if (err != 0)
    {
        QTextStream os(request->m_requestSocket);
        os << "HTTP/1.1 " << QByteArray::number(err) << " ERROR\r\n"
            << "\r\n";
        request->m_requestSocket->close();
        return -1;
    }
    else
        return 0;
}

int QJsonRpcHttpRequest::onHeaderField(http_parser *parser, const char *at, size_t length)
{
    QJsonRpcHttpRequest *request = (QJsonRpcHttpRequest *)parser->data;
    if (!request->m_currentHeaderField.isEmpty() && !request->m_currentHeaderValue.isEmpty()) {
        request->m_requestHeaders.insert(request->m_currentHeaderField, request->m_currentHeaderValue);
        request->m_currentHeaderField.clear();
        request->m_currentHeaderValue.clear();
    }

    request->m_currentHeaderField.append(QString::fromAscii(at, length));
    return 0;
}

int QJsonRpcHttpRequest::onHeaderValue(http_parser *parser, const char *at, size_t length)
{
    QJsonRpcHttpRequest *request = (QJsonRpcHttpRequest *)parser->data;
    request->m_currentHeaderValue.append(QString::fromAscii(at, length));
    return 0;
}

int QJsonRpcHttpRequest::onMessageBegin(http_parser *parser)
{
    QJsonRpcHttpRequest *request = (QJsonRpcHttpRequest *)parser->data;
    request->m_requestHeaders.clear();

    return 0;
}

int QJsonRpcHttpRequest::onUrl(http_parser *parser, const char *at, size_t length)
{
    Q_UNUSED(parser)
    Q_UNUSED(at)
    Q_UNUSED(length)

    return 0;
}

QJsonRpcHttpServer::QJsonRpcHttpServer(QObject *parent)
    : QJsonRpcTcpServer(new QJsonRpcHttpServerPrivate, parent)
{
}

QJsonRpcHttpServer::~QJsonRpcHttpServer()
{
}

/*
 * TODO: handle ssl configurations directly in the server part by overriding
 * nextPendingConnection() method.
 */
void QJsonRpcHttpServer::processIncomingConnection()
{
    Q_D(QJsonRpcHttpServer);
    QTcpSocket *tcpSocket = d->server->nextPendingConnection();
    if (!tcpSocket) {
        qDebug() << Q_FUNC_INFO << "nextPendingConnection is null";
        return;
    }

    QJsonRpcHttpRequest *request = new QJsonRpcHttpRequest(tcpSocket, this);
    QJsonRpcSocket *socket = new QJsonRpcSocket(request, this);
#if QT_VERSION >= 0x050100 || QT_VERSION <= 0x050000
    socket->setWireFormat(d->format);
#endif

    connect(socket, SIGNAL(messageReceived(QJsonRpcMessage)), this, SLOT(processMessage(QJsonRpcMessage)));
    d->clients.append(socket);
    connect(tcpSocket, SIGNAL(disconnected()), this, SLOT(clientDisconnected()));
    d->socketLookup.insert(tcpSocket, socket);
    d->requests.insert(tcpSocket, request);
}

void QJsonRpcHttpServer::clientDisconnected()
{
    Q_D(QJsonRpcHttpServer);
    QTcpSocket *tcpSocket = static_cast<QTcpSocket*>(sender());
    if (tcpSocket) {
        if (d->requests.contains(tcpSocket)) {
            QJsonRpcHttpRequest *req = d->requests.take(tcpSocket);
            req->deleteLater();
        }
    }
    QJsonRpcTcpServer::clientDisconnected();
}

