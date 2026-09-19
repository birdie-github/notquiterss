"""Generator regression checks; no Qt build or network required."""
import importlib.util
import json
from pathlib import Path
import plistlib
import shutil
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('metadata', ROOT / 'scripts/generate-project.py')
metadata = importlib.util.module_from_spec(spec)
spec.loader.exec_module(metadata)


class GeneratorTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.source = Path(self.temp.name) / 'source'
        self.output = Path(self.temp.name) / 'build'
        self.source.mkdir()
        for directory in ('packaging', 'resources'):
            shutil.copytree(ROOT / directory, self.source / directory)
        self.data = json.loads((ROOT / 'project.json').read_text())

    def generate(self):
        (self.source / 'project.json').write_text(json.dumps(self.data))
        metadata.generate(self.source, self.output)
        return json.loads((self.output / 'project-build.json').read_text())

    def test_identity_and_source_archive(self):
        self.data['identity']['name'] = 'ExampleRSS'
        values = self.generate()
        self.assertEqual(values['name'], 'ExampleRSS')
        self.assertEqual(values['executable'], 'examplerss')
        self.assertEqual(values['organization'], 'ExampleRSS')
        self.assertEqual(values['translationPrefix'], 'ExampleRSS_')
        self.assertEqual(values['revision'], '')
        plist = plistlib.loads((self.output / 'Info.plist').read_bytes())
        self.assertEqual(plist['CFBundleName'], 'ExampleRSS')
        self.assertEqual(plist['CFBundleExecutable'], 'examplerss')
        desktop = (self.output / 'ExampleRSS.desktop').read_text()
        self.assertIn('Exec=examplerss', desktop)
        self.assertIn('Icon=ExampleRSS', desktop)
        self.assertTrue((self.output / 'icons/48/ExampleRSS.png').exists())

    def test_repository_derivation_and_override(self):
        self.data['project']['repository'] = 'https://github.com/example/reader'
        values = self.generate()
        self.assertEqual(values['issuesUrl'], 'https://github.com/example/reader/issues')
        self.assertEqual(values['updateEndpoint'], 'https://api.github.com/repos/example/reader/releases/latest')
        self.data['project']['issues'] = 'https://example.org/bugs'
        self.assertEqual(self.generate()['issuesUrl'], 'https://example.org/bugs')

    def test_escaping_and_idempotence(self):
        self.data['project']['copyright'] = '© "Example" & Friends 😀'
        self.generate()
        ET.parse(self.output / 'appdata.xml')
        plist = plistlib.loads((self.output / 'Info.plist').read_bytes())
        self.assertIn(self.data['project']['copyright'], plist['NSHumanReadableCopyright'])
        header = (self.output / 'projectmetadata.h').read_text()
        self.assertIn(r'\U0001f600', header)
        self.assertIn(r'\u00a9', header)
        resource = (self.output / 'projectmetadata_rc.h').read_text()
        self.assertIn(r'\xd83d\xde00', resource)
        before = {p: (p.read_bytes(), p.stat().st_mtime_ns) for p in self.output.rglob('*') if p.is_file()}
        self.generate()
        self.assertEqual(before, {p: (p.read_bytes(), p.stat().st_mtime_ns) for p in before})

    def test_default_feeds(self):
        self.data['default_feeds'] = [
            {'enabled': True, 'title': 'First "feed"',
             'feed_url': 'https://example.org/rss/?a=1&b=2'},
            {'enabled': False, 'title': 'Disabled feed',
             'feed_url': 'https://disabled.example/rss'},
            {'enabled': True, 'title': 'Second feed',
             'feed_url': 'http://example.net/rss',
             'website_url': 'https://example.net/'}]
        self.generate()
        header = (self.output / 'projectmetadata.h').read_text()
        self.assertIn('https://example.org/rss/?a=1&b=2', header)
        self.assertIn('https://example.net/', header)
        self.assertNotIn('Disabled feed', header)
        self.assertNotIn('disabled.example', header)
        self.assertLess(header.index('example.org/rss'), header.index('example.net/rss'))
        for entries in ([], [dict(self.data['default_feeds'][0], enabled=False)]):
            self.data['default_feeds'] = entries
            self.generate()
            header = (self.output / 'projectmetadata.h').read_text()
            self.assertIn('defaultFeeds() { return {\n}; }', header)

    def test_invalid_default_feeds(self):
        valid = {'enabled': True, 'title': 'Feed', 'feed_url': 'https://example.org/rss'}
        for entries in ({}, [dict(valid, enabled='false')],
                        [dict(valid, feed_url='file:///tmp/feed')],
                        [dict(valid, website_url='https://user:pass@example.org/')],
                        [dict(valid, title='')], [dict(valid, unexpected=True)],
                        [valid, valid]):
            with self.subTest(entries=entries):
                self.data['default_feeds'] = entries
                with self.assertRaises(ValueError):
                    self.generate()

    def test_invalid_metadata(self):
        original = json.loads(json.dumps(self.data))
        for section, key, value in [('identity', 'name', '../bad'),
                                    ('release', 'version', '1.2.65536'),
                                    ('release', 'version', '1.2-beta'),
                                    ('release', 'date', '2026-02-30'),
                                    ('project', 'repository', 'http://github.com/a/b'),
                                    ('resources', 'sounds', 'a/b')]:
            with self.subTest(key=key, value=value):
                self.data = json.loads(json.dumps(original))
                self.data[section][key] = value
                with self.assertRaises(ValueError):
                    self.generate()


if __name__ == '__main__':
    unittest.main()
