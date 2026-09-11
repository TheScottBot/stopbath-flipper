#!/usr/bin/env python3
"""
Repository scan for specification 0.8: no em dash in any tracked text file,
and no converter-mangled double or triple hyphen in prose either, since those
are the same defect wearing a different coat.

Exemptions are exactly two, chosen because each is a place a run of hyphens
has an unambiguous non-prose meaning:

1. A Markdown table separator row, which consists only of pipes, hyphens,
   colons and spaces.
2. A command line style flag: a hyphen run at the start of a token that is
   immediately followed by a letter or digit, such as --url or --hw-target.
   This also covers a C prefix decrement, which is unavoidable in the language.

Everything else that contains two or more consecutive hyphens fails, including
a postfix decrement in C, which this codebase therefore does not use.

Exit status is zero when clean and one when any defect is found, with each
defect printed as path:line: reason.
"""

import re
import sys
from pathlib import Path

REPOSITORY_ROOT = Path(__file__).resolve().parent.parent

EXCLUDED_DIRECTORY_NAMES = {".git", ".ufbt", "build", "dist", "__pycache__"}

TEXT_FILE_SUFFIXES = {
    ".c",
    ".h",
    ".md",
    ".py",
    ".yml",
    ".yaml",
    ".fam",
    ".txt",
    ".json",
    ".toml",
    ".env",
    ".sh",
    ".cmd",
}
TEXT_FILE_NAMES = {"Makefile", "LICENSE", ".gitignore", ".env", ".gitattributes"}

# Written as escapes so this file passes its own scan.
EM_DASH = "\u2014"
EN_DASH = "\u2013"

TABLE_SEPARATOR_ROW = re.compile(r"^\s*\|?[\s:|-]+\|?\s*$")
HYPHEN_RUN = re.compile(r"-{2,}")


def is_text_file(path: Path) -> bool:
    return path.suffix in TEXT_FILE_SUFFIXES or path.name in TEXT_FILE_NAMES


def iter_text_files(root: Path):
    for path in sorted(root.rglob("*")):
        if any(part in EXCLUDED_DIRECTORY_NAMES for part in path.relative_to(root).parts):
            continue
        if path.is_file() and is_text_file(path):
            yield path


def hyphen_run_is_a_flag(line: str, match: re.Match) -> bool:
    starts_a_token = match.start() == 0 or line[match.start() - 1].isspace() or line[match.start() - 1] in "\"'`([=,"
    followed_by_word = match.end() < len(line) and (line[match.end()].isalnum() or line[match.end()] == "_")
    return starts_a_token and followed_by_word


def defects_in_line(line: str):
    if EM_DASH in line:
        yield "em dash"
    if EN_DASH in line:
        yield "en dash"
    if TABLE_SEPARATOR_ROW.match(line):
        return
    for match in HYPHEN_RUN.finditer(line):
        if not hyphen_run_is_a_flag(line, match):
            yield f"hyphen run '{match.group(0)}' outside a flag or table row"


def main() -> int:
    defect_count = 0
    for path in iter_text_files(REPOSITORY_ROOT):
        try:
            content = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            print(f"{path.relative_to(REPOSITORY_ROOT)}: not valid UTF-8")
            defect_count += 1
            continue
        for line_number, line in enumerate(content.splitlines(), start=1):
            for reason in defects_in_line(line):
                print(f"{path.relative_to(REPOSITORY_ROOT)}:{line_number}: {reason}")
                defect_count += 1
    if defect_count:
        print(f"{defect_count} typography defect(s) found")
        return 1
    print("typography scan clean")
    return 0


if __name__ == "__main__":
    sys.exit(main())
