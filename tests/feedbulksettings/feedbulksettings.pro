QT = core sql testlib
CONFIG += console testcase c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = feedbulksettings-test
SOURCES += test_feedbulksettings.cpp ../../src/database/feedbulksettings.cpp
INCLUDEPATH += ../../src/database
