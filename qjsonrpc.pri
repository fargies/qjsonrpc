QJSONRPC_VERSION = 1.0.99

# Comment this to disable the http server classes
CONFIG += http_server

isEmpty(QJSONRPC_LIBRARY_TYPE) {
    QJSONRPC_LIBRARY_TYPE = shared
}

QT += network
QJSONRPC_INCLUDEPATH = $${PWD}/src
QJSONRPC_LIBS = -lqjsonrpc
contains(QJSONRPC_LIBRARY_TYPE, staticlib) {
    DEFINES += QJSONRPC_STATIC
} else {
    DEFINES += QJSONRPC_SHARED
    win32:QJSONRPC_LIBS = -lqjsonrpc1
}

isEmpty(PREFIX) {
    unix {
        PREFIX = /usr
    } else {
        PREFIX = $$[QT_INSTALL_PREFIX]
    }
}
isEmpty(LIBDIR) {
    LIBDIR = lib
}

