/*
 * Copyright (C) 2012-2013 Matt Broadstone
 * Contact: http://bitbucket.org/devonit/qjsonrpc
 *
 * This file is part of the QJsonRpc Library.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
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

class TestQJsonRpcService: public QObject
{
    Q_OBJECT
private slots:
    void testDispatch();
    void testSignals();
    void testDetached();
    void testChild();
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

Q_SIGNALS:
    void testSignal();
    void testSignalWithParameter(const QString &param);

public Q_SLOTS:
    QString testMethod(const QString &string) const {
        return string;
    }

};

class TestDetachedService : public QObject
{
    Q_OBJECT
public:
    TestDetachedService(QObject *parent = 0)
        : QObject(parent)
    {}

Q_SIGNALS:
    void testSignal();
    void testSignalWithParameter(const QString &param);

public Q_SLOTS:
    QString testMethod(const QString &string) const {
        return string;
    }
};

class TestServiceProvider : public QJsonRpcServiceProvider
{
public:
    TestServiceProvider() {}
};

void TestQJsonRpcService::testDispatch()
{
    TestServiceProvider provider;
    TestService service;
    provider.addService(&service);

    QJsonRpcMessage validRequestDispatch =
        QJsonRpcMessage::createRequest("service.testMethod", QLatin1String("testParam"));
    QVERIFY(service.testDispatch(validRequestDispatch));

    QJsonRpcMessage validNotificationDispatch =
        QJsonRpcMessage::createNotification("service.testMethod", QLatin1String("testParam"));
    QVERIFY(service.testDispatch(validNotificationDispatch));

    QJsonRpcMessage invalidResponseDispatch =
        validRequestDispatch.createResponse(QLatin1String("testResult"));
    QVERIFY(!service.testDispatch(invalidResponseDispatch));

    QJsonRpcMessage invalidDispatch;
    QVERIFY(!service.testDispatch(invalidDispatch));
}

void TestQJsonRpcService::testSignals()
{
    TestServiceProvider provider;
    TestService service;
    provider.addService(&service);

    QJsonRpcMessage validRequestSignalDispatch =
        QJsonRpcMessage::createRequest("service.testSignal");
    QVERIFY(service.testDispatch(validRequestSignalDispatch));

    QJsonRpcMessage validRequestSignalWithParamDispatch =
        QJsonRpcMessage::createRequest("service.testSignalWithParameter", QLatin1String("testParam"));
    QVERIFY(service.testDispatch(validRequestSignalWithParamDispatch));

    QJsonRpcMessage invalidRequestSignalDispatch =
        QJsonRpcMessage::createRequest("service.testSignal", QLatin1String("testParam"));
    QCOMPARE(service.testDispatch(invalidRequestSignalDispatch), false);
}

void TestQJsonRpcService::testDetached()
{
    TestServiceProvider provider;
    TestDetachedService object;
    QJsonRpcService service(&object);
    service.setProperty("serviceName", "service");

    provider.addService(&service);

    QJsonRpcMessage validRequestDispatch =
        QJsonRpcMessage::createRequest(QLatin1String("service.testMethod"),
                                       QLatin1String("testParam"));
    QVERIFY(service.dispatch(validRequestDispatch));

    QJsonRpcMessage validNotificationDispatch =
        QJsonRpcMessage::createNotification(QLatin1String("service.testMethod"),
                                            QLatin1String("testParam"));
    QVERIFY(service.dispatch(validNotificationDispatch));

    QJsonRpcMessage invalidResponseDispatch =
        validRequestDispatch.createResponse(QLatin1String("testResult"));
    QVERIFY(!service.dispatch(invalidResponseDispatch));

    QJsonRpcMessage invalidDispatch;
    QVERIFY(!service.dispatch(invalidDispatch));

    QJsonRpcMessage validRequestSignalDispatch =
        QJsonRpcMessage::createRequest(QLatin1String("service.testSignal"));
    QVERIFY(service.dispatch(validRequestSignalDispatch));

    QJsonRpcMessage validRequestSignalWithParamDispatch =
        QJsonRpcMessage::createRequest(QLatin1String("service.testSignalWithParameter"),
                                       QLatin1String("testParam"));
    QVERIFY(service.dispatch(validRequestSignalWithParamDispatch));

    QJsonRpcMessage invalidRequestSignalDispatch =
        QJsonRpcMessage::createRequest(QLatin1String("service.testSignal"),
                                       QLatin1String("testParam"));
    QCOMPARE(service.dispatch(invalidRequestSignalDispatch), false);
}

void TestQJsonRpcService::testChild()
{
    TestServiceProvider provider;
    TestDetachedService object;
    TestDetachedService *child = new TestDetachedService(&object);
    child->setObjectName("child1");

    QJsonRpcService service(&object);
    service.setChildWatch(true, true);

    child = new TestDetachedService(&object);
    child->setObjectName("child2");

    provider.addService(&service);

    QJsonRpcMessage validRequestDispatch =
        QJsonRpcMessage::createRequest("service.child1.testMethod",
                QString("testParam"));
    QVERIFY(service.dispatch(validRequestDispatch));

    validRequestDispatch =
        QJsonRpcMessage::createRequest("service.child2.testMethod",
                QString("testParam"));
    QVERIFY(service.dispatch(validRequestDispatch));

    delete child;
    validRequestDispatch =
        QJsonRpcMessage::createRequest("service.child2.testMethod",
                QString("testParam"));
    QVERIFY(service.dispatch(validRequestDispatch));
}

QTEST_MAIN(TestQJsonRpcService)
#include "tst_qjsonrpcservice.moc"
