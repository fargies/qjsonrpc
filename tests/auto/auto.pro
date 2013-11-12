DEPTH = ../..
include($${DEPTH}/qjsonrpc.pri)

TEMPLATE = subdirs
SUBDIRS += json
SUBDIRS += qjsonrpcmessage \
           qjsonrpcsocket \
           qjsonrpcserver \
           qjsonrpcservice \
           qjsonrpchttpclient

http_server {
    SUBDIRS += qjsonrpchttpserver
}
