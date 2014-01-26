TEMPLATE = subdirs
SUBDIRS += json
SUBDIRS += qjsonrpcmessage \
           qjsonrpcsocket \
           qjsonrpcserver \
           qjsonrpcservice \
           qjsonrpchttpclient \
           qjsonrpc_custom_types \
           qjsonrpcservice_dispatch

http_server {
    SUBDIRS += qjsonrpchttpserver
}
