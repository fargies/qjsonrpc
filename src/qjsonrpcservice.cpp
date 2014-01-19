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
#include <QVarLengthArray>
#include <QMetaMethod>
#include <QEventLoop>
#include <QDebug>

#include "qjsonrpcsocket.h"
#include "qjsonrpcservice_p.h"
#include "qjsonrpcservice.h"
#include "qjsonrpcconverter.h"

QJsonRpcService::QJsonRpcService(QObject *parent)
    : QObject(parent),
      d_ptr(new QJsonRpcServicePrivate(this))
{
}

QJsonRpcService::~QJsonRpcService()
{
}

QJsonRpcSocket *QJsonRpcService::senderSocket()
{
    Q_D(QJsonRpcService);
    if (d->socket)
        return d->socket.data();
    return 0;
}

static int convertVariantTypeToJSType(int type)
{
    switch (type) {
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::Double:
    case QMetaType::Long:
    case QMetaType::LongLong:
    case QMetaType::Short:
    case QMetaType::Char:
    case QMetaType::ULong:
    case QMetaType::ULongLong:
    case QMetaType::UShort:
    case QMetaType::UChar:
    case QMetaType::Float:
        return QJsonValue::Double;    // all numeric types in js are doubles
    case QMetaType::QVariantList:
    case QMetaType::QStringList:
        return QJsonValue::Array;
    case QMetaType::QVariantMap:
        return QJsonValue::Object;
    case QMetaType::QString:
        return QJsonValue::String;
    case QMetaType::Bool:
        return QJsonValue::Bool;
    default:
        break;
    }

    return QJsonValue::Undefined;
}

static int qJsonNameToTypeId(const char *name)
{
    int id = static_cast<int>(QVariant::nameToType(name));
    if (id == QVariant::UserType || id == QVariant::Invalid)
        id = QMetaType::type(name);
    return id;
}

int QJsonRpcServicePrivate::qjsonRpcMessageType = qRegisterMetaType<QJsonRpcMessage>("QJsonRpcMessage");
void QJsonRpcServicePrivate::cacheInvokableInfo()
{
    if (!invokableMethodHash.isEmpty())
        return; // FIXME: empty hash and re-introspect ?

    QObject *obj = object();
    if (!obj)
        return;

    const QMetaObject *meta = obj->metaObject();
    int startIdx = obj->staticMetaObject.methodCount(); // skip QObject slots
    for (int idx = startIdx; idx < meta->methodCount(); ++idx) {
        const QMetaMethod method = meta->method(idx);
        if ((method.methodType() == QMetaMethod::Slot &&
             method.access() == QMetaMethod::Public) ||
             method.methodType() == QMetaMethod::Signal) {
#if QT_VERSION >= 0x050000
            QByteArray methodName = method.name();
#else
            QByteArray signature = method.signature();
            QByteArray methodName = signature.left(signature.indexOf('('));
#endif

            QList<int> parameterTypes;
            QList<int> jsParameterTypes;
            int type;
#if QT_VERSION >= 0x050000
            if ((type = method.returnType()) == QMetaType::UnknownType)
            {
                qWarning() << "QJsonRpcService: can't bind method's return type"
                    << QString(methodName);
                continue;
            }
#else
            if (qstrlen(method.typeName()) == 0) {
                /* typeName returns an empty string for void returns */
                type = 0;
            } else if ((type = qJsonNameToTypeId(method.typeName())) == 0) {
                qWarning() << "QJsonRpcService: can't bind method's return type"
                    << QString(methodName);
                continue;
            }
#endif
            parameterTypes << type;

            foreach(QByteArray parameterType, method.parameterTypes()) {
                type = QMetaType::type(parameterType);

                if (type == 0)
                {
                    qWarning() << "QJsonRpcService: can't bind method's parameter"
                        << QString(parameterType);
                    methodName.clear();
                    break;
                }
                parameterTypes << type;
                jsParameterTypes << convertVariantTypeToJSType(type);
            }
            if (methodName.isEmpty())
                continue;

            invokableMethodHash.insert(methodName, idx);
            parameterTypeHash[idx] = parameterTypes;
            jsParameterTypeHash[idx] = jsParameterTypes;
        }
    }
}

QObject *QJsonRpcServicePrivate::object()
{
    Q_Q(QJsonRpcService);

    if (q->metaObject() != &QJsonRpcService::staticMetaObject)
        return q;
    else
        return q->parent();
}

static bool variantAwareCompare(const QJsonArray &params, const QList<int> &jsParameterTypes)
{
    if (params.size() != jsParameterTypes.size())
        return false;

    QJsonArray::const_iterator it = params.constBegin();
    for (int i = 0; it != params.end(); ++it, ++i) {
        int jsType = jsParameterTypes.at(i);

        if ((*it).type() == jsType)
            continue;
        else if (jsType == QJsonValue::Undefined)
            continue;
        else
            return false;
    }

    return true;
}

QByteArray QJsonRpcService::serviceName()
{
    Q_D(QJsonRpcService);

    QObject *obj = d->object();
    if (!obj)
        return QByteArray();

    const QMetaObject *meta = obj->metaObject();
    int idx = meta->indexOfClassInfo("serviceName");
    if (idx >= 0)
        return meta->classInfo(idx).name();

    QVariant prop(obj->property("serviceName"));
    if (prop.isValid() && prop.canConvert<QByteArray>())
        return prop.value<QByteArray>();

    return QByteArray(meta->className()).toLower();
}

//QJsonRpcMessage QJsonRpcService::dispatch(const QJsonRpcMessage &request) const
bool QJsonRpcService::dispatch(const QJsonRpcMessage &request)
{
    Q_D(QJsonRpcService);
    if (request.type() != QJsonRpcMessage::Request &&
        request.type() != QJsonRpcMessage::Notification) {
        QJsonRpcMessage error =
            request.createErrorResponse(QJsonRpc::InvalidRequest, "invalid request");
        Q_EMIT result(error);
        return false;
    }

    QByteArray method = request.method().section(".", -1).toLatin1();
    if (!d->invokableMethodHash.contains(method)) {
        QJsonRpcMessage error =
            request.createErrorResponse(QJsonRpc::MethodNotFound, "invalid method called");
        Q_EMIT result(error);
        return false;
    }

    int idx = -1;
    QList<int> parameterTypes;
    QList<int> indexes = d->invokableMethodHash.values(method);
    const QJsonArray &arguments = request.params();

    foreach (int methodIndex, indexes) {
        if (variantAwareCompare(arguments, d->jsParameterTypeHash.value(methodIndex))) {
            parameterTypes = d->parameterTypeHash.value(methodIndex);
            idx = methodIndex;
            break;
        }
    }

    if (idx == -1) {
        QJsonRpcMessage error =
            request.createErrorResponse(QJsonRpc::InvalidParams, "invalid parameters");
        Q_EMIT result(error);
        return false;
    }

    QVarLengthArray<void *, 10> parameters;
    parameters.reserve(parameterTypes.count());
    QVector<QVariant> vars;
    vars.reserve(parameterTypes.count());

    // first argument to metacall is the return value
    vars.append(QVariant(parameterTypes.value(0), (const void *) 0));
    if (parameterTypes.value(0) == QMetaType::QVariant)
        parameters.append(&vars.last());
    else
        parameters.append(vars.last().data());

    // compile arguments
    for (int i = 0; i < parameterTypes.size() - 1; ++i) {
        int parameterType = parameterTypes.value(i + 1);

        QVariant var(QJsonRpcConverter::fromJson(arguments.at(i), parameterType));

        if (!var.isValid())
        {
#if !defined(QT_NO_DEBUG_STREAM) && !defined(QT_JSON_READONLY)
            qWarning() << "QJsonRpcService: failed to convert argument:" <<
                          arguments.at(i) << "to" << QMetaType::typeName(parameterType);
#else
            qWarning() << "QJsonRpcService: failed to convert argument to" <<
                          QMetaType::typeName(parameterType);
#endif

            QJsonRpcMessage error =
                    request.createErrorResponse(QJsonRpc::InvalidParams,
                                                QString(QLatin1String("invalid parameter")));
            Q_EMIT result(error);
            return false;
        }
        vars.append(var);
        parameters.append(const_cast<void *>(vars.last().constData()));
    }

    bool success =
        d->object()->qt_metacall(QMetaObject::InvokeMetaMethod, idx, parameters.data()) < 0;
    if (!success) {
        QString message = QString("dispatch for method '%1' failed").arg(method.constData());
        QJsonRpcMessage error =
            request.createErrorResponse(QJsonRpc::InvalidRequest, message);
        Q_EMIT result(error);
        return false;
    }

    // cleanup and result
    QVariant returnCopy(vars[0]);
    Q_EMIT result(request.createResponse(QJsonRpcConverter::toJson(returnCopy)));
    return true;
}

