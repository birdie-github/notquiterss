QT = core sql testlib
CONFIG += console testcase c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = feeddeletion-test
SOURCES += test_feeddeletion.cpp ../../src/database/feeddeletion.cpp
INCLUDEPATH += ../../src/database
