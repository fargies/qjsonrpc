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
    void testAmbiguous();

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

public Q_SLOTS:
    QString testMethod(const QString &) const {
        return QLatin1String("String");
    }

    QString testMethod(int) const {
        return QLatin1String("int");
    }

    QString testMethod(const QVariant &) {
        return QLatin1String("Variant");
    }
};

class TestServiceProvider : public QObject, public QJsonRpcServiceProvider
{
    Q_OBJECT
public:
    TestServiceProvider() {}

    bool addService(QJsonRpcService *srv) {
        if (QJsonRpcServiceProvider::addService(srv)) {
            connect(srv, SIGNAL(result(QJsonRpcMessage)),
                    this, SLOT(reply(QJsonRpcMessage)), Qt::DirectConnection);
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

void TestQJsonRpcService::testAmbiguous()
{
    TestServiceProvider provider;
    TestService service;
    provider.addService(&service);

    QJsonRpcMessage request =
        QJsonRpcMessage::createRequest("service.testMethod",
                                       QLatin1String("test"));
    QVERIFY(service.testDispatch(request));
    QVERIFY(provider.last.result().isString());
    QCOMPARE(provider.last.result().toString(), QLatin1String("String"));

    request = QJsonRpcMessage::createRequest("service.testMethod", 42);
    QVERIFY(service.testDispatch(request));
    QVERIFY(provider.last.result().isString());
    QCOMPARE(provider.last.result().toString(), QLatin1String("int"));
}

QTEST_MAIN(TestQJsonRpcService)
#include "tst_qjsonrpcservice_dispatch.moc"
