TEMPLATE = app
CONFIG += console
CONFIG -= app_bundle
CONFIG -= qt
CONFIG += c++11

SOURCES += main.cpp \
           linuxtrack.c

HEADERS += linuxtrack.h

LIBS += -framework IOKit

include(deployment.pri)
qtcAddDeployment()

