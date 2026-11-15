QT = core gui testlib
CONFIG += console testcase c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = applicationstyle-test
SOURCES += test_applicationstyle.cpp ../../src/application/applicationstyle.cpp
INCLUDEPATH += ../../src/application
isEmpty(PYTHON): PYTHON = python3
!system($$shell_quote($$PYTHON) $$shell_quote($$PWD/../../scripts/generate-themes.py) --source $$shell_quote($$PWD/../..) --output $$shell_quote($$OUT_PWD/themes)): error("Theme generation failed")
RESOURCES += ../../app.qrc $$OUT_PWD/themes/themes.qrc
