# Silence GCC warnings
gcc {
    GCC_VERSION = $$system($$QMAKE_CXX -dumpfullversion -dumpversion)
    GCC_MAJOR = $$section(GCC_VERSION, ., 0, 0)

    greaterThan(GCC_MAJOR, 15) {
        QMAKE_CXXFLAGS += -Wno-sfinae-incomplete
    }
}

# Apply architecture optimization safely
contains(QT_ARCH, x86_64) {
    QMAKE_CXXFLAGS += -march=x86-64-v2
}

# Handle LTO per-compiler family
clang {
    # ThinLTO is fast and recommended for Clang
    QMAKE_CXXFLAGS += -flto=thin
    QMAKE_LFLAGS   += -flto=thin
} else:gcc {
    # GCC supports auto thread detection
    QMAKE_CXXFLAGS += -flto=auto
    QMAKE_LFLAGS   += -flto=auto
} else {
    # MSVC or other compilers fallback
    CONFIG += ltcg
}

# Generate project metadata in the build tree, including for source archives.
isEmpty(PYTHON): PYTHON = python3
PROJECT_GENERATED_DIR = $$OUT_PWD/generated
!system($$shell_quote($$PYTHON) $$shell_quote($$PWD/scripts/generate-project.py) --source $$shell_quote($$PWD) --output $$shell_quote($$PROJECT_GENERATED_DIR)): error("Project metadata generation failed")
include($$PROJECT_GENERATED_DIR/project.pri)
INCLUDEPATH += $$PROJECT_GENERATED_DIR
VERSION = $$PROJECT_VERSION
# qmake includes these inputs in its Makefile regeneration dependencies.
QMAKE_INTERNAL_INCLUDED_FILES += $$PWD/project.json $$PWD/scripts/generate-project.py $$files($$PWD/packaging/*.in) $$files($$PWD/$${PROJECT_RESOURCE_ROOT}/external/icons/*/quiterss.png) $$PWD/$${PROJECT_RESOURCE_ROOT}/external/icons/application.ico
DISTFILES += $$PWD/project.json $$PWD/scripts/generate-project.py $$files($$PWD/packaging/*.in)

# Invoke the matching qmake; this option validates, rather than switches, its Qt.
isEmpty(USE_QT): USE_QT = 5
!equals(USE_QT, 5):!equals(USE_QT, 6): error("USE_QT must be 5 or 6")
!equals(QT_MAJOR_VERSION, $$USE_QT): error("Run qmake from the requested Qt installation. Qt 6 requires USE_QT=6; Qt 5 is the default.")
equals(QT_MAJOR_VERSION, 5):lessThan(QT_MINOR_VERSION, 15): error("$$PROJECT_NAME requires Qt 5.15 or newer within Qt 5")
equals(QT_MAJOR_VERSION, 6):lessThan(QT_MINOR_VERSION, 2): error("$$PROJECT_NAME requires Qt 6.2 or newer within Qt 6")
QT += widgets network xml printsupport sql
unix:!mac:qtHaveModule(dbus) {
  QT += dbus
  DEFINES += HAVE_FILEMANAGER_DBUS
}
HEADERS += src/application/commandline.h
SOURCES += src/application/commandline.cpp
HEADERS += src/feedreadstate.h
HEADERS += src/network/websiteoverrides.h
SOURCES += src/network/websiteoverrides.cpp
SOURCES += src/feedreadstate.cpp
equals(QT_MAJOR_VERSION, 6): QT += core5compat
CONFIG += c++17 link_pkgconfig
!packagesExist(libxml-2.0) {
  error("libxml2 development files and pkg-config are required. Set PKG_CONFIG_PATH to the directory containing libxml-2.0.pc. See INSTALL for Linux, MSYS2 and Homebrew setup.")
}
PKGCONFIG += libxml-2.0
HEADERS += src/application/applicationstyle.h
SOURCES += src/application/applicationstyle.cpp

HEADERS += src/application/releaseinfo.h
SOURCES += src/application/releaseinfo.cpp

HEADERS += src/application/languagecatalog.h
SOURCES += src/application/languagecatalog.cpp

HEADERS += src/network/networkpolicy.h src/articleview/articlecontent.h src/articleview/articleimages.h src/sharing/shareservice.h src/newsretention.h src/application/statusbarcontroller.h src/application/trayiconcontroller.h src/application/soundplayer.h
SOURCES += src/articleview/articlecontent.cpp src/articleview/articleimages.cpp src/sharing/shareservice.cpp src/application/statusbarcontroller.cpp src/application/trayiconcontroller.cpp src/application/soundplayer.cpp

unix:!mac:DEFINES += HAVE_X11

TEMPLATE = app

HEADERS += \
    src/parseobject.h \
    src/optionsdialog.h \
    src/newsview/newsview.h \
    src/newsview/newsmodel.h \
    src/newsview/newsheader.h \
    src/aboutdialog.h \
    src/updateappdialog.h \
    src/feedpropertiesdialog.h \
    src/feedsview/feedselectiontree.h \
    src/addfeedwizard.h \
    src/newstabwidget.h \
    src/findtext.h \
    src/findfeed.h \
    src/feedsview/feedsview.h \
    src/feedsview/feedhealth.h \
    src/feedsview/feedstatusdelegate.h \
    src/feedsview/feedsmodel.h \
    src/addfolderdialog.h \
    src/labeldialog.h \
    src/faviconobject.h \
    src/customizetoolbardialog.h \
    src/downloads/downloadmanager.h \
    src/downloads/downloaditem.h \
    src/tabbar.h \
    src/categoriestreewidget.h \
    src/cleanupwizard.h \
    src/updatefeeds.h \
    src/requestfeed.h \
    src/notifications/notificationsfeeditem.h \
    src/notifications/notificationsnewsitem.h \
    src/notifications/notificationswidget.h \
    src/application/mainapplication.h \
    src/application/settings.h \
    src/application/databasebackup.h \
    src/application/backupsettingspage.h \
    src/application/filemanager.h \
    src/application/logfile.h \
    src/application/mainwindow.h \
    src/application/opmlexporter.h \
    src/application/shortcutregistry.h \
    src/application/splashscreen.h \
    src/network/authenticationdialog.h \
    src/network/cookiejar.h \
    src/network/networkmanager.h \
    src/articleview/articleview.h \
    src/database/database.h \
    src/database/feedbulksettings.h \
    src/common/common.h \
    src/common/delegatewithoutfocus.h \
    src/common/dialog.h \
    src/common/lineedit.h \
    src/common/toolbutton.h \
    src/newsfilters/filterrulesdialog.h \
    src/newsfilters/newsfiltersdialog.h \
    src/newsfilters/itemcondition.h \
    src/newsfilters/itemaction.h \
    src/network/networkmanagerproxy.h \
    src/feedsview/feedsproxymodel.h \
    src/main/globals.h \

SOURCES += \
    src/parseobject.cpp \
    src/optionsdialog.cpp \
    src/newsview/newsview.cpp \
    src/newsview/newsmodel.cpp \
    src/newsview/newsheader.cpp \
    src/aboutdialog.cpp \
    src/updateappdialog.cpp \
    src/feedpropertiesdialog.cpp \
    src/database/feedbulksettings.cpp \
    src/application/feedsettings.cpp \
    src/addfeedwizard.cpp \
    src/newstabwidget.cpp \
    src/findtext.cpp \
    src/findfeed.cpp \
    src/feedsview/feedsview.cpp \
    src/feedsview/feedselectiontree.cpp \
    src/feedsview/feedhealth.cpp \
    src/feedsview/feedstatusdelegate.cpp \
    src/feedsview/feedsmodel.cpp \
    src/addfolderdialog.cpp \
    src/labeldialog.cpp \
    src/faviconobject.cpp \
    src/customizetoolbardialog.cpp \
    src/downloads/downloadmanager.cpp \
    src/downloads/downloaditem.cpp \
    src/tabbar.cpp \
    src/categoriestreewidget.cpp \
    src/cleanupwizard.cpp \
    src/updatefeeds.cpp \
    src/requestfeed.cpp \
    src/notifications/notificationsfeeditem.cpp \
    src/notifications/notificationsnewsitem.cpp \
    src/notifications/notificationswidget.cpp \
    src/application/mainapplication.cpp \
    src/application/settings.cpp \
    src/application/databasebackup.cpp \
    src/application/backupsettingspage.cpp \
    src/application/filemanager.cpp \
    src/application/logfile.cpp \
    src/application/mainwindow.cpp \
    src/application/opmlexporter.cpp \
    src/application/shortcutregistry.cpp \
    src/main/globals.cpp \
    src/main/main.cpp \
    src/application/splashscreen.cpp \
    src/network/authenticationdialog.cpp \
    src/network/cookiejar.cpp \
    src/network/networkmanager.cpp \
    src/articleview/articleview.cpp \
    src/database/database.cpp \
    src/common/common.cpp \
    src/common/delegatewithoutfocus.cpp \
    src/common/dialog.cpp \
    src/common/lineedit.cpp \
    src/common/toolbutton.cpp \
    src/newsfilters/filterrulesdialog.cpp \
    src/newsfilters/newsfiltersdialog.cpp \
    src/newsfilters/itemcondition.cpp \
    src/newsfilters/itemaction.cpp \
    src/network/networkmanagerproxy.cpp \
    src/feedsview/feedsproxymodel.cpp

INCLUDEPATH +=  $$PWD/src \
                $$PWD/src/application \
                $$PWD/src/common \
                $$PWD/src/main \
                $$PWD/src/database \
                $$PWD/src/downloads \
                $$PWD/src/feedsview \
                $$PWD/src/newsfilters \
                $$PWD/src/newsview \
                $$PWD/src/notifications \
                $$PWD/src/network \
                $$PWD/src/articleview \
                $$PWD/src/sharing \

# Use the toolchain/command-line choice of single or dual configuration.
CONFIG(debug, debug|release) {
  BUILD_DIR = $$OUT_PWD/debug
} else {
  BUILD_DIR = $$OUT_PWD/release
#  DEFINES += QT_NO_DEBUG_OUTPUT
}

DESTDIR = $${BUILD_DIR}/target
OBJECTS_DIR = $${BUILD_DIR}/obj
MOC_DIR = $${BUILD_DIR}/moc
RCC_DIR = $${BUILD_DIR}/rcc

# Require QtSingleApplication built for the selected Qt and its qmake feature.
!load(qtsingleapplication, true) {
  error("QtSingleApplication built with the selected Qt is required, including qtsingleapplication.prf. See INSTALL for the preparation helper and QMAKEFEATURES setup.")
}
include(3rdparty/sqlite.pri)
include(3rdparty/miniaudio.pri)

win32|mac {
  TARGET = $$PROJECT_EXECUTABLE
}

win32 {
  RC_FILE = $$PROJECT_GENERATED_DIR/application.rc
  RC_INCLUDEPATH += $$PROJECT_GENERATED_DIR
}

win32-g++ {
  LIBS += -lkernel32 -lpsapi -lshell32 -luser32
}

win32-msvc* {
  LIBS += -lpsapi
  LIBS += -lShell32
  LIBS += -lUser32

  QMAKE_CXXFLAGS += -D__PRETTY_FUNCTION__=__FUNCTION__
  QMAKE_CFLAGS += -D__PRETTY_FUNCTION__=__FUNCTION__
}

DISTFILES += \
    COPYING \
    AUTHORS \
    CHANGELOG \
    README.md

unix:!mac {
  TARGET = $$PROJECT_EXECUTABLE

  isEmpty(PREFIX) {
    PREFIX =   /usr/local
  }
  DATA_DIR = $$PREFIX/share/$$PROJECT_NAME
  DEFINES += RESOURCES_DIR='\\\"$${DATA_DIR}\\\"'

  target.path =  $$quote($$PREFIX/bin)

  desktop.files = $$PROJECT_GENERATED_DIR/$${PROJECT_NAME}.desktop
  desktop.path =  $$quote($$PREFIX/share/applications)

  target1.files = $$PROJECT_GENERATED_DIR/icons/48/$${PROJECT_NAME}.png
  target1.path =  $$quote($$PREFIX/share/pixmaps)

  icon_16.files =  $$PROJECT_GENERATED_DIR/icons/16/$${PROJECT_NAME}.png
  icon_32.files =  $$PROJECT_GENERATED_DIR/icons/32/$${PROJECT_NAME}.png
  icon_48.files =  $$PROJECT_GENERATED_DIR/icons/48/$${PROJECT_NAME}.png
  icon_64.files =  $$PROJECT_GENERATED_DIR/icons/64/$${PROJECT_NAME}.png
  icon_128.files = $$PROJECT_GENERATED_DIR/icons/128/$${PROJECT_NAME}.png
  icon_256.files = $$PROJECT_GENERATED_DIR/icons/256/$${PROJECT_NAME}.png
  icon_16.path =  $$quote($$PREFIX/share/icons/hicolor/16x16/apps)
  icon_32.path =  $$quote($$PREFIX/share/icons/hicolor/32x32/apps)
  icon_48.path =  $$quote($$PREFIX/share/icons/hicolor/48x48/apps)
  icon_64.path =  $$quote($$PREFIX/share/icons/hicolor/64x64/apps)
  icon_128.path = $$quote($$PREFIX/share/icons/hicolor/128x128/apps)
  icon_256.path = $$quote($$PREFIX/share/icons/hicolor/256x256/apps)


  metainfo.files = $$PROJECT_GENERATED_DIR/$${PROJECT_BUNDLE_ID}.metainfo.xml
  metainfo.path = $$PREFIX/share/metainfo
  INSTALLS += target desktop target1 metainfo
  INSTALLS += icon_16 icon_32 icon_48 icon_64 icon_128 icon_256
}


mac {
  CONFIG += app_bundle
  QMAKE_APPLICATION_BUNDLE_NAME = $$PROJECT_NAME

  QMAKE_INFO_PLIST = $$PROJECT_GENERATED_DIR/Info.plist
  ICON = $$PWD/$${PROJECT_RESOURCE_ROOT}/external/icons/application.icns

  bundle_target.files += AUTHORS
  bundle_target.files += COPYING
  bundle_target.files += CHANGELOG
  bundle_target.files += README.md
  bundle_target.path = Contents/Resources
  QMAKE_BUNDLE_DATA += bundle_target


  INSTALLS += bundle_target
}

include($$PWD/$${PROJECT_RESOURCE_ROOT}/resources.pri)

RESOURCES += \
    app.qrc

OTHER_FILES += \
    COPYING \
    AUTHORS \
    CHANGELOG \
    INSTALL \
    $$PWD/$${PROJECT_RESOURCE_ROOT}/$${PROJECT_SHARING_ICONS}/$${PROJECT_SHARING_CONFIG}

HEADERS += src/network/feedurl.h src/application/opmlinput.h
SOURCES += src/network/feedurl.cpp src/application/opmlinput.cpp
