QT = core gui widgets sql testlib
CONFIG += console testcase c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = feedselectiontree-test
SOURCES += test_feedselectiontree.cpp ../../src/feedsview/feedselectiontree.cpp
INCLUDEPATH += ../../src/feedsview
