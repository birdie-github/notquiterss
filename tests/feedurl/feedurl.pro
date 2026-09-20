QT = core gui widgets testlib
greaterThan(QT_MAJOR_VERSION, 5): QT += core5compat
CONFIG += console testcase c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = feedurl-test
SOURCES += test_feedurl.cpp ../../src/network/feedurl.cpp ../../src/application/opmlinput.cpp
INCLUDEPATH += ../../src/network ../../src/application
