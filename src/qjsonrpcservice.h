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
#ifndef QJSONRPCSERVICE_H
#define QJSONRPCSERVICE_H

#include <QVariant>
#include "qjsonrpcmessage.h"

class QJsonRpcSocket;
class QJsonRpcServiceProvider;
class QJsonRpcServicePrivate;
class QJsonRpcServiceEventFilter;

class QJSONRPC_EXPORT QJsonRpcService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool childWatch READ childWatch WRITE setChildWatch);

public:
    /**
     * @brief create a new QJsonRpcService.
     *
     * @details if the service is created as a standalone class it will bind
     * it's own parent.
     */
    explicit QJsonRpcService(QObject *parent = 0);
    ~QJsonRpcService();

    QByteArray serviceName();

    /**
     * @brief watch for child creation.
     *
     * @details
     * objects created are automatically added as services.
     *
     * @param[in] value enable/disable children mode.
     * @param[in] live add/remove service on current children.
     */
    void setChildWatch(bool value, bool live = false);
    bool childWatch() const;

Q_SIGNALS:
    void result(const QJsonRpcMessage &result);
    void notifyConnectedClients(const QJsonRpcMessage &message);
    void notifyConnectedClients(const QString &method, const QJsonArray &params = QJsonArray());

    /**
     * @brief signal emitted whenever a child service is added.
     */
    void childServiceAdded(QJsonRpcService *);

protected:
    QJsonRpcSocket *senderSocket();
    void addChildService(QObject *obj);

public Q_SLOTS:
    bool dispatch(const QJsonRpcMessage &request);

private:
    Q_DECLARE_PRIVATE(QJsonRpcService)
    QScopedPointer<QJsonRpcServicePrivate> d_ptr;
    friend class QJsonRpcServiceProvider;
    friend class QJsonRpcServiceEventFilter;
};

#endif

