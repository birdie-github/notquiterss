#!/usr/bin/env python3
"""Generate self-contained application QSS themes in the build directory from base.css and .ini files."""
import argparse
import configparser
from pathlib import Path
import re

VAR_PATTERN = re.compile(r'var\(--([a-zA-Z0-9_]+)\)')
EMBEDDED_IDS = ('light', 'dark')


def render_css(template: str, variables: dict, source_name: str) -> tuple[str, set[str]]:
    used = set()

    def replace(match: re.Match) -> str:
        key = match.group(1)
        if key not in variables:
            raise ValueError(f'{source_name}: missing variable --{key}')
        used.add(key)
        return variables[key]

    return VAR_PATTERN.sub(replace, template), used


def generate_theme(ini_path: Path, base_css: str, controls_css: str) -> tuple[str, str]:
    parser = configparser.ConfigParser(interpolation=None)
    parser.optionxform = str
    with ini_path.open('r', encoding='utf-8') as f:
        parser.read_file(f, source=str(ini_path))

    for section in ('Metadata', 'Palette', 'Variables'):
        if not parser.has_section(section):
            raise ValueError(f'{ini_path.name}: missing [{section}] section')

    meta = parser['Metadata']
    name = meta.get('Name', '').strip()
    theme_id = meta.get('Id', '').strip()
    mode = meta.get('Mode', 'fixed').strip()
    include_controls = meta.getboolean('IncludeControls', fallback=False)
    if not name or not theme_id:
        raise ValueError(f'{ini_path.name}: Name and Id are required in [Metadata]')

    header_lines = [
        '/* ApplicationStyle',
        f'Name={name}',
        f'Id={theme_id}',
        f'Mode={mode}',
    ]
    if 'Default' in meta:
        header_lines.append(f"Default={meta['Default'].strip()}")
    for role, color in parser['Palette'].items():
        header_lines.append(f'Palette.{role}={color.strip()}')
    header_lines.append('*/')
    header_lines.append(
        '/* Generated from base.css. If you want to modify the theme make a copy and edit it. */'
    )

    variables = {k: v.strip() for k, v in parser['Variables'].items()}
    body, used = render_css(base_css, variables, f'{ini_path.name} (base.css)')
    parts = ['\n'.join(header_lines), '', body.strip()]

    if include_controls:
        ctrl_body, ctrl_used = render_css(
            controls_css, variables, f'{ini_path.name} (controls.css)'
        )
        used |= ctrl_used
        parts.extend(['', ctrl_body.strip()])

    unused = set(variables.keys()) - used
    if unused:
        unused_list = ', '.join(sorted(unused))
        raise ValueError(f'{ini_path.name}: unused variables: {unused_list}')

    return theme_id, '\n'.join(parts) + '\n'


def write_if_changed(path: Path, content: str) -> None:
    existing = path.read_text(encoding='utf-8') if path.is_file() else None
    if existing != content:
        path.write_text(content, encoding='utf-8', newline='\n')


def generate_all(source: Path, output_dir: Path) -> dict[str, Path]:
    theme_src = source / 'resources/embedded/themes'
    source_root = (source / 'resources').resolve()
    output_resolved = output_dir.resolve()
    if output_resolved == source_root or source_root in output_resolved.parents:
        raise ValueError('Generated themes must be written to a build directory, not the source tree')

    base_css = (theme_src / 'base.css').read_text(encoding='utf-8')
    controls_css = (theme_src / 'controls.css').read_text(encoding='utf-8')

    ini_files = sorted(theme_src.glob('*.ini'))
    if not ini_files:
        raise ValueError(f'No theme .ini files found in {theme_src}')

    output_dir.mkdir(parents=True, exist_ok=True)
    generated = {}
    for ini_path in ini_files:
        theme_id, content = generate_theme(ini_path, base_css, controls_css)
        target = output_dir / f'{theme_id}.qss'
        write_if_changed(target, content)
        generated[theme_id] = target

    for embedded_id in EMBEDDED_IDS:
        if embedded_id not in generated:
            raise ValueError(f'Missing required embedded fallback theme: {embedded_id}')

    qrc_lines = [
        '<RCC>',
        '    <qresource prefix="/style">',
        *(f'        <file alias="{tid}">{tid}.qss</file>' for tid in EMBEDDED_IDS),
        '    </qresource>',
        '</RCC>',
        '',
    ]
    write_if_changed(output_dir / 'themes.qrc', '\n'.join(qrc_lines))
    return generated


def main():
    default_source = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=default_source)
    parser.add_argument('--output', type=Path, required=True, help='Build directory for generated .qss and themes.qrc')
    args = parser.parse_args()
    try:
        generate_all(args.source.resolve(), args.output.resolve())
    except (ValueError, OSError, configparser.Error) as error:
        parser.exit(1, f'Theme generation: {error}\n')


if __name__ == '__main__':
    main()
