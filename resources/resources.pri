# One external resource stage, shared by Linux install, Windows and macOS bundles.
RESOURCE_SOURCE_DIR = $$PWD/external
PROJECT_SOURCE_DIR = $$clean_path($$PWD/..)
RESOURCE_STAGE_DIR = $$BUILD_DIR/$$PROJECT_RESOURCE_ROOT
RESOURCE_STAMP = $$BUILD_DIR/resources.stamp
RESOURCE_SCRIPT = $$PROJECT_SOURCE_DIR/scripts/stage-resources.py

TRANSLATIONS += $$files($$RESOURCE_SOURCE_DIR/$${PROJECT_LANG_DIR}/$${PROJECT_TRANSLATION_PREFIX}*.ts)
LANG_QM_FILES =
!mkpath($$BUILD_DIR/qm): error("Cannot create translation output directory")
!isEmpty(TRANSLATIONS) {
  qtPrepareTool(QMAKE_LRELEASE, lrelease)
  updateqm.input = TRANSLATIONS
  updateqm.output = $$BUILD_DIR/qm/${QMAKE_FILE_IN_BASE}.qm
  updateqm.commands = $$shell_quote($$QMAKE_LRELEASE) "${QMAKE_FILE_IN}" -qm "${QMAKE_FILE_OUT}"
  updateqm.CONFIG += no_link target_predeps
  QMAKE_EXTRA_COMPILERS += updateqm
  for(tsfile, TRANSLATIONS) {
    qmfile = $$basename(tsfile)
    qmfile ~= s/\.ts$/.qm/
    # Extra compiler targets are relative to OUT_PWD; extra-target dependencies
    # are emitted verbatim. Make must see the same path spelling on both sides.
    LANG_QM_FILES += $$relative_path($$BUILD_DIR/qm/$$qmfile, $$OUT_PWD)
  }
}

RESOURCE_COMMAND = $$shell_quote($$PYTHON) $$shell_quote($$RESOURCE_SCRIPT) --source $$shell_quote($$PROJECT_SOURCE_DIR) --build-dir $$shell_quote($$BUILD_DIR)
# Seed static directories at qmake time so install/bundle discovery sees them.
# The build target below strictly requires every generated translation.
!system($$RESOURCE_COMMAND --allow-missing): error("Resource staging failed")
runtime_stage.target = $$RESOURCE_STAMP
runtime_stage.depends = $$files($$RESOURCE_SOURCE_DIR/*, true) $$LANG_QM_FILES $$RESOURCE_SCRIPT $$PROJECT_SOURCE_DIR/project.json
runtime_stage.commands = $$RESOURCE_COMMAND --stamp $$shell_quote($$RESOURCE_STAMP)
QMAKE_EXTRA_TARGETS += runtime_stage
PRE_TARGETDEPS += $$RESOURCE_STAMP
DISTFILES += $$RESOURCE_SCRIPT $$files($$PWD/*, true)

runtime_resources.files = $$files($$RESOURCE_STAGE_DIR/*)
unix:!mac {
  runtime_resources.path = $$quote($$DATA_DIR)
  INSTALLS += runtime_resources
}
win32 {
  runtime_resources.path = $$quote($$DESTDIR/$$PROJECT_RESOURCE_ROOT)
  INSTALLS += runtime_resources
}
mac {
  runtime_resources.path = Contents/Resources
  QMAKE_BUNDLE_DATA += runtime_resources
}
