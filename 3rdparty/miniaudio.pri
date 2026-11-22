# External header-only dependency. Preparation is explicit, never run by qmake.
isEmpty(MINIAUDIO_INCLUDE_DIR): MINIAUDIO_INCLUDE_DIR = $$(MINIAUDIO_INCLUDE_DIR)
isEmpty(MINIAUDIO_INCLUDE_DIR): error("Set MINIAUDIO_INCLUDE_DIR to the directory containing compatible miniaudio headers. See scripts/prepare-miniaudio.py and INSTALL.")
!exists($$MINIAUDIO_INCLUDE_DIR/miniaudio.h): error("miniaudio.h was not found in MINIAUDIO_INCLUDE_DIR")
INCLUDEPATH += $$quote($$MINIAUDIO_INCLUDE_DIR)

unix:!mac: LIBS += -lpthread -lm
linux*: LIBS += -ldl
linux*:contains(QT_ARCH, arm): LIBS += -latomic
mac: LIBS += -framework CoreFoundation -framework CoreAudio -framework AudioToolbox
