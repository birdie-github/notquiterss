#!/usr/bin/env python3
"""Install verified miniaudio headers outside the source tree; never run by qmake."""
import argparse
import hashlib
import os
from pathlib import Path
import sys
import tempfile
import urllib.request

VERSION = '0.11.25'
REVISION = '9634bedb5b5a2ca38c1ee7108a9358a4e233f14d'
BASE_URL = 'https://raw.githubusercontent.com/mackron/miniaudio/' + REVISION + '/'
FILES = {
    'miniaudio.h': ('include/miniaudio.h',
                   'ac7af4de748b7e26b777f37e01cee313a308a7296a3eb080e2906b320cc55c89'),
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
