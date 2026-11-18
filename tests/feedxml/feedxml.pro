QT = core xml testlib
greaterThan(QT_MAJOR_VERSION, 5): QT += core5compat
CONFIG += console testcase c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = feedxml-test
SOURCES += test_feedxml.cpp ../../src/feeds/update/feedxml.cpp
INCLUDEPATH += ../../src
