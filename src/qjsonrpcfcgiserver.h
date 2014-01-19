/*
** Copyright (C) 2013 Fargier Sylvain <fargier.sylvain@free.fr>
**
** This file is part of the QJsonRpc Library.
**
** This library is free software; you can redistribute it and/or
** modify it under the terms of the GNU Lesser General Public
** License as published by the Free Software Foundation; either
** version 2.1 of the License, or (at your option) any later version.
**
** This library is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
** Lesser General Public License for more details.
**
** qjsonrpcfcgiserver.h
**
**        Created on: Nov 13, 2013
**   Original Author: Fargier Sylvain <fargier.sylvain@free.fr>
**
*/
#ifndef QJSONRPCFCGISERVER_H
#define QJSONRPCFCGISERVER_H

#include <QString>
#include <QObject>

#include "qjsonrpcabstractserver.h"

class QJsonRpcFcgiServerPrivate;
class QJSONRPC_EXPORT QJsonRpcFcgiServer : public QJsonRpcAbstractServer
{
    Q_OBJECT
public:
    QJsonRpcFcgiServer(QObject *parent = 0);
    ~QJsonRpcFcgiServer();

    QString errorString() const;
    bool listen();

protected Q_SLOTS:
    void processIncomingConnection();
    void clientDisconnected();

private:
    Q_DECLARE_PRIVATE(QJsonRpcFcgiServer)

};

#endif
