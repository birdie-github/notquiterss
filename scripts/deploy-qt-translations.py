#!/usr/bin/env python3
"""Deploy Qt widget translations for the application catalogs in a package."""
import argparse
import json
from pathlib import Path
import shutil
import sys


def deploy(source, resources, qt_translations):
    metadata = json.loads((source / 'project.json').read_text(encoding='utf-8'))
    names = metadata['resources']
    application = resources / names['translations']
    destination = resources / names['qt_translations']
    if not application.is_dir():
        raise ValueError(f'Missing packaged application translations: {application}')
    if not qt_translations.is_dir():
        raise ValueError(f'Missing Qt translations directory: {qt_translations}')
    prefix = metadata['identity']['name'] + '_'
    available = {p.stem[len('qtbase_'):].lower(): p
                 for p in qt_translations.glob('qtbase_*.qm')}
    if not available:
        raise ValueError(f'No qtbase catalogs found in {qt_translations}')
    copied = set()
    for catalog in sorted(application.glob(prefix + '*.qm')):
        language = catalog.stem[len(prefix):].replace('-', '_')
        if language == 'en':
            continue
        fallback = language
        match = None
        while fallback:
            match = available.get(fallback.lower())
            if match:
                break
            fallback = fallback.rsplit('_', 1)[0] if '_' in fallback else ''
        if match is None:
            print(f'WARNING: no Qt widget translation for {language} in {qt_translations}',
                  file=sys.stderr)
            continue
        # Preserve the Qt filename so QTranslator uses its normal locale fallback.
        destination.mkdir(parents=True, exist_ok=True)
        target = destination / match.name
        if target not in copied:
            shutil.copyfile(match, target)
            if target.read_bytes() != match.read_bytes():
                raise ValueError(f'Qt translation verification failed: {target}')
            copied.add(target)
        print(f'Qt translation for {language}: {target.name}')
    return copied


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--resources', type=Path, required=True)
    parser.add_argument('--qt-translations', type=Path, required=True)
    args = parser.parse_args()
    try:
        deploy(args.source, args.resources, args.qt_translations)
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, f'Qt translation deployment: {error}\n')


if __name__ == '__main__':
    main()
