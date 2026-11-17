QT = core gui widgets testlib
CONFIG += console testcase c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = logging-test
PROJECT_ROOT = $$clean_path($$PWD/../..)
isEmpty(PYTHON): PYTHON = python3
!system($$shell_quote($$PYTHON) $$shell_quote($$PROJECT_ROOT/scripts/generate-project.py) --source $$shell_quote($$PROJECT_ROOT) --output $$shell_quote($$OUT_PWD/generated)): error("Metadata generation failed")
INCLUDEPATH += $$PROJECT_ROOT/src $$OUT_PWD/generated
SOURCES += test_logging.cpp ../../src/app/commandline.cpp ../../src/common/logfile.cpp
SOURCES += ../../src/platform/filemanager.cpp
