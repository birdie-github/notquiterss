QT = core sql testlib
CONFIG += console testcase c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = sqlownership-test
include(../../3rdparty/sqlite.pri)
SOURCES += test_sqlownership.cpp
