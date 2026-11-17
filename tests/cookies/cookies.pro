QT = core network testlib
CONFIG += console testcase c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = cookies-test
INCLUDEPATH += ../../src
SOURCES += test_cookies.cpp ../../src/network/cookiejar.cpp
HEADERS += ../../src/network/cookiejar.h
