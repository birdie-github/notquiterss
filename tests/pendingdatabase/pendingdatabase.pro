QT = core testlib
CONFIG += console testcase c++17 link_pkgconfig
CONFIG -= app_bundle
TEMPLATE = app
TARGET = pendingdatabase-test
PKGCONFIG += sqlite3
INCLUDEPATH += ../../src/database
SOURCES += test_pendingdatabase.cpp ../../src/database/pendingdatabase.cpp
