# This GCC-specific flag is not supported by Clang's GCC-compatible toolchain.
gcc:!clang {
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

# Optional link-time optimization.
# Enable with: qmake CONFIG+=lto
contains(CONFIG, lto) {
    # Handle LTO per-compiler family
    clang {
        # ThinLTO is fast and recommended for Clang
        QMAKE_CXXFLAGS += -flto=thin
        QMAKE_LFLAGS   += -flto=thin
    } else:gcc {
        # Allow CI to supply a worker count where automatic detection fails.
        isEmpty(LTO_JOBS): LTO_JOBS = auto
        QMAKE_CXXFLAGS += -flto=$$LTO_JOBS
        QMAKE_LFLAGS   += -flto=$$LTO_JOBS
    } else:msvc {
        # MSVC or other compilers fallback
        CONFIG += ltcg
    }
}

# Generate project metadata in the build tree, including for source archives.
isEmpty(PYTHON): PYTHON = python3
PROJECT_GENERATED_DIR = $$OUT_PWD/generated
!system($$shell_quote($$PYTHON) $$shell_quote($$PWD/scripts/generate-project.py) --source $$shell_quote($$PWD) --output $$shell_quote($$PROJECT_GENERATED_DIR)): error("Project metadata generation failed")
include($$PROJECT_GENERATED_DIR/project.pri)
INCLUDEPATH += $$PROJECT_GENERATED_DIR
VERSION = $$PROJECT_VERSION
# qmake includes these inputs in its Makefile regeneration dependencies.
QMAKE_INTERNAL_INCLUDED_FILES += $$PWD/project.json $$PWD/scripts/generate-project.py $$files($$PWD/packaging/*.in) $$files($$PWD/$${PROJECT_RESOURCE_ROOT}/external/icons/*/notquiterss.png) $$PWD/$${PROJECT_RESOURCE_ROOT}/external/icons/application.ico
DISTFILES += $$PWD/project.json $$PWD/scripts/generate-project.py $$files($$PWD/packaging/*.in)

# Invoke the matching qmake; this option validates, rather than switches, its Qt.
isEmpty(USE_QT): USE_QT = 6
!equals(USE_QT, 5):!equals(USE_QT, 6): error("USE_QT must be 5 or 6")
!equals(QT_MAJOR_VERSION, $$USE_QT): error("Run qmake from the requested Qt installation. Qt 6 is the default. For Qt 5, pass USE_QT=5.")
equals(QT_MAJOR_VERSION, 5):lessThan(QT_MINOR_VERSION, 15): error("$$PROJECT_NAME requires Qt 5.15 or newer within Qt 5")
equals(QT_MAJOR_VERSION, 6):lessThan(QT_MINOR_VERSION, 2): error("$$PROJECT_NAME requires Qt 6.2 or newer within Qt 6")
QT += widgets network xml printsupport sql svg
unix:!mac:qtHaveModule(dbus) {
  QT += dbus
  DEFINES += HAVE_FILEMANAGER_DBUS
}
HEADERS += src/app/commandline.h
SOURCES += src/app/commandline.cpp
HEADERS += src/articles/feedreadstate.h
HEADERS += src/network/websiteoverrides.h
SOURCES += src/network/websiteoverrides.cpp
SOURCES += src/articles/feedreadstate.cpp
equals(QT_MAJOR_VERSION, 6): QT += core5compat

!contains(CONFIG, no_donate_button) {
  DEFINES += NQRSS_ENABLE_DONATE_BUTTON
  HEADERS += src/widgets/donatebutton.h
  SOURCES += src/widgets/donatebutton.cpp
}
CONFIG += c++17 link_pkgconfig
!packagesExist(libxml-2.0) {
  error("libxml2 development files and pkg-config are required. Set PKG_CONFIG_PATH to the directory containing libxml-2.0.pc. See INSTALL for Linux, MSYS2 and Homebrew setup.")
}
PKGCONFIG += libxml-2.0
HEADERS += src/preferences/applicationstyle.h
SOURCES += src/preferences/applicationstyle.cpp

HEADERS += src/app/releaseinfo.h
SOURCES += src/app/releaseinfo.cpp

HEADERS += src/preferences/languagecatalog.h
SOURCES += src/preferences/languagecatalog.cpp

HEADERS += src/network/credentialscope.h src/network/networkpolicy.h src/articles/rendering/articlecontent.h src/articles/rendering/articleimages.h src/articles/rendering/lanczosscaler.h src/sharing/shareservice.h src/articles/cleanup/newsretention.h src/mainwindow/statusbarcontroller.h src/mainwindow/trayiconcontroller.h src/platform/soundplayer.h
SOURCES += src/articles/rendering/articlecontent.cpp src/articles/rendering/articleimages.cpp src/articles/rendering/lanczosscaler.cpp src/sharing/shareservice.cpp src/mainwindow/statusbarcontroller.cpp src/mainwindow/trayiconcontroller.cpp src/platform/soundplayer.cpp

unix:!mac:DEFINES += HAVE_X11

TEMPLATE = app

HEADERS += \
    src/feeds/update/parseobject.h \
    src/preferences/optionsdialog.h \
    src/preferences/optionspagebuilder.h \
    src/preferences/optionssearchpanel.h \
    src/preferences/pages/backupsettingspage.h \
    src/preferences/pages/browsersettingspage.h \
    src/preferences/pages/feedssettingspage.h \
    src/preferences/pages/fontscolorssettingspage.h \
    src/preferences/pages/generalsettingspage.h \
    src/preferences/pages/labelssettingspage.h \
    src/preferences/pages/languagesettingspage.h \
    src/preferences/pages/networksettingspage.h \
    src/preferences/pages/notificationssettingspage.h \
    src/preferences/pages/notifierfeedsdialog.h \
    src/preferences/pages/passwordssettingspage.h \
    src/preferences/pages/shortcutssettingspage.h \
    src/preferences/pages/systemtraysettingspage.h \
    src/articles/newsview.h \
    src/articles/newsmodel.h \
    src/articles/newsheader.h \
    src/app/aboutdialog.h \
    src/app/updateappdialog.h \
    src/feeds/feedpropertiesdialog.h \
    src/feeds/feedselectiontree.h \
    src/feeds/feeddiscovery.h \
    src/feeds/addfeedwizard.h \
    src/articles/newstabwidget.h \
    src/articles/findtext.h \
    src/feeds/findfeed.h \
    src/feeds/feedsview.h \
    src/feeds/feedhealth.h \
    src/feeds/feedstatusdelegate.h \
    src/feeds/feedsmodel.h \
    src/feeds/addfolderdialog.h \
    src/articles/labeldialog.h \
    src/feeds/update/faviconobject.h \
    src/mainwindow/customizetoolbardialog.h \
    src/downloads/downloadmanager.h \
    src/downloads/downloaditem.h \
    src/articles/tabbar.h \
    src/articles/categoriestreewidget.h \
    src/articles/cleanup/cleanuppreviewdialog.h \
    src/articles/cleanup/cleanupwizard.h \
    src/feeds/update/updatefeeds.h \
    src/feeds/update/requestfeed.h \
    src/notifications/notificationsfeeditem.h \
    src/notifications/notificationsnewsitem.h \
    src/notifications/notificationswidget.h \
    src/app/mainapplication.h \
    src/preferences/settings.h \
    src/backup/databasebackup.h \
    src/platform/filemanager.h \
    src/common/logfile.h \
    src/mainwindow/mainwindow.h \
    src/feeds/opml/opmlexporter.h \
    src/preferences/shortcutregistry.h \
    src/app/splashscreen.h \
    src/network/authenticationdialog.h \
    src/network/cookiejar.h \
    src/network/cookiesdialog.h \
    src/network/httprequestqueue.h \
    src/network/networkmanager.h \
    src/articles/rendering/articleview.h \
    src/database/credentialstore.h \
    src/database/database.h \
    src/database/pendingdatabase.h \
    src/feeds/feedbulksettings.h \
    src/feeds/feedcounters.h \
    src/feeds/feeddeletion.h \
    src/common/common.h \
    src/feeds/update/feedxml.h \
    src/feeds/update/feeddate.h \
    src/widgets/delegatewithoutfocus.h \
    src/widgets/dialog.h \
    src/widgets/lineedit.h \
    src/widgets/reorderabletreecontroller.h \
    src/widgets/searchlineedit.h \
    src/widgets/toolbutton.h \
    src/articles/filters/filterrulesdialog.h \
    src/articles/filters/newsfiltersdialog.h \
    src/articles/filters/itemcondition.h \
    src/articles/filters/itemaction.h \
    src/network/networkmanagerproxy.h \
    src/feeds/feedsproxymodel.h \
    src/app/globals.h \

SOURCES += \
    src/feeds/update/parseobject.cpp \
    src/preferences/optionsdialog.cpp \
    src/preferences/optionspagebuilder.cpp \
    src/preferences/optionssearchpanel.cpp \
    src/preferences/pages/backupsettingspage.cpp \
    src/preferences/pages/browsersettingspage.cpp \
    src/preferences/pages/feedssettingspage.cpp \
    src/preferences/pages/fontscolorssettingspage.cpp \
    src/preferences/pages/generalsettingspage.cpp \
    src/preferences/pages/labelssettingspage.cpp \
    src/preferences/pages/languagesettingspage.cpp \
    src/preferences/pages/networksettingspage.cpp \
    src/preferences/pages/notificationssettingspage.cpp \
    src/preferences/pages/notifierfeedsdialog.cpp \
    src/preferences/pages/passwordssettingspage.cpp \
    src/preferences/pages/shortcutssettingspage.cpp \
    src/preferences/pages/systemtraysettingspage.cpp \
    src/articles/newsview.cpp \
    src/articles/newsmodel.cpp \
    src/articles/newsheader.cpp \
    src/app/aboutdialog.cpp \
    src/app/updateappdialog.cpp \
    src/feeds/feedpropertiesdialog.cpp \
    src/feeds/feedbulksettings.cpp \
    src/mainwindow/feedsettings.cpp \
    src/feeds/feeddiscovery.cpp \
    src/feeds/addfeedwizard.cpp \
    src/articles/newstabwidget.cpp \
    src/articles/findtext.cpp \
    src/feeds/findfeed.cpp \
    src/feeds/feedsview.cpp \
    src/feeds/feedselectiontree.cpp \
    src/feeds/feedhealth.cpp \
    src/feeds/feedstatusdelegate.cpp \
    src/feeds/feedsmodel.cpp \
    src/feeds/addfolderdialog.cpp \
    src/articles/labeldialog.cpp \
    src/feeds/update/faviconobject.cpp \
    src/mainwindow/customizetoolbardialog.cpp \
    src/downloads/downloadmanager.cpp \
    src/downloads/downloaditem.cpp \
    src/articles/tabbar.cpp \
    src/articles/categoriestreewidget.cpp \
    src/articles/cleanup/cleanuppreviewdialog.cpp \
    src/articles/cleanup/cleanupwizard.cpp \
    src/feeds/update/updatefeeds.cpp \
    src/feeds/update/requestfeed.cpp \
    src/notifications/notificationsfeeditem.cpp \
    src/notifications/notificationsnewsitem.cpp \
    src/notifications/notificationswidget.cpp \
    src/app/mainapplication.cpp \
    src/preferences/settings.cpp \
    src/backup/databasebackup.cpp \
    src/platform/filemanager.cpp \
    src/common/logfile.cpp \
    src/mainwindow/mainwindow.cpp \
    src/mainwindow/mainwindow_feeds.cpp \
    src/mainwindow/mainwindow_news.cpp \
    src/mainwindow/mainwindow_notifications.cpp \
    src/mainwindow/mainwindow_settings.cpp \
    src/mainwindow/mainwindow_theme.cpp \
    src/mainwindow/mainwindow_ui.cpp \
    src/feeds/opml/opmlexporter.cpp \
    src/preferences/shortcutregistry.cpp \
    src/app/globals.cpp \
    src/app/main.cpp \
    src/app/splashscreen.cpp \
    src/network/authenticationdialog.cpp \
    src/network/cookiejar.cpp \
    src/network/cookiesdialog.cpp \
    src/network/httprequestqueue.cpp \
    src/network/networkmanager.cpp \
    src/articles/rendering/articleview.cpp \
    src/database/credentialstore.cpp \
    src/database/database.cpp \
    src/feeds/feedcounters.cpp \
    src/feeds/feeddeletion.cpp \
    src/database/pendingdatabase.cpp \
    src/common/common.cpp \
    src/feeds/update/feedxml.cpp \
    src/feeds/update/feeddate.cpp \
    src/widgets/delegatewithoutfocus.cpp \
    src/widgets/dialog.cpp \
    src/widgets/lineedit.cpp \
    src/widgets/reorderabletreecontroller.cpp \
    src/widgets/searchlineedit.cpp \
    src/widgets/toolbutton.cpp \
    src/articles/filters/filterrulesdialog.cpp \
    src/articles/filters/newsfiltersdialog.cpp \
    src/articles/filters/itemcondition.cpp \
    src/articles/filters/itemaction.cpp \
    src/network/networkmanagerproxy.cpp \
    src/feeds/feedsproxymodel.cpp

INCLUDEPATH += $$PWD/src

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
    SUPPORTERS \
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
  icon_512.files = $$PROJECT_GENERATED_DIR/icons/512/$${PROJECT_NAME}.png
  icon_16.path =  $$quote($$PREFIX/share/icons/hicolor/16x16/apps)
  icon_32.path =  $$quote($$PREFIX/share/icons/hicolor/32x32/apps)
  icon_48.path =  $$quote($$PREFIX/share/icons/hicolor/48x48/apps)
  icon_64.path =  $$quote($$PREFIX/share/icons/hicolor/64x64/apps)
  icon_128.path = $$quote($$PREFIX/share/icons/hicolor/128x128/apps)
  icon_256.path = $$quote($$PREFIX/share/icons/hicolor/256x256/apps)
  icon_512.path = $$quote($$PREFIX/share/icons/hicolor/512x512/apps)


  metainfo.files = $$PROJECT_GENERATED_DIR/$${PROJECT_BUNDLE_ID}.metainfo.xml
  metainfo.path = $$PREFIX/share/metainfo
  INSTALLS += target desktop target1 metainfo
  INSTALLS += icon_16 icon_32 icon_48 icon_64 icon_128 icon_256 icon_512
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

HEADERS += src/network/feedurl.h src/feeds/opml/opmlinput.h
SOURCES += src/network/feedurl.cpp src/feeds/opml/opmlinput.cpp
