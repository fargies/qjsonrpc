#ifndef QJSONRPCFCGISERVER_P_H
#define QJSONRPCFCGISERVER_P_H

#include <QIODevice>
#include <QByteArray>
#include <QSocketNotifier>

#include <fcgiapp.h>

class QJsonRpcFcgiRequest : public QIODevice
{
    Q_OBJECT
public:
    explicit QJsonRpcFcgiRequest(int sockDesc, QObject *parent = 0);
    ~QJsonRpcFcgiRequest();

    bool isSequential() const;

protected:
    qint64 readData(char *data, qint64 maxSize);
    qint64 writeData(const char *data, qint64 maxSize);

    bool accept(int sockDesc);
    bool parse();

public Q_SLOTS:
    void readIncomingData();

private:
    Q_DISABLE_COPY(QJsonRpcFcgiRequest)

    // request
    QByteArray m_requestPayload;

    // response
    QByteArray m_responseBuffer;

    QSocketNotifier *m_notifier;

    FCGX_Request m_req;
    int m_length;
};

#endif
