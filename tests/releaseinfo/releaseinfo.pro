QT = core testlib
CONFIG += console testcase c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = releaseinfo-test
SOURCES += test_releaseinfo.cpp ../../src/app/releaseinfo.cpp
INCLUDEPATH += ../../src
