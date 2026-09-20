"""Exercise the real qmake -> lrelease -> resource-stage dependency chain."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def check(qmake, make):
    with tempfile.TemporaryDirectory() as temp:
        source = Path(temp) / 'source'
        build = Path(temp) / 'build'
        source.mkdir()
        build.mkdir()
        shutil.copy(ROOT / 'project.json', source)
        shutil.copytree(ROOT / 'resources', source / 'resources')
        (source / 'scripts').mkdir()
        shutil.copy(ROOT / 'scripts/stage-resources.py', source / 'scripts')
        metadata = json.loads((source / 'project.json').read_text())
        names = metadata['resources']
        prefix = metadata['identity']['name'] + '_'
        translations = source / names['root'] / 'external' / names['translations']
        for pattern in ('*.ts', '*.qm'):
            for catalog in translations.glob(pattern):
                catalog.unlink()
        # Always exercise a real catalog, even if release sources have none.
        (translations / (prefix + 'en.ts')).write_text(
            '<?xml version="1.0" encoding="utf-8"?>\n'
            '<TS version="2.1" language="en"><context><name>Fixture</name>'
            '<message><source>Ready</source><translation>Ready</translation>'
            '</message></context></TS>\n', encoding='utf-8')
        project = source / 'translations.pro'
        project.write_text('\n'.join([
            'TEMPLATE = aux',
            'CONFIG -= qt debug_and_release',
            'load(qt_functions)',
            f'PYTHON = "{Path(sys.executable).as_posix()}"',
            f'PROJECT_RESOURCE_ROOT = {names["root"]}',
            f'PROJECT_LANG_DIR = {names["translations"]}',
            f'PROJECT_TRANSLATION_PREFIX = {prefix}',
            'BUILD_DIR = $$OUT_PWD/release',
            'DATA_DIR = $$OUT_PWD/install',
            'include(resources/resources.pri)',
        ]) + '\n', encoding='utf-8')
        subprocess.run([qmake, str(project)], cwd=build, check=True)
        # Request the stamp directly: building all compilers first can hide a
        # broken edge between an absolute dependency and a relative QM target.
        stamp = (build / 'release/resources.stamp').as_posix()
        qm = build / 'release/qm' / (prefix + 'en.qm')
        staged = build / 'release' / names['root'] / names['translations'] / qm.name
        for attempt in range(2):
            if attempt:
                qm.unlink()  # A missing generated catalog must be rebuilt too.
            subprocess.run([make, '-j2', stamp], cwd=build, check=True)
            if not qm.is_file() or not qm.stat().st_size:
                raise AssertionError('lrelease did not generate a QM catalog')
            if staged.read_bytes() != qm.read_bytes():
                raise AssertionError('Generated translation was not staged')
        if list((build / 'release' / names['root']).rglob('*.ts')):
            raise AssertionError('Translation sources leaked into runtime resources')
        print('Translation generation, staging and missing-output rebuild passed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qmake', required=True)
    parser.add_argument('--make', default='make')
    args = parser.parse_args()
    check(args.qmake, args.make)
