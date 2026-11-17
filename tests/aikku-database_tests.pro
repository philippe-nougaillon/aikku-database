QT += core gui qml quick quickcontrols2 sql testlib dbus
CONFIG += c++17 testcase
TARGET = aikku-database_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += \
    ../src/backend.h \
    ../src/filepicker.h \
    ../src/theme.h \
    ../src/translator.h

SOURCES += \
    aikku-database_tests.cpp \
    ../src/backend.cpp \
    ../src/theme.cpp \
    ../src/translator.cpp

RESOURCES += ../src/resources.qrc
