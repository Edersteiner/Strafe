"""Keeps Zed's project settings opening every [[file]] type as JSON."""

from __future__ import annotations

import re
from pathlib import Path

from .generators.reflect import FILE_TYPES

_LANGUAGE = "JSON"
# Zed's settings allow comments, so the file is edited as text rather than
# parsed, which would lose them. Only this one entry is ever rewritten.
_ENTRY = re.compile(r'"' + _LANGUAGE + r'"\s*:\s*\[[^\]]*\]')
_FILE_TYPES = re.compile(r'"file_types"\s*:\s*\{')


def update_file_types(settings: Path, generated_root: Path) -> bool:
    """Points the JSON entry of file_types at the extensions every target lists,
    adding the entry when needed. Returns whether the settings changed."""
    extensions = sorted(
        {line.strip() for path in generated_root.glob(f"*/{FILE_TYPES}") for line in path.read_text("utf-8").split()}
    )
    text = settings.read_text("utf-8") if settings.is_file() else "{\n}\n"
    entry = f'"{_LANGUAGE}": [' + ", ".join(f'"**/*.{extension}"' for extension in extensions) + "]"

    if _ENTRY.search(text):
        updated = _ENTRY.sub(lambda _: entry, text, count=1)
    elif not extensions:
        return False
    else:
        comment = "// Engine file types, kept up to date by the codegen."
        file_types = _FILE_TYPES.search(text)
        if file_types:
            updated = _insert(text, file_types.end(), [comment, entry], indent="    ")
        else:
            opening = text.find("{")
            if opening < 0:
                return False
            lines = [comment, '"file_types": {', f"  {entry}", "}"]
            updated = _insert(text, opening + 1, lines, indent="  ")

    if updated == text:
        return False
    settings.parent.mkdir(parents=True, exist_ok=True)
    with settings.open("w", encoding="utf-8", newline="\n") as stream:
        _ = stream.write(updated)
    return True


def _insert(text: str, offset: int, lines: list[str], indent: str) -> str:
    """Inserts lines as the first members of the object opened just before
    offset, with a comma when members follow."""
    rest = text[offset:]
    has_members = not re.match(r"\s*\}", _strip_comments(rest))
    block = "".join(f"\n{indent}{line}" for line in lines)
    if has_members:
        block += ","
    return text[:offset] + block + rest


def _strip_comments(text: str) -> str:
    return re.sub(r"//[^\n]*|/\*.*?\*/", "", text, flags=re.DOTALL)
