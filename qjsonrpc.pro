TEMPLATE = subdirs
SUBDIRS += src \
           tests
CONFIG += ordered

# Comment this to disable the http server classes
CONFIG += http_server

