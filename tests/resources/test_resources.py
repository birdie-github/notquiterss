"""Resource staging regression checks; no Qt compilation or application launch."""
import importlib.util
import json
from pathlib import Path
import shutil
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('stage', ROOT / 'scripts/stage-resources.py')
stage = importlib.util.module_from_spec(spec)
spec.loader.exec_module(stage)


class ResourceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.source = Path(self.temp.name) / 'source'
        self.build = Path(self.temp.name) / 'build'
        self.source.mkdir()
        shutil.copy(ROOT / 'project.json', self.source)
        shutil.copytree(ROOT / 'resources', self.source / 'resources')

    def test_external_resources_and_deployment(self):
        root = self.source / 'resources'
        (root / 'styles/extra').mkdir()
        (root / 'styles/extra/decoration.png').write_bytes(b'extra image')
        (root / 'styles/added.qss').write_text('QWidget {color: red;}')
        output = stage.stage(self.source, self.build)
        self.assertTrue((output / 'sounds/notification.wav').is_file())
        self.assertTrue((output / 'social-networks/configuration.ini').is_file())
        self.assertTrue((output / 'styles/extra/decoration.png').is_file())
        self.assertFalse((output / 'images').exists())
        self.assertFalse((output / 'html').exists())
        self.assertFalse(list(output.rglob('*.md')))
        installed = Path(self.temp.name) / 'installed'
        shutil.copytree(output, installed / 'resources')
        stage.verify(self.source, self.build, installed)
        (installed / 'resources/styles/added.qss').unlink()
        with self.assertRaises(ValueError):
            stage.verify(self.source, self.build, installed)
        (root / 'styles/added.qss').unlink()
        stage.stage(self.source, self.build)
        self.assertFalse((output / 'styles/added.qss').exists())

    def test_translation_compilation_contract(self):
        translations = self.source / 'resources/translations'
        (translations / 'NotQuiteRSS_xx.qm').write_bytes(b'prebuilt')
        (translations / 'NotQuiteRSS_yy.qm').write_bytes(b'old prebuilt')
        (translations / 'NotQuiteRSS_yy.ts').write_text('<TS/>')
        output = stage.stage(self.source, self.build, allow_missing=True)
        self.assertEqual((output / 'translations/NotQuiteRSS_xx.qm').read_bytes(), b'prebuilt')
        self.assertFalse((output / 'translations/NotQuiteRSS_yy.qm').exists())
        with self.assertRaises(ValueError):
            stage.stage(self.source, self.build)
        (self.build / 'qm').mkdir()
        (self.build / 'qm/NotQuiteRSS_yy.qm').write_bytes(b'compiled fixture, not a real QM')
        stage.stage(self.source, self.build)
        self.assertEqual((output / 'translations/NotQuiteRSS_yy.qm').read_bytes(), b'compiled fixture, not a real QM')
        self.assertFalse(list(output.rglob('*.ts')))

    def test_source_guard(self):
        before = (self.source / 'resources/images/logo.png').read_bytes()
        with self.assertRaises(ValueError):
            stage.stage(self.source, self.source)
        self.assertEqual((self.source / 'resources/images/logo.png').read_bytes(), before)

    def test_qrc_paths_and_aliases(self):
        seen = set()
        for section in ET.parse(ROOT / 'app.qrc').getroot():
            for file in section:
                self.assertTrue((ROOT / file.text).is_file(), file.text)
                alias = section.get('prefix') + '/' + file.get('alias', file.text)
                self.assertNotIn(alias, seen)
                seen.add(alias)
                if file.text.startswith('resources/'):
                    self.assertIsNotNone(file.get('alias'))
        self.assertIn('/images/images/logo.png', seen)
        self.assertIn('/html/description', seen)
        self.assertIn('/style/automaticStyle', seen)


if __name__ == '__main__':
    unittest.main()
