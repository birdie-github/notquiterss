#!/usr/bin/env python3

"""
Convert a Netscape-format cookies.txt file to NotQuiteRSS cookies.dat.

Usage:
    import-cookies.py <path/to/cookies.txt> <path/to/cookies.dat>

The script:
  * accepts standard Netscape/Mozilla cookies.txt files;
  * preserves HttpOnly, Secure, Domain, Path and expiry attributes;
  * refuses to run while NotQuiteRSS is running;
  * validates all input before touching the destination;
  * writes cookies.dat atomically;
  * backs up an existing cookies.dat before replacing it.

No third-party Python modules are required.
"""

from __future__ import annotations

import datetime as _datetime
import os
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


PROGRAM_NAME = "notquiterss"
EXPECTED_ARGS = 2


class CookieImportError(Exception):
    pass


def usage(file=sys.stderr) -> None:
    prog = Path(sys.argv[0]).name
    print(
        f"Usage: {prog} <cookies.txt> <path/to/cookies.dat>\n"
        f"\n"
        f"Example:\n"
        f"  {prog} ~/Downloads/cookies.txt "
        f"~/.local/share/NotQuiteRSS/cookies.dat",
        file=file,
    )


def is_notquiterss_running() -> tuple[bool, list[int]]:
    """
    Return (running, pids).

    Linux:
      Inspect /proc directly, matching either /proc/PID/comm or the basename
      of /proc/PID/exe. This avoids matching this script's own command line.

    Other Unix-like systems:
      Use pgrep -x.

    Windows:
      Use tasklist and match notquiterss.exe exactly.
    """
    pids: list[int] = []

    if sys.platform.startswith("linux"):
        proc = Path("/proc")
        try:
            entries = proc.iterdir()
        except OSError:
            entries = ()

        for entry in entries:
            if not entry.name.isdigit():
                continue

            pid = int(entry.name)

            try:
                comm = (entry / "comm").read_text(
                    encoding="utf-8", errors="replace"
                ).strip()
            except OSError:
                comm = ""

            exe_name = ""
            try:
                exe_name = Path(os.readlink(entry / "exe")).name
            except OSError:
                pass

            if comm == PROGRAM_NAME or exe_name == PROGRAM_NAME:
                pids.append(pid)

        return bool(pids), sorted(set(pids))

    if os.name == "nt":
        try:
            result = subprocess.run(
                ["tasklist", "/FI", "IMAGENAME eq notquiterss.exe", "/FO", "CSV", "/NH"],
                stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL,
                text=True,
                check=False,
            )
        except OSError:
            return False, []

        for line in result.stdout.splitlines():
            # Example:
            # "notquiterss.exe","1234","Console","1","12,345 K"
            if not line.lower().startswith('"notquiterss.exe",'):
                continue
            fields = [field.strip('"') for field in line.split('","')]
            if len(fields) >= 2:
                try:
                    pids.append(int(fields[1].strip('"')))
                except ValueError:
                    pass

        return bool(pids), sorted(set(pids))

    try:
        result = subprocess.run(
            ["pgrep", "-x", PROGRAM_NAME],
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            check=False,
        )
    except OSError:
        return False, []

    for line in result.stdout.splitlines():
        try:
            pids.append(int(line.strip()))
        except ValueError:
            pass

    return bool(pids), sorted(set(pids))


def http_date_from_unix(timestamp: int) -> str:
    try:
        dt = _datetime.datetime.fromtimestamp(
            timestamp, tz=_datetime.timezone.utc
        )
    except (OverflowError, OSError, ValueError) as exc:
        raise CookieImportError(
            f"invalid cookie expiry timestamp {timestamp}: {exc}"
        ) from exc

    # RFC 7231 / HTTP-date, e.g.:
    # Wed, 21 Oct 2015 07:28:00 GMT
    return dt.strftime("%a, %d %b %Y %H:%M:%S GMT")


def parse_netscape_cookie_file(path: Path) -> list[bytes]:
    """
    Parse Netscape cookies.txt and return QNetworkCookie-compatible raw forms.

    Expected fields:
        domain
        include_subdomains
        path
        secure
        expires
        name
        value
    """
    cookies: list[bytes] = []
    seen = set()

    try:
        text = path.read_text(encoding="utf-8-sig")
    except UnicodeDecodeError:
        # cookies.txt is normally UTF-8/ASCII, but browser exporters sometimes
        # preserve arbitrary byte-ish values through Latin-1.
        try:
            text = path.read_text(encoding="latin-1")
        except OSError as exc:
            raise CookieImportError(f"cannot read {path}: {exc}") from exc
    except OSError as exc:
        raise CookieImportError(f"cannot read {path}: {exc}") from exc

    for lineno, original_line in enumerate(text.splitlines(), 1):
        line = original_line.rstrip("\r\n")

        if not line.strip():
            continue

        httponly = False

        if line.startswith("#HttpOnly_"):
            httponly = True
            line = line[len("#HttpOnly_") :]
        elif line.startswith("#"):
            continue

        fields = line.split("\t", 6)
        if len(fields) != 7:
            raise CookieImportError(
                f"{path}:{lineno}: expected 7 tab-separated fields, "
                f"got {len(fields)}"
            )

        (
            domain,
            include_subdomains,
            cookie_path,
            secure,
            expires_text,
            name,
            value,
        ) = fields

        if not domain:
            raise CookieImportError(f"{path}:{lineno}: empty cookie domain")

        if not cookie_path:
            cookie_path = "/"

        include_subdomains_upper = include_subdomains.upper()
        if include_subdomains_upper not in {"TRUE", "FALSE"}:
            raise CookieImportError(
                f"{path}:{lineno}: invalid include-subdomains value "
                f"{include_subdomains!r}"
            )

        secure_upper = secure.upper()
        if secure_upper not in {"TRUE", "FALSE"}:
            raise CookieImportError(
                f"{path}:{lineno}: invalid secure value {secure!r}"
            )

        if not expires_text:
            raise CookieImportError(
                f"{path}:{lineno}: empty expiry field"
            )

        try:
            expires = int(expires_text, 10)
        except ValueError as exc:
            raise CookieImportError(
                f"{path}:{lineno}: invalid expiry timestamp "
                f"{expires_text!r}"
            ) from exc

        if expires < 0:
            raise CookieImportError(
                f"{path}:{lineno}: negative expiry timestamp {expires}"
            )

        if not name:
            raise CookieImportError(f"{path}:{lineno}: empty cookie name")

        # CR/LF cannot safely appear in a Set-Cookie-style raw form.
        for field_name, field_value in (
            ("domain", domain),
            ("path", cookie_path),
            ("name", name),
            ("value", value),
        ):
            if "\r" in field_value or "\n" in field_value:
                raise CookieImportError(
                    f"{path}:{lineno}: {field_name} contains CR/LF"
                )

        parts = [f"{name}={value}"]

        # expires=0 in Netscape format means a session cookie.
        if expires != 0:
            parts.append(f"expires={http_date_from_unix(expires)}")

        parts.append(f"domain={domain}")
        parts.append(f"path={cookie_path}")

        if secure_upper == "TRUE":
            parts.append("secure")

        if httponly:
            parts.append("HttpOnly")

        raw = "; ".join(parts).encode("utf-8")

        # Preserve distinct cookie records, but silently suppress byte-identical
        # duplicates. Multiple cookies with the same name are valid if their
        # domain/path differ.
        if raw not in seen:
            seen.add(raw)
            cookies.append(raw)

    if not cookies:
        raise CookieImportError(
            f"{path}: no cookies found; is this a Netscape-format cookies.txt file?"
        )

    return cookies


def qdatastream_qbytearray(data: bytes) -> bytes:
    """
    Serialize QByteArray as QDataStream does for ordinary (< 4 GiB) arrays.

    QDataStream uses a big-endian quint32 byte count followed by the bytes.
    """
    size = len(data)

    # 0xffffffff means null; 0xfffffffe is Qt's extended-size marker.
    if size >= 0xFFFFFFFE:
        raise CookieImportError(
            "cookie record is too large for the standard QByteArray encoding"
        )

    return struct.pack(">I", size) + data


def build_cookies_dat(cookies: list[bytes]) -> bytes:
    if len(cookies) > 0x7FFFFFFF:
        raise CookieImportError("too many cookies")

    # C++ code writes `int count`, i.e. qint32, using QDataStream's
    # default big-endian byte order.
    out = bytearray(struct.pack(">i", len(cookies)))

    for cookie in cookies:
        out.extend(qdatastream_qbytearray(cookie))

    return bytes(out)


def backup_existing(path: Path) -> Path | None:
    if not path.exists():
        return None

    if not path.is_file():
        raise CookieImportError(
            f"destination exists but is not a regular file: {path}"
        )

    stamp = _datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    candidate = path.with_name(f"{path.name}.bak-{stamp}")
    counter = 1

    while candidate.exists():
        candidate = path.with_name(
            f"{path.name}.bak-{stamp}-{counter}"
        )
        counter += 1

    try:
        shutil.copy2(path, candidate)
    except OSError as exc:
        raise CookieImportError(
            f"cannot back up existing {path} to {candidate}: {exc}"
        ) from exc

    return candidate


def atomic_write(path: Path, data: bytes) -> None:
    parent = path.parent

    if not parent.exists():
        raise CookieImportError(
            f"destination directory does not exist: {parent}"
        )

    if not parent.is_dir():
        raise CookieImportError(
            f"destination parent is not a directory: {parent}"
        )

    old_mode = None
    if path.exists():
        try:
            old_mode = path.stat().st_mode & 0o7777
        except OSError:
            pass

    temp_name = None

    try:
        fd, temp_name = tempfile.mkstemp(
            prefix=f".{path.name}.",
            suffix=".tmp",
            dir=str(parent),
        )

        with os.fdopen(fd, "wb") as f:
            f.write(data)
            f.flush()
            os.fsync(f.fileno())

        if old_mode is not None:
            os.chmod(temp_name, old_mode)
        elif os.name != "nt":
            # cookies.dat may contain authentication credentials.
            os.chmod(temp_name, 0o600)

        os.replace(temp_name, path)
        temp_name = None

        # Best effort: fsync the directory so the rename itself reaches disk.
        if os.name != "nt":
            try:
                dir_fd = os.open(parent, os.O_RDONLY)
                try:
                    os.fsync(dir_fd)
                finally:
                    os.close(dir_fd)
            except OSError:
                pass

    except OSError as exc:
        raise CookieImportError(f"cannot write {path}: {exc}") from exc
    finally:
        if temp_name is not None:
            try:
                os.unlink(temp_name)
            except OSError:
                pass


def main() -> int:
    if len(sys.argv) != EXPECTED_ARGS + 1:
        usage()
        return 2

    source = Path(sys.argv[1]).expanduser()
    destination = Path(sys.argv[2]).expanduser()

    if not source.exists():
        print(f"Error: input file does not exist: {source}", file=sys.stderr)
        return 1

    if not source.is_file():
        print(f"Error: input is not a regular file: {source}", file=sys.stderr)
        return 1

    try:
        if source.resolve() == destination.resolve():
            print(
                "Error: input and destination refer to the same file.",
                file=sys.stderr,
            )
            return 1
    except OSError:
        pass

    running, pids = is_notquiterss_running()
    if running:
        pid_text = ", ".join(str(pid) for pid in pids)
        suffix = f" (PID{'s' if len(pids) != 1 else ''}: {pid_text})" if pids else ""
        print(
            f"Error: {PROGRAM_NAME} is running{suffix}.\n"
            f"Exit NotQuiteRSS completely before importing cookies.",
            file=sys.stderr,
        )
        return 1

    try:
        cookies = parse_netscape_cookie_file(source)
        data = build_cookies_dat(cookies)

        backup = backup_existing(destination)
        atomic_write(destination, data)

    except CookieImportError as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1

    print(f"Imported {len(cookies)} cookie{'s' if len(cookies) != 1 else ''}.")
    print(f"Wrote: {destination}")

    if backup is not None:
        print(f"Backup: {backup}")

    session_count = sum(
        b"; expires=" not in cookie.lower()
        for cookie in cookies
    )
    if session_count:
        print(
            f"Note: {session_count} session cookie"
            f"{'s were' if session_count != 1 else ' was'} imported. "
            f"NotQuiteRSS does not persist session cookies when it later "
            f"rewrites cookies.dat."
        )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
