#!/usr/bin/env python3
"""Stage external application data once for every platform; never compile resources."""
import argparse
import json
from pathlib import Path
import shutil


def resource_files(source, build, allow_missing=False):
    metadata = json.loads((source / 'project.json').read_text(encoding='utf-8'))
    names = metadata['resources']
    root = source / names['root'] / 'external'
    files = {}
    files[Path('overrides.ini.sample')] = root / 'overrides.ini.sample'
    # Only this subtree is installed. Keep existing runtime directory names;
    # source themes are still discovered in the installed styles directory.
    for directory in sorted(root.iterdir()):
        if not directory.is_dir() or directory.name == names['translations']:
            continue
        for path in sorted(directory.rglob('*')):
            if path.is_file() and path.suffix.lower() not in ('.md', '.pri', '.bat'):
                relative = path.relative_to(directory)
                destination = names['styles'] if directory.name == 'themes' else directory.name
                files[Path(destination) / relative] = path
    translations = root / names['translations']
    files[Path(names['translations']) / 'languages.ini'] = translations / 'languages.ini'
    for path in translations.glob('*.qm'):
        files[Path(names['translations']) / path.name] = path
    # Generated catalogs override a prebuilt file with the same name.
    for ts in translations.glob(metadata['identity']['name'] + '_*.ts'):
        qm = build / 'qm' / (ts.stem + '.qm')
        relative = Path(names['translations']) / qm.name
        if qm.is_file():
            files[relative] = qm
        elif not allow_missing:
            raise ValueError(f'Missing compiled translation: {qm}; build translations first')
        else:
            files.pop(relative, None)
    for relative, path in files.items():
        if not path.is_file():
            raise ValueError(f'Missing resource: {path}')
        if path.is_symlink():
            raise ValueError(f'Resource must be a regular file: {path}')
    return names, files


def stage(source, build, allow_missing=False):
    names = json.loads((source / 'project.json').read_text(encoding='utf-8'))['resources']
    source_root = (source / names['root']).resolve()
    destination = (build / names['root']).resolve()
    if destination == source_root or source_root in destination.parents or destination in source_root.parents:
        raise ValueError('The resource stage must be separate from the source resource tree')
    names, files = resource_files(source, build, allow_missing)
    output = build / names['root']
    output.mkdir(parents=True, exist_ok=True)
    # The stage is exclusively build output. Prune removed/renamed resources so
    # incremental packages cannot silently retain old layouts or stale catalogs.
    for path in output.rglob('*'):
        if path.is_file() and path.relative_to(output) not in files:
            path.unlink()
    for relative, path in files.items():
        target = output / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.exists() or target.read_bytes() != path.read_bytes():
            shutil.copyfile(path, target)
            # Directory-based bundle copy rules must also notice edits to files.
            for parent in target.parents:
                if parent == output:
                    break
                parent.touch()
    for path in sorted(output.rglob('*'), key=lambda p: len(p.parts), reverse=True):
        if path.is_dir() and not any(path.iterdir()):
            path.rmdir()
    return output


def verify(source, build, deployment):
    names, files = resource_files(source, build)
    for root in (build / names['root'], deployment / names['root']):
        for relative, source_file in files.items():
            target = root / relative
            if not target.is_file() or target.read_bytes() != source_file.read_bytes():
                raise ValueError(f'Missing or different deployed resource: {target}')
    print(f'Verified {len(files)} external resources in the stage and Windows artifact')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--allow-missing', action='store_true', help='qmake bootstrap only; permit unbuilt QM files')
    parser.add_argument('--stamp', type=Path)
    parser.add_argument('--verify-windows', type=Path)
    args = parser.parse_args()
    source, build = args.source.resolve(), args.build_dir.resolve()
    try:
        if args.verify_windows:
            verify(source, build, args.verify_windows.resolve())
        else:
            stage(source, build, args.allow_missing)
            if args.stamp:
                args.stamp.touch()
    except (ValueError, OSError, KeyError) as error:
        parser.exit(1, f'Resource deployment: {error}\n')


if __name__ == '__main__':
    main()
