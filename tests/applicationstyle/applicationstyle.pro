QT = core gui testlib
CONFIG += console testcase c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = applicationstyle-test
SOURCES += test_applicationstyle.cpp ../../src/application/applicationstyle.cpp
INCLUDEPATH += ../../src/application
RESOURCES += ../../app.qrc
