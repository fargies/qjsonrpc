#include <QStringList>
#include <QTcpSocket>
#include <QDateTime>

#include "qjsondocument.h"
#include "qjsonrpcmessage.h"
#include "qjsonrpchttpserver_p.h"
#include "qjsonrpchttpserver.h"

QJsonRpcHttpRequest::QJsonRpcHttpRequest(int socketDescriptor, QObject *parent)
    : QIODevice(parent),
      m_requestSocket(0),
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

    m_requestSocket = new QTcpSocket(this);
    m_requestSocket->setSocketDescriptor(socketDescriptor);
    connect(m_requestSocket, SIGNAL(readyRead()), this, SLOT(readIncomingData()));

    open(QIODevice::ReadWrite);
}

QJsonRpcHttpRequest::~QJsonRpcHttpRequest()
{
}

bool QJsonRpcHttpRequest::isSequential() const
{
    return true;
}

qint64 QJsonRpcHttpRequest::readData(char *data, qint64 maxSize)
{
    int bytesRead = 0;
    if (!m_requestPayload.isEmpty()) {
        int bytesToRead = qMin(m_requestPayload.size(), (int)maxSize);
        for (int byte = 0; byte < bytesToRead; ++byte, ++bytesRead)
            data[bytesRead] = m_requestPayload[byte];
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
        QJsonRpcMessage message(document.object());
        switch (message.type()) {
        case QJsonRpcMessage::Response:
            break;

        case QJsonRpcMessage::Notification:
            break;

        case QJsonRpcMessage::Invalid:
            break;

        case QJsonRpcMessage::Error:
            break;

        case QJsonRpcMessage::Request:
            // unhandled
            break;
        }

        QTextStream os(m_requestSocket);
        os.setAutoDetectUnicode(true);
        os << "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json-rpc\r\n"
            "\r\n";
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
//    qDebug() << "body";
    return 0;
}

int QJsonRpcHttpRequest::onMessageComplete(http_parser *parser)
{
    QJsonRpcHttpRequest *request = (QJsonRpcHttpRequest *)parser->data;
    Q_EMIT request->readyRead();
//    qDebug() << "message complete";
    return 0;
}

int QJsonRpcHttpRequest::onHeadersComplete(http_parser *parser)
{
    QJsonRpcHttpRequest *request = (QJsonRpcHttpRequest *)parser->data;
//    qDebug() << "headers complete: " << request->m_requestHeaders;

    if (parser->method != HTTP_GET && parser->method != HTTP_POST) {
        // close the socket, cleanup, delete, etc..
        qDebug() << "invalid method: " << parser->method;
        return -1;
    }

    // check headers
    // see: http://www.jsonrpc.org/historical/json-rpc-over-http.html#http-header
    if (!request->m_requestHeaders.contains("Content-Type") ||
        !request->m_requestHeaders.contains("Content-Length") ||
        !request->m_requestHeaders.contains("Accept")) {
        // signal the error somehow
        qDebug() << "did not contain the right headers";
        return -1;
    }

    QStringList supportedContentTypes =
        QStringList() << "application/json-rpc" << "application/json" << "application/jsonrequest";
    QString contentType = request->m_requestHeaders.value("Content-Type");
    QString acceptType = request->m_requestHeaders.value("Accept");
    if (!supportedContentTypes.contains(contentType) || !supportedContentTypes.contains(acceptType)) {
        // signal the error
        qDebug() << "didn't contain contentType or acceptType";
        return -1;
    }

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
//    qDebug() << "message begin";
    return 0;
}

int QJsonRpcHttpRequest::onUrl(http_parser *parser, const char *at, size_t length)
{
    Q_UNUSED(parser)
    Q_UNUSED(at)
    Q_UNUSED(length)
//    QString url = QString::fromAscii(at, length);
//    qDebug() << "requested url: " << url;
    return 0;
}

QJsonRpcHttpServer::QJsonRpcHttpServer(QObject *parent)
    : QTcpServer(parent)
{
}

QJsonRpcHttpServer::~QJsonRpcHttpServer()
{
}

void QJsonRpcHttpServer::incomingConnection(int socketDescriptor)
{
    QJsonRpcHttpRequest *request = new QJsonRpcHttpRequest(socketDescriptor, this);
    QJsonRpcSocket *socket = new QJsonRpcSocket(request, this);
    m_requests.insert(socket, request);
    connect(socket, SIGNAL(messageReceived(QJsonRpcMessage)), this, SLOT(processIncomingMessage(QJsonRpcMessage)));
}

void QJsonRpcHttpServer::processIncomingMessage(const QJsonRpcMessage &message)
{
    QJsonRpcSocket *socket = qobject_cast<QJsonRpcSocket*>(sender());
    if (!socket)
        return;

    processMessage(socket, message);
}
