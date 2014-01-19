
#include <fcntl.h>
#include <fcgios.h>
#include <unistd.h>

#include <QDebug>
#include <QSocketNotifier>

#include "qjsonrpcabstractserver_p.h"
#include "qjsonrpcfcgiserver.h"
#include "qjsonrpcfcgiserver_p.h"
#include "qjsonrpcsocket.h"

static const QString REQ_CONTENT_TYPE = "application/json";

class QJsonRpcFcgiServerPrivate : public QJsonRpcAbstractServerPrivate
{
public:
    QJsonRpcFcgiServerPrivate();

    int sock;
    QSocketNotifier *notifier;
};

QJsonRpcFcgiRequest::QJsonRpcFcgiRequest(
        int sockDesc,
        QObject *parent) :
    QIODevice(parent)
{
    if (!accept(sockDesc))
        return;

    if (parse())
    {
        m_notifier = new QSocketNotifier(m_req.ipcFd, QSocketNotifier::Read,
                this);
        connect(m_notifier, SIGNAL(activated(int)),
                this, SLOT(readIncomingData()));
        open(QIODevice::ReadWrite);
    }
}

QJsonRpcFcgiRequest::~QJsonRpcFcgiRequest()
{
    FCGX_Finish_r(&m_req);
}

bool QJsonRpcFcgiRequest::isSequential() const
{
    return true;
}

qint64 QJsonRpcFcgiRequest::readData(char *data, qint64 maxSize)
{
    int bytesRead = 0;
    if (!m_requestPayload.isEmpty()) {
        bytesRead = qMin(m_requestPayload.size(), (int)maxSize);
        memcpy(data, m_requestPayload.constData(), bytesRead);
        m_requestPayload.remove(0, bytesRead);
    }

    return bytesRead;
}

qint64 QJsonRpcFcgiRequest::writeData(const char *data, qint64 maxSize)
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

        // header
        FCGX_FPrintF(m_req.out, "HTTP/1.1 %i OK\r\n" \
                "Content-Type: application/json-rpc\r\n" \
                "\r\n", statusCode);
        FCGX_PutStr(m_responseBuffer.constData(), m_responseBuffer.size(),
                m_req.out);
        close();

        // then clear the buffer
        m_responseBuffer.clear();
    }

    return maxSize;
}

bool QJsonRpcFcgiRequest::accept(int sockDesc)
{
    if (FCGX_InitRequest(&m_req, sockDesc, 0) != 0) {
        qWarning("QJsonRpcFcgiServer: failed to init request");
        return false;
    }

    if (FCGX_Accept_r(&m_req) != 0) {
        qWarning("QJsonRpcFcgiServer: accept failed on request");
        return false;
    }
    OS_SetFlags(m_req.ipcFd, O_NONBLOCK);

    return true;
}

bool QJsonRpcFcgiRequest::parse()
{
    int err = 0;
    bool ok;

    //FIXME: check method

    m_length = QString(FCGX_GetParam("CONTENT_LENGTH", m_req.envp)).toInt(&ok);
    if (!ok)
    {
        qWarning() <<
            "QJsonRpcFcgiRequest: failed to parse CONTENT_LENGTH:" <<
            FCGX_GetParam("CONTENT_LENGTH", m_req.envp);
        err = 400;
    }
    else
        m_requestPayload.reserve(m_length);

    if (!err && !QString(FCGX_GetParam("CONTENT_TYPE",
                    m_req.envp)).contains(REQ_CONTENT_TYPE)) {
        // signal the error
        qDebug("didn't contain contentType");
//        err = 400;
    }

    if (err != 0)
    {
        FCGX_FPrintF(m_req.out, "HTTP/1.1 %i ERROR\r\n" \
                "\r\n", err);
        close();
        return false;
    }
    return true;
}

void QJsonRpcFcgiRequest::readIncomingData()
{
    int len = FCGX_GetStr(&m_requestPayload.data()[m_requestPayload.size()],
            m_length - m_requestPayload.size(), m_req.in);

    if (len < 0)
        close();
    else
    {
        m_requestPayload.resize(len + m_requestPayload.size());
        if (m_requestPayload.size() >= m_length)
            emit readyRead();
        else if (FCGX_HasSeenEOF(m_req.in) != 0)
            close();
        else
            m_notifier->setEnabled(true);
    }
}

QJsonRpcFcgiServerPrivate::QJsonRpcFcgiServerPrivate() :
    sock(-1),
    notifier(0)
{
}

QJsonRpcFcgiServer::QJsonRpcFcgiServer(QObject *parent) :
    QJsonRpcAbstractServer(new QJsonRpcFcgiServerPrivate, parent)
{
    FCGX_Init();
}

QJsonRpcFcgiServer::~QJsonRpcFcgiServer()
{
    Q_D(QJsonRpcFcgiServer);
    if (d->sock >= 0)
        ::close(d->sock);
}

bool QJsonRpcFcgiServer::listen()
{
    Q_D(QJsonRpcFcgiServer);
    if (d->sock < 0) {
        d->sock = 0;//FCGX_OpenSocket(qPrintable(addr), 5);
        d->notifier = new QSocketNotifier(d->sock, QSocketNotifier::Read, this);
        connect(d->notifier, SIGNAL(activated(int)), this, SLOT(processIncomingConnection()));
        d->notifier->setEnabled(true);
        return true;
    }
    return false;
}

void QJsonRpcFcgiServer::processIncomingConnection()
{
    Q_D(QJsonRpcFcgiServer);
    QJsonRpcFcgiRequest *fcgi = new QJsonRpcFcgiRequest(d->sock);

    if (fcgi->isOpen())
    {
        QJsonRpcSocket *socket = new QJsonRpcSocket(fcgi, this);
#if QT_VERSION >= 0x050100 || QT_VERSION <= 0x050000
        socket->setWireFormat(d->format);
#endif
        fcgi->setParent(socket);

        connect(socket, SIGNAL(messageReceived(QJsonRpcMessage)), this,
                SLOT(processMessage(QJsonRpcMessage)));
        d->clients.append(socket);
        connect(fcgi, SIGNAL(aboutToClose()), this, SLOT(clientDisconnected()));

        fcgi->readIncomingData();
    }
    else
        delete fcgi;
    d->notifier->setEnabled(true);
}

void QJsonRpcFcgiServer::clientDisconnected()
{
    QJsonRpcFcgiRequest *fcgi = static_cast<QJsonRpcFcgiRequest*>(sender());
    if (fcgi) {
        fcgi->parent()->deleteLater();
    }
}

QString QJsonRpcFcgiServer::errorString() const
{
    return QString();
}

