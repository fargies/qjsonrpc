#include <QtCore/QEventLoop>
#include <QtCore/QVariant>
#include <QtTest/QtTest>

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>

#include "json/qjsondocument.h"
#include "qjsonrpchttpserver.h"
#include "qjsonrpcmessage.h"

class TestQJsonRpcHttpServer: public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void quickTest();
    void sslTest();

private:
    QSslConfiguration serverSslConfiguration;
    QSslConfiguration clientSslConfiguration;

};

class TestService : public QJsonRpcService
{
    Q_OBJECT
    Q_CLASSINFO("serviceName", "service")
public:
    TestService(QObject *parent = 0)
        : QJsonRpcService(parent),
          m_called(0)
    {}

    void resetCount() { m_called = 0; }
    int callCount() const {
        return m_called;
    }

public Q_SLOTS:
    void noParam() const {}
    QString singleParam(const QString &string) const { return string; }
    QString multipleParam(const QString &first,
                          const QString &second,
                          const QString &third) const
    {
        return first + second + third;
    }

    void numberParameters(int intParam, double doubleParam, float floatParam)
    {
        Q_UNUSED(intParam)
        Q_UNUSED(doubleParam)
        Q_UNUSED(floatParam)
    }

    bool variantParameter(const QVariant &variantParam) const
    {
        return variantParam.toBool();
    }

    QVariant variantStringResult() {
        return "hello";
    }

    QVariantList variantListResult() {
        return QVariantList() << "one" << 2 << 3.0;
    }

    QVariantMap variantMapResult() {
        QVariantMap result;
        result["one"] = 1;
        result["two"] = 2.0;
        return result;
    }

    void increaseCalled() {
        m_called++;
    }

    bool methodWithListOfInts(const QList<int> &list) {
        if (list.size() < 3)
            return false;

        if (list.at(0) != 300)
            return false;
        if (list.at(1) != 30)
            return false;
        if (list.at(2) != 3)
            return false;
        return true;
    }

private:
    int m_called;
};

void TestQJsonRpcHttpServer::initTestCase()
{
    qRegisterMetaType<QJsonRpcMessage>("QJsonRpcMessage");

    // setup ssl configuration for tests
    QList<QSslCertificate> caCerts =
        QSslCertificate::fromPath(QLatin1String(":/certs/qt-test-server-cacert.pem"));
    serverSslConfiguration.setCaCertificates(caCerts);
    serverSslConfiguration.setProtocol(QSsl::AnyProtocol);
}

void TestQJsonRpcHttpServer::cleanupTestCase()
{
}

void TestQJsonRpcHttpServer::init()
{
}

void TestQJsonRpcHttpServer::cleanup()
{
}

void TestQJsonRpcHttpServer::quickTest()
{
    QJsonRpcHttpServer server;
    server.addService(new TestService);
    server.listen(QHostAddress::LocalHost, 8118);

    QJsonRpcMessage message = QJsonRpcMessage::createRequest("service.noParam");
    QJsonDocument document(message.toObject());

    QUrl requestUrl;
    requestUrl.setScheme("http");
    requestUrl.setHost("127.0.0.1");
    requestUrl.setPort(8118);
    QNetworkRequest request(requestUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json-rpc");
    request.setRawHeader("Accept", "application/json-rpc");

    QNetworkAccessManager manager;
    QNetworkReply *reply = manager.post(request, document.toJson());

    QEventLoop loop;
    connect(reply, SIGNAL(finished()), &loop, SLOT(quit()));
    QTimer::singleShot(5000, &loop, SLOT(quit()));
    loop.exec();

    qDebug() << reply->readAll();
    qDebug() << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    reply->deleteLater();

/*
    // Initialize the service provider.
    QJsonRpcLocalServer serviceProvider;
    serviceProvider.addService(new TestService);
    QVERIFY(serviceProvider.listen("test"));

    // Connect to the socket.
    QLocalSocket socket;
    socket.connectToServer("test");
    QVERIFY(socket.waitForConnected(1000));
    QJsonRpcSocket serviceSocket(&socket, this);
    QSignalSpy spyMessageReceived(&serviceSocket,
                                  SIGNAL(messageReceived(QJsonRpcMessage)));

    QJsonRpcMessage request = QJsonRpcMessage::createRequest("service.noParam");
    QJsonRpcMessage response = serviceSocket.sendMessageBlocking(request);
    QCOMPARE(spyMessageReceived.count(), 1);
    QVERIFY(response.errorCode() == QJsonRpc::NoError);
    QCOMPARE(request.id(), response.id());
*/
}

void TestQJsonRpcHttpServer::sslTest()
{
    QJsonRpcHttpServer server;
    server.setSslConfiguration(serverSslConfiguration);
    server.addService(new TestService);
    server.listen(QHostAddress::LocalHost, 8118);

    QJsonRpcMessage message = QJsonRpcMessage::createRequest("service.noParam");
    QJsonDocument document(message.toObject());

    QUrl requestUrl;
    requestUrl.setScheme("http");
    requestUrl.setHost("127.0.0.1");
    requestUrl.setPort(8118);
    QNetworkRequest request(requestUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json-rpc");
    request.setRawHeader("Accept", "application/json-rpc");
    request.setSslConfiguration(serverSslConfiguration);

    QNetworkAccessManager manager;
    QNetworkReply *reply = manager.post(request, document.toJson());

    QEventLoop loop;
    connect(reply, SIGNAL(finished()), &loop, SLOT(quit()));
    QTimer::singleShot(5000, &loop, SLOT(quit()));
    loop.exec();

    qDebug() << reply->readAll();
    qDebug() << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    reply->deleteLater();
}

QTEST_MAIN(TestQJsonRpcHttpServer)
#include "tst_qjsonrpchttpserver.moc"
