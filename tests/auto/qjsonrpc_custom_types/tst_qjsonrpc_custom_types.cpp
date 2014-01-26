/*
** Copyright (C) 2013 Fargier Sylvain <fargier.sylvain@free.fr>
**
** This software is provided 'as-is', without any express or implied
** warranty.  In no event will the authors be held liable for any damages
** arising from the use of this software.
**
** Permission is granted to anyone to use this software for any purpose,
** including commercial applications, and to alter it and redistribute it
** freely, subject to the following restrictions:
**
** 1. The origin of this software must not be misrepresented; you must not
**    claim that you wrote the original software. If you use this software
**    in a product, an acknowledgment in the product documentation would be
**    appreciated but is not required.
** 2. Altered source versions must be plainly marked as such, and must not be
**    misrepresented as being the original software.
** 3. This notice may not be removed or altered from any source distribution.
**
** tst_qjsonrpc_custom_types.cpp
**
**        Created on: Nov 06, 2013
**   Original Author: Fargier Sylvain <fargier.sylvain@free.fr>
**
*/

/*
 * CustomTypes are only supported in Qt >= 5.2
 * adding/removing QMetaType converters is required to use this
 */

#include <QtCore/QVariant>
#include <QtTest/QtTest>

#if QT_VERSION >= 0x050000
#include <QJsonDocument>
#else
#include "json/qjsondocument.h"
#endif

#include "qjsonrpcabstractserver.h"
#include "qjsonrpcmessage.h"
#include "qjsonrpcservice.h"

class TestQJsonRpcCustomTypes: public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void testCustomParams();
    void testCustomRet();
    void testInvalidParams();
    void testEnums();
    void testCommonMethodName();
};

class CustomClass : public QObject
{
public:
    CustomClass(int data = 0, QObject *parent = 0) :
        QObject(parent),
        data(data)
    {}

    CustomClass(const CustomClass &other) :
        QObject(),
        data(other.data)
    {}

    CustomClass &operator = (const CustomClass &other) {
        if (&other != this)
        {
            data = other.data;
        }
        return *this;
    }

    QJsonValue toJson() const
    {
        return QJsonValue(data);
    }

    static CustomClass fromJson(const QJsonValue &value)
    {
#if QT_VERSION >= 0x050200
        return CustomClass(value.toInt());
#else
        return CustomClass((int) value.toDouble());
#endif
    }

    int data;
};

Q_DECLARE_METATYPE(CustomClass)

class AnotherCustomClass
{
public:
    explicit AnotherCustomClass(const QString &str = QString()) :
        data(str)
    {}

    QJsonValue toJson() const
    {
        return QJsonValue(data);
    }

    static AnotherCustomClass fromJson(const QJsonValue &value)
    {
        return AnotherCustomClass(value.toString());
    }

    QString data;
};

Q_DECLARE_METATYPE(AnotherCustomClass)

class UnboundClass : public QObject
{
public:
    UnboundClass(QObject *parent = 0) :
        QObject(parent)
    {}
};


class TestService : public QJsonRpcService
{
    Q_OBJECT
    Q_CLASSINFO("serviceName", "service")
public:
    TestService(QObject *parent = 0)
        : QJsonRpcService(parent)
    {}

    bool testDispatch(const QJsonRpcMessage &message) {
        return QJsonRpcService::dispatch(message);
    }

    enum TestEnum
    {
        ZERO = 0, ONE = 1, TWO = 2, THREE = 3
    }
    Q_ENUMS(TestEnum);
    static QMetaEnum TestMetaEnum()
    {
        static int i = TestService::staticMetaObject.indexOfEnumerator("TestEnum");
        return TestService::staticMetaObject.enumerator(i);
    }

public Q_SLOTS:
    void testCustomParams(const CustomClass &param) const {
        QCOMPARE(param.data, 42);
    }

    CustomClass testCustomRet(const CustomClass &param) const {
        CustomClass ret(param);
        ++ret.data;

        return ret;
    }

    /*
     * QJsonRpc should be unable to bind this method.
     */
    void testInvalidParams(const UnboundClass &) const {
    }

    void testEnums(TestService::TestEnum) {
    }

    int testCommonMethodName(const CustomClass &c)
    { return c.data; }

    QString testCommonMethodName(const AnotherCustomClass &c)
    { return c.data; }

};

Q_DECLARE_METATYPE(TestService::TestEnum)

QJsonValue toJson(TestService::TestEnum e)
{
    return QString(TestService::TestMetaEnum().valueToKey(e));
}

TestService::TestEnum fromJson(const QJsonValue &val)
{
    if (val.isString())
    {
        QString str(val.toString());
        if (str.isEmpty())
            return TestService::ZERO;
        return (TestService::TestEnum) TestService::TestMetaEnum().keysToValue(str.toLatin1().constData());
    }
    else if (val.isDouble())
    {
        int idx = (int) val.toDouble();
        if (!TestService::TestMetaEnum().valueToKey(idx))
            return TestService::ZERO;
        return (TestService::TestEnum) idx;
    }
    else
        return TestService::ZERO;
}



class TestServiceProvider : public QObject, public QJsonRpcServiceProvider
{
    Q_OBJECT
public:
    TestServiceProvider() {}

    bool addService(QJsonRpcService *srv) {
        if (QJsonRpcServiceProvider::addService(srv)) {
            connect(srv, SIGNAL(result(QJsonRpcMessage)),
                    this, SLOT(reply(QJsonRpcMessage)));
            return true;
        }
        else
            return false;

    }

    QJsonRpcMessage last;

protected Q_SLOTS:
    void reply(const QJsonRpcMessage &msg) {
        last = msg;
    }
};

void TestQJsonRpcCustomTypes::initTestCase()
{
#if QT_VERSION >= 0x050200
    QMetaType::registerConverter(&CustomClass::toJson);
    QMetaType::registerConverter<QJsonValue, CustomClass>(&CustomClass::fromJson);
    QMetaType::registerConverter<TestService::TestEnum, QJsonValue>(&toJson);
    QMetaType::registerConverter<QJsonValue, TestService::TestEnum>(&fromJson);
    QMetaType::registerConverter(&AnotherCustomClass::toJson);
    QMetaType::registerConverter<QJsonValue, AnotherCustomClass>(&AnotherCustomClass::fromJson);
#endif
}

/*
 * Custom class parameter
 */
void TestQJsonRpcCustomTypes::testCustomParams()
{
    TestServiceProvider provider;
    TestService service;
    provider.addService(&service);
    CustomClass custom(42);

    QJsonRpcMessage request =
        QJsonRpcMessage::createRequest("service.testCustomParams",
                                       custom.toJson());
    QVERIFY(service.testDispatch(request));
}

/*
 * Custom return type class
 */
void TestQJsonRpcCustomTypes::testCustomRet()
{
    TestServiceProvider provider;
    TestService service;
    provider.addService(&service);

    QJsonRpcMessage request =
        QJsonRpcMessage::createRequest("service.testCustomRet",
                                       CustomClass().toJson());
    QVERIFY(service.testDispatch(request));

    QCOMPARE(provider.last.type(), QJsonRpcMessage::Response);
#if QT_VERSION >= 0x050100
    QVariant result = provider.last.result();
#else
    QVariant result = provider.last.result().toVariant(); /* won't work but at least will compile */
#endif

    QVERIFY(result.canConvert<CustomClass>());
    QCOMPARE(result.value<CustomClass>().data, 1);
}

/*
 * Methods with unconvertible parameters shouldn't be bound.
 */
void TestQJsonRpcCustomTypes::testInvalidParams()
{
    TestServiceProvider provider;
    TestService service;
    provider.addService(&service);
    UnboundClass custom;

    QJsonRpcMessage request =
        QJsonRpcMessage::createRequest("service.testInvalidParams",
                QJsonValue::fromVariant(QVariant::fromValue(CustomClass())));
    QVERIFY(!service.testDispatch(request));

    QCOMPARE(provider.last.type(), QJsonRpcMessage::Error);
    QCOMPARE(provider.last.errorCode(), (int) QJsonRpc::MethodNotFound);
}

/*
 * Invalid conversions are not properly supported yet, arguments must be checked
 * withing called function
 */
void TestQJsonRpcCustomTypes::testEnums()
{
    TestServiceProvider provider;
    TestService service;
    provider.addService(&service);

    QJsonRpcMessage request =
        QJsonRpcMessage::createRequest("service.testEnums", TestService::ONE);
    QVERIFY(service.testDispatch(request));

    request = QJsonRpcMessage::createRequest("service.testEnums",
                                             QLatin1String("ONE"));
    QVERIFY(service.testDispatch(request));
}

/*
 * There's no way to guess which method is the best to dispatch a custom type
 * for the moment, something like a signature, or a typetesting functor might
 * be good.
 *
 * This test will fail until then
 */
void TestQJsonRpcCustomTypes::testCommonMethodName()
{
    TestServiceProvider provider;
    TestService service;
    provider.addService(&service);

    QJsonRpcMessage request =
        QJsonRpcMessage::createRequest("service.testCommonMethodName",
                CustomClass(42).toJson());
    QVERIFY(service.testDispatch(request));
    QCOMPARE(provider.last.type(), QJsonRpcMessage::Response);
    CustomClass c(CustomClass::fromJson(provider.last.result()));
    QCOMPARE(c.data, 42);

    request = QJsonRpcMessage::createRequest("service.testCommonMethodName",
                AnotherCustomClass("test string").toJson());
    QVERIFY(service.testDispatch(request));
    QCOMPARE(provider.last.type(), QJsonRpcMessage::Response);
    AnotherCustomClass ac(AnotherCustomClass::fromJson(provider.last.result()));
    QCOMPARE(ac.data, QLatin1String("test string"));
}

QTEST_MAIN(TestQJsonRpcCustomTypes)
#include "tst_qjsonrpc_custom_types.moc"
