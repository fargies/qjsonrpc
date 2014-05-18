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

QJsonRpcService::QJsonRpcService(QObject *parent)
    : QObject(*new QJsonRpcServicePrivate(this), parent)
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

int convertVariantTypeToJSType(int type)
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

int QJsonRpcServicePrivate::qjsonRpcMessageType = qRegisterMetaType<QJsonRpcMessage>("QJsonRpcMessage");
void QJsonRpcServicePrivate::cacheInvokableInfo()
{
    Q_Q(QJsonRpcService);
    const QMetaObject *obj = q->metaObject();
    int startIdx = q->staticMetaObject.methodCount(); // skip QObject slots
    for (int idx = startIdx; idx < obj->methodCount(); ++idx) {
        const QMetaMethod method = obj->method(idx);
        if ((method.methodType() == QMetaMethod::Slot &&
             method.access() == QMetaMethod::Public) ||
             method.methodType() == QMetaMethod::Signal) {

#if QT_VERSION >= 0x050000
            QByteArray signature = method.methodSignature();
            QByteArray methodName = method.name();
#else
            QByteArray signature = method.signature();
            QByteArray methodName = signature.left(signature.indexOf('('));
#endif

            MethodInfo info;

#if QT_VERSION >= 0x050000
            info.params.reserve(method.parameterCount());

            if ((info.retType = method.returnType()) == QMetaType::UnknownType)
            {
                qWarning() << "QJsonRpcService: can't bind method's return type"
                           << QString(methodName);
                continue;
            }
#else
            if (qstrlen(method.typeName()) == 0) {
                /* typeName returns an empty string for void returns */
                info.retType = 0;
            } else if ((info.retType = QMetaType::type(method.typeName())) == 0) {
                qWarning() << "QJsonRpcService: can't bind method's return type"
                           << QString(methodName);
                continue;
            }
#endif

            const QList<QByteArray> &types = method.parameterTypes();
            const QList<QByteArray> &names = method.parameterNames();
            for (int i = 0; i < types.size(); ++i)
            {
                QByteArray parameterType = types.at(i);
                const QByteArray &parameterName = names.at(i);

                bool out = false;
                if (parameterType.endsWith('&'))
                {
                    out = true;
                    info.hasOut = true;
                    parameterType.resize(parameterType.size() - 1);
                }
                int type = QMetaType::type(parameterType);
                if (type == 0)
                {
                    qWarning() <<  "QJsonRpcService: can't bind method's parameter"
                                << QString(parameterType);
                    methodName.clear();
                    break;
                }
                info.params.push_back(ParamInfo(type, convertVariantTypeToJSType(type),
                                      parameterName, out));
            }
            if (methodName.isEmpty())
                continue;

            if (signature.contains("QVariant"))
                invokableMethodHash[methodName].append(idx);
            else
                invokableMethodHash[methodName].prepend(idx);
            methods[idx] = info;
        }
    }
}

static bool jsParamCompare(const QJsonArray &params, const QJsonRpcServicePrivate::MethodInfo &info)
{
    int j = 0;
    for (int i = 0; i < info.params.size() && j < params.size(); ++i)
    {
        int jsType = info.params.at(i).jsType;
        if ((jsType != QJsonValue::Undefined) &&
                (jsType != params.at(j).type()))
        {
            if (!info.params.at(i).out)
                return false;
        }
        else
            ++j;
    }
    return (j == params.size());
}

static bool jsParamCompare(const QJsonObject &params, const QJsonRpcServicePrivate::MethodInfo &info)
{
    for (int i = 0; i < info.params.size(); ++i)
    {
        int jsType = info.params.at(i).jsType;
        QJsonValue value = params.value(info.params.at(i).name);
        if (value == QJsonValue::Undefined)
        {
            if (!info.params.at(i).out)
                return false;
        }
        else if (jsType == QJsonValue::Undefined)
            continue;
        else if (jsType != value.type())
            return false;
    }
    return true;
}

/*
 * Convenience function to convert JSonValue to the right type.
 */
static inline QVariant argumentConvert(
        const QJsonValue &val,
        const QJsonRpcServicePrivate::ParamInfo &info)
{
    if (val.isUndefined())
        return QVariant(info.type, NULL);

#if QT_VERSION >= 0x050200
    if (info.type >= QMetaType::User)
    {
        /* for user defined types only custom conversion will work */
        QVariant arg(val);
        if (arg.convert(info.type))
            return arg;
    }
    else if (info.type == QMetaType::QJsonValue ||
             info.type == QMetaType::QVariant)
        return QVariant(val);

    QVariant arg(val.toVariant());
    if (arg.userType() == info.type || info.type == QMetaType::QVariant)
        return arg;
    else if (arg.convert(info.type))
        return arg;
    else if (info.type < QMetaType::User) /* already tried for >= user */
    {
        QVariant arg(val);
        if (arg.convert(info.type))
            return arg;
    }
    return QVariant();
#else
    QVariant arg(val.toVariant());
    /* custom conversions could not be registered before 5.2, so this is only
         * an optimization */
    if (info.type != QMetaType::QVariant &&
            info.type != arg.type() &&
            !arg.convert(static_cast<QVariant::Type>(info.type)))
        return QVariant();
    return arg;
#endif
}

static inline QJsonValue retConvert(QVariant &ret)
{
#if QT_VERSION >= 0x050200
    switch (ret.type())
    {
    case QMetaType::Bool:
    case QMetaType::Int:
    case QMetaType::Double:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::UInt:
    case QMetaType::QString:
    case QMetaType::QStringList:
    case QMetaType::QVariantList:
    case QMetaType::QVariantMap:
        return QJsonValue::fromVariant(ret);
    default:
        /* if a conversion operator was registered it will be used */
        if (ret.convert(QMetaType::QJsonValue))
            return ret.toJsonValue();
        else
            return QJsonValue();
    }
#else
    /* custom conversions could not be registered before 5.2, so this is only
     * an optimization */
    return QJsonValue::fromVariant(ret);
#endif
}

static inline QByteArray methodName(const QJsonRpcMessage &request)
{
    const QString &methodPath(request.method());
    return methodPath.midRef(methodPath.lastIndexOf('.') + 1).toLatin1();
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

    const QByteArray &method(methodName(request));
    if (!d->invokableMethodHash.contains(method)) {
        QJsonRpcMessage error =
            request.createErrorResponse(QJsonRpc::MethodNotFound, "invalid method called");
        Q_EMIT result(error);
        return false;
    }

    int idx = -1;
    QVariantList arguments;
    const QList<int> &indexes = d->invokableMethodHash.value(method);
    const QJsonValue &params = request.params();
    QVarLengthArray<void *, 10> parameters;
    QVariant returnValue;
    QMetaType::Type returnType = QMetaType::Void;

    if (params.isObject()) {
        QJsonObject namedParametersObject = params.toObject();
        foreach (int methodIndex, indexes) {
            QJsonRpcServicePrivate::MethodInfo &info = d->methods[methodIndex];
            if (jsParamCompare(namedParametersObject, info)) {
                idx = methodIndex;
                arguments.reserve(info.params.size());
                returnType = static_cast<QMetaType::Type>(info.retType);
                returnValue = returnType == QMetaType::Void ?
                                         QVariant() : QVariant(returnType, NULL);
                if (returnType == QMetaType::QVariant)
                    parameters.append(&returnValue);
                else
                    parameters.append(returnValue.data());

                for (int i = 0; i < info.params.size(); ++i)
                {
                    const QJsonRpcServicePrivate::ParamInfo &parInfo(info.params.at(i));
                    QJsonValue val = namedParametersObject.value(parInfo.name);
                    QVariant arg = argumentConvert(val, parInfo);
                    if (!arg.isValid())
                    {
                        QString message;
                        if (!val.isUndefined())
                            message = QString("failed to construct default '%1' object").arg(parInfo.name);
                        else
                            message = QString("failed to convert '%1' object from JSon").arg(parInfo.name);
                        QJsonRpcMessage error =request.createErrorResponse(
                                    QJsonRpc::InvalidParams, message);
                        Q_EMIT result(error);
                        return false;
                    }
                    arguments.push_back(arg);
                    if (parInfo.type == QMetaType::QVariant)
                        parameters.append(static_cast<void *>(&arguments.last()));
                    else
                        parameters.append(const_cast<void *>(arguments.last().constData()));
                }
                break;
            }
        }
    }
    else {
        QJsonArray arrayParameters = params.toArray();
        foreach (int methodIndex, indexes) {
            QJsonRpcServicePrivate::MethodInfo &info = d->methods[methodIndex];
            if (jsParamCompare(arrayParameters, info)) {
                idx = methodIndex;
                arguments.reserve(info.params.size());
                returnType = static_cast<QMetaType::Type>(info.retType);
                returnValue = returnType == QMetaType::Void ?
                                         QVariant() : QVariant(returnType, NULL);
                if (returnType == QMetaType::QVariant)
                    parameters.append(&returnValue);
                else
                    parameters.append(returnValue.data());
                for (int i = 0; i < info.params.size(); ++i)
                {
                    const QJsonRpcServicePrivate::ParamInfo &parInfo(info.params.at(i));
                    QVariant arg = argumentConvert(arrayParameters.at(i),
                                                   parInfo);
                    if (!arg.isValid())
                    {
                        QString message;
                        if (arrayParameters.at(i).isUndefined())
                            message = QString("failed to construct default '%1' object").arg(parInfo.name);
                        else
                            message = QString("failed to convert '%1' object from JSon").arg(parInfo.name);
                        QJsonRpcMessage error =request.createErrorResponse(
                                    QJsonRpc::InvalidParams, message);
                        Q_EMIT result(error);
                        return false;
                    }
                    arguments.push_back(arg);
                    if (parInfo.type == QMetaType::QVariant)
                        parameters.append(static_cast<void *>(&arguments.last()));
                    else
                        parameters.append(const_cast<void *>(arguments.last().constData()));
                }
                break;
            }
        }
    }

    if (idx == -1) {
        QJsonRpcMessage error =
            request.createErrorResponse(QJsonRpc::InvalidParams, "invalid parameters");
        Q_EMIT result(error);
        return false;
    }

    QJsonRpcServicePrivate::MethodInfo &info = d->methods[idx];

    // first argument to metacall is the return value



    bool success =
        const_cast<QJsonRpcService*>(this)->qt_metacall(QMetaObject::InvokeMetaMethod, idx, parameters.data()) < 0;
    if (!success) {
        QString message = QString("dispatch for method '%1' failed").arg(method.constData());
        QJsonRpcMessage error =
            request.createErrorResponse(QJsonRpc::InvalidRequest, message);
        Q_EMIT result(error);
        return false;
    }
    else if (info.hasOut)
    {
        QJsonArray ret;
        if (info.retType != QMetaType::Void)
            ret.append(retConvert(returnValue));
        for (int i = 0; i < info.params.size(); ++i)
            if (info.params.at(i).out)
                ret.append(retConvert(arguments[i]));
        if (ret.size() > 1)
            Q_EMIT result(request.createResponse(ret));
        else
            Q_EMIT result(request.createResponse(ret.first()));
        return true;
    }
    else
    {
        Q_EMIT result(request.createResponse(retConvert(returnValue)));
        return true;
    }
}
