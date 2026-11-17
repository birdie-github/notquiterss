QT = core testlib sql
CONFIG += console testcase c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = feedhealth-test
INCLUDEPATH += ../../src
SOURCES += test_feedhealth.cpp ../../src/feeds/feedhealth.cpp
HEADERS += ../../src/feeds/feedhealth.h
