QT = core gui widgets network testlib
greaterThan(QT_MAJOR_VERSION, 5): QT += core5compat
CONFIG += console testcase c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = feedurl-test
SOURCES += test_feedurl.cpp ../../src/network/feedurl.cpp ../../src/feeds/opml/opmlinput.cpp
INCLUDEPATH += ../../src
