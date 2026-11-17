QT = core testlib
CONFIG += console testcase c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = feeddate-test
SOURCES += test_feeddate.cpp ../../src/feeds/update/feeddate.cpp
INCLUDEPATH += ../../src
