#!/usr/bin/env python3
"""Reuse translations from an older Qt .ts catalog in a newer one.

Usage:
    forward_ts_translations.py original_source new_source destination_file

The destination keeps the new_source file's structure/location entries and only
replaces suitable <translation> elements.
"""

from __future__ import annotations

import argparse
import copy
import difflib
import re
import sys
import xml.etree.ElementTree as ET
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path

# Conservative on purpose. 0.92 catches punctuation, tiny grammar changes,
# singular/plural wording tweaks, etc., without happily mapping rewritten UI.
FUZZY_THRESHOLD = 0.92
# Best match must beat the runner-up by this much, otherwise leave untranslated.
FUZZY_MARGIN = 0.04

MESSAGE_RE = re.compile(r"<message(?:\s[^>]*)?>.*?</message>", re.DOTALL)
TRANSLATION_RE = re.compile(
    r"<translation\b[^>]*(?:/>|>.*?</translation>)", re.DOTALL
)
PLACEHOLDER_RE = re.compile(r"%(?:L?\d+|L?n|n)")


@dataclass(frozen=True)
class Entry:
    context: str
    source: str
    translation_xml: str
    placeholders: tuple[tuple[str, int], ...]
    numerus: bool


def normalized(text: str) -> str:
    return " ".join(text.split()).casefold()


def placeholders(text: str) -> tuple[tuple[str, int], ...]:
    return tuple(sorted(Counter(PLACEHOLDER_RE.findall(text)).items()))


def translation_is_usable(elem: ET.Element | None) -> bool:
    if elem is None:
        return False
    if elem.get("type") in {"unfinished", "vanished", "obsolete"}:
        return False
    # itertext() also sees numerus forms and nested markup.
    return bool("".join(elem.itertext()).strip())


def translation_xml(elem: ET.Element) -> str:
    clone = copy.deepcopy(elem)
    clone.attrib.pop("type", None)
    return ET.tostring(clone, encoding="unicode", short_empty_elements=False)


def load_old_catalog(path: Path):
    root = ET.parse(path).getroot()
    by_context_source: dict[tuple[str, str], list[Entry]] = defaultdict(list)
    by_source: dict[str, list[Entry]] = defaultdict(list)
    by_context: dict[str, list[Entry]] = defaultdict(list)

    for context in root.findall("context"):
        context_name = context.findtext("name", default="")
        for message in context.findall("message"):
            source = message.findtext("source", default="")
            tr = message.find("translation")
            if not source or not translation_is_usable(tr):
                continue
            entry = Entry(
                context=context_name,
                source=source,
                translation_xml=translation_xml(tr),
                placeholders=placeholders(source),
                numerus=message.get("numerus") == "yes",
            )
            by_context_source[(context_name, source)].append(entry)
            by_source[source].append(entry)
            by_context[context_name].append(entry)

    return by_context_source, by_source, by_context


def unique_translation(entries: list[Entry]) -> Entry | None:
    if not entries:
        return None
    variants = {e.translation_xml for e in entries}
    return entries[0] if len(variants) == 1 else None


def compatible(entry: Entry, source: str, numerus: bool) -> bool:
    return entry.numerus == numerus and entry.placeholders == placeholders(source)


def choose_match(
    context: str,
    source: str,
    numerus: bool,
    by_context_source,
    by_source,
    by_context,
):
    # 1. Exact source in the exact same Qt context.
    exact = [
        e for e in by_context_source.get((context, source), [])
        if compatible(e, source, numerus)
    ]
    chosen = unique_translation(exact)
    if chosen:
        return chosen, "exact-context", 1.0

    # 2. Exact English source anywhere, but only if all old translations agree.
    exact_global = [e for e in by_source.get(source, []) if compatible(e, source, numerus)]
    chosen = unique_translation(exact_global)
    if chosen:
        return chosen, "exact-global", 1.0

    # 3. Fuzzy matching is intentionally restricted to the same Qt context.
    #    File paths/line numbers may move freely; context is much stronger signal.
    target = normalized(source)
    scored: list[tuple[float, Entry]] = []
    for entry in by_context.get(context, []):
        if not compatible(entry, source, numerus):
            continue
        score = difflib.SequenceMatcher(None, target, normalized(entry.source)).ratio()
        if score >= FUZZY_THRESHOLD:
            scored.append((score, entry))

    if not scored:
        return None, None, None

    scored.sort(key=lambda x: x[0], reverse=True)
    best_score, best_entry = scored[0]
    if len(scored) > 1 and best_score - scored[1][0] < FUZZY_MARGIN:
        return None, "ambiguous", best_score

    return best_entry, "fuzzy", best_score


def parse_message_fragment(block: str) -> ET.Element:
    # A <message> is self-contained XML in Qt TS files.
    return ET.fromstring(block)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Copy matching translations from an old Qt TS file into a new TS file."
    )
    parser.add_argument("original_source", type=Path)
    parser.add_argument("new_source", type=Path)
    parser.add_argument("destination_file", type=Path)
    args = parser.parse_args()

    by_context_source, by_source, by_context = load_old_catalog(args.original_source)
    text = args.new_source.read_text(encoding="utf-8")

    # Track context while walking the raw document, preserving all original formatting.
    context_ranges = []
    for cm in re.finditer(r"<context>.*?</context>", text, re.DOTALL):
        name_m = re.search(r"<name>(.*?)</name>", cm.group(0), re.DOTALL)
        if name_m:
            try:
                name = ET.fromstring(f"<x>{name_m.group(1)}</x>").text or ""
            except ET.ParseError:
                name = name_m.group(1)
            context_ranges.append((cm.start(), cm.end(), name))

    stats = Counter()
    fuzzy_log = []
    context_idx = 0

    def replace_message(match: re.Match[str]) -> str:
        nonlocal context_idx
        block = match.group(0)
        pos = match.start()
        while context_idx + 1 < len(context_ranges) and pos >= context_ranges[context_idx][1]:
            context_idx += 1
        if not context_ranges or not (context_ranges[context_idx][0] <= pos < context_ranges[context_idx][1]):
            stats["outside-context"] += 1
            return block
        context = context_ranges[context_idx][2]

        try:
            msg = parse_message_fragment(block)
        except ET.ParseError:
            stats["parse-error"] += 1
            return block

        source = msg.findtext("source", default="")
        tr = msg.find("translation")
        if not source or tr is None:
            stats["no-source-or-translation"] += 1
            return block

        # Do not overwrite translations somebody has already supplied manually.
        if translation_is_usable(tr):
            stats["already-translated"] += 1
            return block

        numerus = msg.get("numerus") == "yes"
        entry, kind, score = choose_match(
            context, source, numerus,
            by_context_source, by_source, by_context,
        )
        if entry is None:
            stats[kind or "unmatched"] += 1
            return block

        new_block, n = TRANSLATION_RE.subn(entry.translation_xml, block, count=1)
        if n != 1:
            stats["translation-tag-error"] += 1
            return block

        stats[kind] += 1
        if kind == "fuzzy":
            fuzzy_log.append((score, context, source, entry.source))
        return new_block

    output = MESSAGE_RE.sub(replace_message, text)
    args.destination_file.write_text(output, encoding="utf-8")

    # Fail if our output is not valid XML.
    ET.parse(args.destination_file)

    copied = stats["exact-context"] + stats["exact-global"] + stats["fuzzy"]
    print(f"Wrote: {args.destination_file}", file=sys.stderr)
    print(
        f"Copied {copied} translations: "
        f"{stats['exact-context']} exact-context, "
        f"{stats['exact-global']} exact-global, "
        f"{stats['fuzzy']} fuzzy.",
        file=sys.stderr,
    )
    print(
        f"Left untouched: {stats['unmatched']} unmatched, "
        f"{stats['ambiguous']} ambiguous, "
        f"{stats['already-translated']} already translated.",
        file=sys.stderr,
    )

    if fuzzy_log:
        print("\nFuzzy matches used:", file=sys.stderr)
        for score, context, new, old in sorted(fuzzy_log, reverse=True):
            print(f"  {score:.3f} [{context}] {new!r}  <-  {old!r}", file=sys.stderr)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
