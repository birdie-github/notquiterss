#!/usr/bin/env python3
"""Install verified miniaudio headers outside the source tree; never run by qmake."""
import argparse
import hashlib
import os
from pathlib import Path
import sys
import tempfile
import urllib.request

VERSION = '0.11.26'
REVISION = '877ea4a7c2601c7b0f2a44e83dfac42e6470fea6'
BASE_URL = 'https://raw.githubusercontent.com/mackron/miniaudio/' + REVISION + '/'
FILES = {
    'miniaudio.h': ('include/miniaudio.h',
                   '6b376c7fbb1352ba1e0e432ab66069b335ea9405e45cb6c30b97ef0f3c9c3e0e'),
    'LICENSE': ('share/licenses/miniaudio/LICENSE',
                '457f1b500e0adf6bc059edddfa78a2f62012e7c3bb43476c20e0bd23b25ba0eb'),
}


def verified(data, digest):
    return hashlib.sha256(data).hexdigest() == digest


def install(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as stream:
        temporary = Path(stream.name)
        stream.write(data)
    try:
        temporary.chmod(0o644)
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix', required=True, help='dedicated external installation directory')
    parser.add_argument('--source-dir', help='offline source directory containing the pinned header and license')
    args = parser.parse_args()
    prefix = Path(args.prefix).expanduser().resolve()
    repo = Path(__file__).resolve().parent.parent
    if prefix == repo or repo in prefix.parents:
        parser.error('--prefix must be outside the application source tree')

    # Verify ALL inputs before installing anything. Existing verified files allow
    # offline repeated runs; --source-dir explicitly verifies the supplied sources.
    pending = []
    for name, (relative, digest) in FILES.items():
        target = prefix / relative
        if args.source_dir:
            data = (Path(args.source_dir) / name).read_bytes()
        elif target.is_file() and verified(target.read_bytes(), digest):
            continue
        else:
            with urllib.request.urlopen(BASE_URL + name, timeout=60) as response:
                data = response.read()
        if not verified(data, digest):
            raise ValueError('SHA-256 mismatch for ' + name + '; nothing installed')
        pending.append((target, data))
    for target, data in pending:
        install(target, data)
    install(prefix / 'share/licenses/miniaudio/SOURCE.txt',
            ('https://github.com/mackron/miniaudio\n' + VERSION + '\n' + REVISION + '\n').encode())
    print('miniaudio ' + VERSION + ' prepared; MINIAUDIO_INCLUDE_DIR=' + (prefix / 'include').as_posix())


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError) as exc:
        sys.exit('miniaudio preparation failed: ' + str(exc))
