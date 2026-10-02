#!/usr/bin/env python3
"""Packaging checks without building or launching Qt."""
import contextlib
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest

SOURCE = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('deploy_qt', SOURCE / 'scripts/deploy-qt-translations.py')
deploy_qt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(deploy_qt)


class QtTranslationsTest(unittest.TestCase):
    def test_matching_fallback_and_missing_language(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            resources, qt = root / 'resources', root / 'qt'
            application = resources / 'translations'
            application.mkdir(parents=True)
            qt.mkdir()
            for locale in ('ru', 'ru_RU', 'pt_BR', 'en', 'zz'):
                (application / f'NotQuiteRSS_{locale}.qm').write_bytes(b'app')
            # An unrelated Qt catalog in the app directory must not select a language.
            (application / 'qtbase_de.qm').write_bytes(b'unrelated')
            for locale in ('ru', 'pt', 'pt_BR', 'de'):
                (qt / f'qtbase_{locale}.qm').write_bytes(locale.encode())
            errors = io.StringIO()
            with contextlib.redirect_stderr(errors), contextlib.redirect_stdout(io.StringIO()):
                copied = deploy_qt.deploy(SOURCE, resources, qt)
            self.assertEqual({p.name for p in copied}, {'qtbase_ru.qm', 'qtbase_pt_BR.qm'})
            self.assertEqual((resources / 'qt-translations/qtbase_pt_BR.qm').read_bytes(), b'pt_BR')
            self.assertIn('no Qt widget translation for zz', errors.getvalue())
            self.assertNotIn('for en ', errors.getvalue())

    def test_missing_inputs_fail(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaisesRegex(ValueError, 'application translations'):
                deploy_qt.deploy(SOURCE, root, root / 'qt')
            (root / 'translations').mkdir()
            with self.assertRaisesRegex(ValueError, 'Qt translations directory'):
                deploy_qt.deploy(SOURCE, root, root / 'qt')
            (root / 'qt').mkdir()
            with self.assertRaisesRegex(ValueError, 'No qtbase catalogs'):
                deploy_qt.deploy(SOURCE, root, root / 'qt')


if __name__ == '__main__':
    unittest.main()
