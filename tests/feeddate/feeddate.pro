QT = core testlib
CONFIG += console testcase c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = feeddate-test
SOURCES += test_feeddate.cpp ../../src/common/feeddate.cpp
INCLUDEPATH += ../../src/common ../../src
