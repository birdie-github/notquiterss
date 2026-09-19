QT = core testlib
CONFIG += console testcase c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = test_websiteoverrides
INCLUDEPATH += ../../src/network
SOURCES += test_websiteoverrides.cpp ../../src/network/websiteoverrides.cpp
HEADERS += ../../src/network/websiteoverrides.h
