#DEFINES += SQLITEDRIVER_DEBUG
#DEFINES += SQLITEDRIVER_EXPORT

INCLUDEPATH += $$PWD \
               $$PWD/sqlitex

# SQLitex and the backup API must link to the same external SQLite library.
# PKG_CONFIG_PATH selects non-default installations (see INSTALL).
CONFIG += link_pkgconfig
!packagesExist(sqlite3) {
  error("SQLite development files are required. Install sqlite-devel / MSYS2 sqlite3 / Homebrew sqlite and set PKG_CONFIG_PATH to the directory containing sqlite3.pc. See INSTALL.")
}
!system($$pkgConfigExecutable() --atleast-version=3.36.0 sqlite3) {
  error("SQLite 3.36.0 or newer with the memdb VFS is required; check the sqlite3.pc selected by pkg-config.")
}
PKGCONFIG += sqlite3

HEADERS += $$PWD/sqlitex/sqlcachedresult.h \
           $$PWD/sqlitex/sqlitedriver.h \
           $$PWD/sqlitex/sqliteextension.h

SOURCES += $$PWD/sqlitex/sqlcachedresult.cpp \
           $$PWD/sqlitex/sqlitedriver.cpp \
           $$PWD/sqlitex/sqliteextension.cpp
