#!/usr/bin/env python3
"""Build the Qt5 modules needed by the Windows x86/SSE2 CI package."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys

# Qt 5.15.18 public LGPL source releases, pinned to commits rather than branches.
REVISIONS = {
    'qtbase': '49adb85d34918034e0d6a4c23817407103fb9f73',
    'qtsvg': '119bb997151cfcab6b13246b8edd2783ff1df8d5',
    'qtimageformats': '21492b6e2ef4a5d913bdce1d34ec32e6e7ed53ae',
    'qttools': '83b91bf9dbdde0bd51c1906168ad4b4ecef54420',
    'qttranslations': '3cbcceb8e3e2e63a4022f1be946c7118c527a83e',
}
CPU_FLAGS = '-march=pentium4 -mtune=generic'


def run(*args, cwd=None):
    subprocess.run([str(arg) for arg in args], cwd=cwd, check=True)


def output(*args):
    return subprocess.check_output(args, text=True).strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work-dir', type=Path, required=True)
    parser.add_argument('--prefix', type=Path, required=True)
    parser.add_argument('--jobs', type=int, default=2)
    args = parser.parse_args()
    if sys.platform != 'win32':
        parser.error('Run with native Windows Python in an MSYS2 MINGW32 shell')
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    for tool in ('git', 'gcc', 'g++', 'mingw32-make', 'perl', 'pkg-config'):
        if not shutil.which(tool):
            parser.error('Required executable not found: ' + tool)
    if output('gcc', '-dumpmachine') != 'i686-w64-mingw32':
        parser.error('Select the 32-bit MINGW32 compiler')
    work = args.work_dir.resolve()
    prefix = args.prefix.resolve()
    repo = Path(__file__).resolve().parent.parent
    for path in (work, prefix):
        if path == repo or repo in path.parents:
            parser.error('Dependency directories must be outside the application source')
    work.mkdir(parents=True, exist_ok=True)
    sources = {}
    for module, revision in REVISIONS.items():
        source = work / module
        if source.exists():
            parser.error('Use a fresh work directory; already exists: ' + str(source))
        run('git', 'init', source)
        run('git', '-C', source, 'fetch', '--depth=1',
            'https://github.com/qt/' + module + '.git', revision)
        run('git', '-C', source, 'checkout', '--detach', 'FETCH_HEAD')
        if output('git', '-C', str(source), 'rev-parse', 'HEAD') != revision:
            parser.error('Unexpected source revision: ' + module)
        sources[module] = source

    # No OpenGL backend is needed by this text-widget application. Qt's bundled
    # font/image helpers avoid optional system dependencies; TLS and SQLite use
    # the same MINGW32 installation as the application's external libraries.
    # Query pkg-config rather than infer the compiler's installation layout.
    includes = output('pkg-config', '--variable=includedir', 'openssl')
    libraries = output('pkg-config', '--variable=libdir', 'openssl')
    build = work / 'build-qtbase'
    build.mkdir()
    run('cmd.exe', '/d', '/c', sources['qtbase'] / 'configure.bat',
        '-opensource', '-confirm-license', '-release', '-shared',
        '-platform', 'win32-g++', '-prefix', prefix.as_posix(),
        '-nomake', 'examples', '-nomake', 'tests', '-no-opengl', '-no-dbus',
        '-no-icu', '-openssl-runtime', '-I', includes, '-L', libraries,
        '-qt-zlib', '-qt-libpng', '-qt-libjpeg', '-qt-freetype', '-qt-harfbuzz',
        '-qt-pcre', '-system-sqlite',
        'QMAKE_CFLAGS+=' + CPU_FLAGS, 'QMAKE_CXXFLAGS+=' + CPU_FLAGS, cwd=build)
    run('mingw32-make', '-j' + str(args.jobs), cwd=build)
    run('mingw32-make', 'install', cwd=build)
    qmake = prefix / 'bin' / 'qmake.exe'
    for module in ('qtsvg', 'qtimageformats', 'qttools'):
        build = work / ('build-' + module)
        build.mkdir()
        configure = ('--', '-no-feature-assistant', '-no-feature-designer') if module == 'qttools' else ()
        run(qmake, sources[module] / (module + '.pro'),
            'QMAKE_CFLAGS+=' + CPU_FLAGS, 'QMAKE_CXXFLAGS+=' + CPU_FLAGS,
            *configure, cwd=build)
        run('mingw32-make', '-j' + str(args.jobs), cwd=build)
        run('mingw32-make', 'install', cwd=build)

    translations = prefix / 'translations'
    translations.mkdir(exist_ok=True)
    for source in sorted((sources['qttranslations'] / 'translations').glob('qtbase_*.ts')):
        run(prefix / 'bin' / 'lrelease.exe', source,
            '-qm', translations / (source.stem + '.qm'))
    notices = prefix / 'share' / 'licenses' / 'qt5'
    for module, source in sources.items():
        destination = notices / module
        destination.mkdir(parents=True, exist_ok=True)
        for license_file in source.glob('LICENSE*'):
            if license_file.is_file():
                shutil.copy2(license_file, destination / license_file.name)
        (destination / 'SOURCE.txt').write_text(
            'https://github.com/qt/' + module + '.git\n' + REVISIONS[module] + '\n',
            encoding='utf-8')
    for tool in ('qmake.exe', 'lrelease.exe', 'windeployqt.exe'):
        if not (prefix / 'bin' / tool).is_file():
            raise RuntimeError('Missing installed Qt tool: ' + tool)
    if not list(translations.glob('qtbase_*.qm')):
        raise RuntimeError('Qt widget translations were not generated')
    print('Installed Windows x86/SSE2 Qt5 at ' + str(prefix))


if __name__ == '__main__':
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        sys.exit('Qt5 preparation failed: ' + str(error))
