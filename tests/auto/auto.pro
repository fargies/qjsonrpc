TEMPLATE = subdirs
SUBDIRS += json
SUBDIRS += qjsonrpcmessage \
           qjsonrpcsocket \
           qjsonrpcserver \
           qjsonrpcservice \
           qjsonrpchttpclient \
           qjsonrpc_custom_types

http_server {
    SUBDIRS += qjsonrpchttpserver
}
