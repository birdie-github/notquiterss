QT = core xml testlib
CONFIG += console testcase c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = feedxml-test
SOURCES += test_feedxml.cpp ../../src/feeds/update/feedxml.cpp
INCLUDEPATH += ../../src
