#!/usr/bin/env python3
"""Generate build-only project metadata. Python standard library; no network access."""
import argparse
import datetime
import json
import re
import subprocess
from pathlib import Path
from urllib.parse import urlsplit
from xml.sax.saxutils import escape


def string(value, key):
    if not isinstance(value, str) or not value or any(ord(c) < 32 for c in value):
        raise ValueError(f'{key}: expected a nonempty single-line string')
    return value


def component(value, key):
    value = string(value, key)
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]*', value) or value in ('.', '..'):
        raise ValueError(f'{key}: expected a filename/identifier without directories')
    return value


def url(value, key):
    value = string(value, key)
    parsed = urlsplit(value)
    if parsed.scheme != 'https' or not parsed.netloc or parsed.username or parsed.password:
        raise ValueError(f'{key}: expected an absolute HTTPS URL without credentials')
    return value.rstrip('/')


def default_feeds(entries):
    if not isinstance(entries, list):
        raise ValueError('default_feeds: expected an array')
    result = []
    urls = set()
    for index, entry in enumerate(entries):
        key = f'default_feeds[{index}]'
        required = {'enabled', 'title', 'feed_url'}
        if (not isinstance(entry, dict) or not required <= entry.keys()
                or entry.keys() - (required | {'website_url'})):
            raise ValueError(f'{key}: unexpected or missing key')
        if type(entry['enabled']) is not bool:
            raise ValueError(f'{key}.enabled: expected a boolean')
        title = string(entry['title'], key + '.title')
        urls_for_entry = []
        for field in ('feed_url', 'website_url'):
            value = entry.get(field, '')
            if field == 'website_url' and value == '':
                urls_for_entry.append('')
                continue
            string(value, key + '.' + field)
            parsed = urlsplit(value)
            if (parsed.scheme not in ('http', 'https') or not parsed.hostname
                    or parsed.username is not None or parsed.password is not None
                    or any(c.isspace() for c in value)):
                raise ValueError(f'{key}.{field}: expected an HTTP(S) URL without credentials')
            urls_for_entry.append(value)
        if entry['enabled']:
            feed_url, website_url = urls_for_entry
            if feed_url in urls:
                raise ValueError(f'{key}.feed_url: duplicate enabled feed')
            urls.add(feed_url)
            result.append(dict(title=title, feedUrl=feed_url, websiteUrl=website_url))
    return result


def load(source):
    data = json.loads((source / 'project.json').read_text(encoding='utf-8'))
    allowed = {'identity', 'release', 'project', 'resources', 'files', 'default_feeds'}
    if set(data) != allowed:
        raise ValueError('project.json: unexpected or missing section')
    keys = {
        'identity': {'name', 'bundle_namespace'},
        'release': {'version', 'date'},
        'project': {'repository', 'homepage', 'issues', 'releases', 'translations',
                    'update_endpoint', 'email', 'copyright', 'original_copyright', 'original_project'},
        'resources': {'root', 'translations', 'qt_translations', 'styles',
                      'sounds', 'sharing_config', 'sharing_icons'},
        'files': {'database', 'log', 'cookies', 'last_feed', 'portable_marker', 'cache', 'backup'}
    }
    for section, values in data.items():
        if section == 'default_feeds':
            continue
        if not isinstance(values, dict) or set(values) - keys[section]:
            raise ValueError(f'{section}: unexpected key')
    identity, release, project = (data[k] for k in ('identity', 'release', 'project'))
    name = component(identity['name'], 'identity.name')
    version = string(release['version'], 'release.version')
    if not re.fullmatch(r'(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)', version):
        raise ValueError('release.version: expected major.minor.patch')
    if any(int(n) > 65535 for n in version.split('.')):
        raise ValueError('release.version: Windows version components must fit 16 bits')
    date = datetime.date.fromisoformat(release['date']).isoformat()
    repository = url(project['repository'], 'project.repository')
    parsed = urlsplit(repository)
    if parsed.netloc != 'github.com' or not re.fullmatch(r'/[\w.-]+/[\w.-]+', parsed.path):
        raise ValueError('project.repository: expected https://github.com/owner/repository')
    result = dict(name=name, executable=name.lower(), translationPrefix=name + "_",
                  displayName=name,
                  organization=name,
                  bundleId=component(identity['bundle_namespace'], 'identity.bundle_namespace') + '.' + name,
                  version=version, releaseDate=date, sourceUrl=repository,
                  email=string(project['email'], 'project.email'),
                  copyright=string(project['copyright'], 'project.copyright'),
                  originalCopyright=string(project['original_copyright'], 'project.original_copyright'))
    for key, output, default in (
        ('homepage', 'homepageUrl', repository),
        ('issues', 'issuesUrl', repository + '/issues'),
        ('releases', 'releasesUrl', repository + '/releases'),
        ('original_project', 'originalProjectUrl', project['original_project']),
        ('translations', 'translationsUrl', repository + '/tree/HEAD/' + data['resources']['root'] + '/external/' + data['resources']['translations']),
        ('update_endpoint', 'updateEndpoint', 'https://api.github.com/repos' + parsed.path + '/releases/latest')):
        result[output] = url(project.get(key, default), 'project.' + key)
    for section in ('resources', 'files'):
        for key in sorted(keys[section]):
            output = ''.join([key.split('_')[0]] + [part.title() for part in key.split('_')[1:]])
            result[output] = component(data[section][key], section + '.' + key)
    result['defaultFeeds'] = default_feeds(data['default_feeds'])
    return result


def write(path, contents):
    path.parent.mkdir(parents=True, exist_ok=True)
    contents = contents.encode('utf-8') if isinstance(contents, str) else contents
    if not path.exists() or path.read_bytes() != contents:
        path.write_bytes(contents)


def cpp(text):
    quoted = json.dumps(text, ensure_ascii=False)
    return ''.join(c if ord(c) < 128 else
                   ('\\u%04x' % ord(c) if ord(c) <= 0xffff else '\\U%08x' % ord(c))
                   for c in quoted)


def rc_string(text):
    # RC uses UTF-16 hexadecimal escapes, not C++ universal character names.
    units = text.encode('utf-16-le')
    return 'L"' + ''.join('\\x%04x' % int.from_bytes(units[i:i+2], 'little')
                          for i in range(0, len(units), 2)) + '"'


def qmake(text):
    # Do not let project strings become qmake expressions or shell substitutions.
    if any(c in text for c in '\n\r$#'):
        raise ValueError('unsupported qmake metacharacter')
    return '"' + text.replace('\\', '/').replace('"', '\\"') + '"'


def render(template, values, transform=lambda s: s):
    def replace(match):
        key = match.group(1)
        if key not in values:
            raise ValueError(f'unknown template field: {key}')
        return transform(values[key])
    return re.sub(r'@META_([A-Za-z0-9_]+)@', replace, template)


def generate(source, output):
    values = load(source)
    try:
        revision = subprocess.check_output(['git', '-C', str(source), 'rev-parse', '--short=12', 'HEAD'],
                                           stderr=subprocess.DEVNULL, text=True).strip()
    except (OSError, subprocess.CalledProcessError):
        revision = ''
    values['revision'] = revision
    feeds = values.pop('defaultFeeds')
    header = '#pragma once\n#include <QString>\n#include <QList>\n\nnamespace ProjectMetadata {\n'
    for key, value in values.items():
        header += f'inline QString {key}() {{ return QStringLiteral({cpp(value)}); }}\n'
    header += 'struct DefaultFeed { QString title; QString feedUrl; QString websiteUrl; };\n'
    header += 'inline QList<DefaultFeed> defaultFeeds() { return {\n'
    for feed in feeds:
        fields = ', '.join(f'QStringLiteral({cpp(feed[key])})' for key in
                           ('title', 'feedUrl', 'websiteUrl'))
        header += '  {' + fields + '},\n'
    header += '}; }\n'
    header += '} // namespace ProjectMetadata\n'
    write(output / 'projectmetadata.h', header)
    rc = '#pragma once\n'
    rc += '#define PROJECT_VERSION_NUMERIC ' + values['version'].replace('.', ',') + ',0\n'
    for key in ('name', 'displayName', 'organization', 'version', 'copyright', 'originalCopyright'):
        rc += f'#define PROJECT_{key.upper()} {rc_string(values[key])}\n'
    rc += '#define PROJECT_EXE ' + rc_string(values['executable'] + '.exe') + '\n'
    rc += '#define PROJECT_LEGALCOPYRIGHT ' + rc_string(values['originalCopyright'] + '; ' + values['copyright']) + '\n'
    write(output / 'projectmetadata_rc.h', rc)
    # A small, machine-readable build result lets CI avoid guessing executable names.
    write(output / 'project-build.json', json.dumps(values, ensure_ascii=False, indent=2) + '\n')
    variables = {'NAME': 'name', 'EXECUTABLE': 'executable', 'VERSION': 'version', 'BUNDLE_ID': 'bundleId',
                 'RESOURCE_ROOT': 'root', 'LANG_DIR': 'translations', 'TRANSLATION_PREFIX': 'translationPrefix',
                 'STYLE_DIR': 'styles', 'SOUND_DIR': 'sounds',
                 'SHARING_CONFIG': 'sharingConfig', 'SHARING_ICONS': 'sharingIcons'}
    pri = ''.join(f'PROJECT_{key} = {qmake(values[value])}\n' for key, value in variables.items())
    write(output / 'project.pri', pri)
    for filename, transform in [('Doxyfile', lambda s: s), ('Info.plist', escape), ('appdata.xml', escape),
                                ('application.desktop', lambda s: s.replace('\\', '\\\\'))]:
        template = (source / 'packaging' / (filename + '.in')).read_text(encoding='utf-8')
        write(output / filename, render(template, values, transform))
    template = (source / 'packaging/application.rc.in').read_text(encoding='utf-8')
    write(output / 'application.rc', template.replace('@ICON_FILE@', str(source / values['root'] / 'external' / 'icons' / 'application.ico').replace('\\', '/').replace('"', '\\"')))
    for size in (16, 32, 48, 64, 128, 256):
        write(output / 'icons' / str(size) / (values['name'] + '.png'),
              (source / values['root'] / 'external' / 'icons' / f'{size}x{size}' / 'quiterss.png').read_bytes())
    # Give install sets the desired public filenames without renaming source templates.
    write(output / (values['name'] + '.desktop'), (output / 'application.desktop').read_bytes())
    write(output / (values['bundleId'] + '.metainfo.xml'), (output / 'appdata.xml').read_bytes())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        generate(args.source.resolve(), args.output.resolve())
    except (ValueError, KeyError, OSError) as error:
        parser.exit(1, f'Project metadata: {error}\n')


if __name__ == '__main__':
    main()
