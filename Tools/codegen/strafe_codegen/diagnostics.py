"""Errors and warnings laid out like clang's"""

from __future__ import annotations

import os
import sys
from dataclasses import dataclass, field
from typing import TextIO

from .model import Location

_RESET = "\033[0m"
_BOLD = "\033[1m"
_STYLES = {"error": "\033[1;31m", "warning": "\033[1;35m", "note": "\033[1;36m"}
_GREEN = "\033[1;32m"
_TAB_WIDTH = 8


@dataclass
class Diagnostic:
    severity: str  # "error", "warning" or "note".
    location: Location
    message: str
    # Text shown under the marker, such as a corrected name or a line to add.
    fixit: str | None = None
    notes: tuple[Diagnostic, ...] = ()


def note(location: Location, message: str, fixit: str | None = None) -> Diagnostic:
    """Extra context printed under an error or warning."""
    return Diagnostic("note", location, message, fixit)


@dataclass
class Diagnostics:
    errors: int = 0
    warnings: int = 0
    _entries: list[Diagnostic] = field(default_factory=list)
    _sources: dict[str, list[str]] = field(default_factory=dict)

    def add_source(self, path: str, text: str) -> None:
        """Lets diagnostics in this file show the line they point at."""
        self._sources[path] = text.splitlines()

    def error(
        self, location: Location, message: str, fixit: str | None = None, notes: tuple[Diagnostic, ...] = ()
    ) -> None:
        self.errors += 1
        self._entries.append(Diagnostic("error", location, message, fixit, notes))

    def warning(
        self, location: Location, message: str, fixit: str | None = None, notes: tuple[Diagnostic, ...] = ()
    ) -> None:
        self.warnings += 1
        self._entries.append(Diagnostic("warning", location, message, fixit, notes))

    def flush(self, stream: TextIO = sys.stderr) -> None:
        color = _use_color(stream)
        # Sorted by position, so each file reads top to bottom however the
        # parser and generators happened to find things.
        for entry in sorted(self._entries, key=lambda d: (d.location.path, d.location.line, d.location.column)):
            for diagnostic in (entry, *entry.notes):
                _ = stream.write(self._render(diagnostic, color))
        if self._entries:
            _ = stream.write(_summary(self.warnings, self.errors) + "\n")
        self._entries.clear()

    def _render(self, diagnostic: Diagnostic, color: bool) -> str:
        def paint(style: str, text: str) -> str:
            return f"{style}{text}{_RESET}" if color else text

        location = diagnostic.location
        lines = [
            paint(_BOLD, f"{location.text()}: ")
            + paint(_STYLES[diagnostic.severity], f"{diagnostic.severity}: ")
            + paint(_BOLD, diagnostic.message)
        ]
        source = self._sources.get(location.path)
        # One line past the end is allowed, for text to add at the end of a file.
        if source is not None and 1 <= location.line <= len(source) + 1:
            text = source[location.line - 1] if location.line <= len(source) else ""
            start, width = _display_span(text, location.column, location.length)
            gutter = " " * 5 + " | "
            lines.append(f"{location.line:>5} | {text.expandtabs(_TAB_WIDTH)}")
            lines.append(gutter + " " * start + paint(_GREEN, "^" + "~" * (width - 1)))
            if diagnostic.fixit is not None:
                lines.append(gutter + " " * start + paint(_GREEN, diagnostic.fixit))
        elif diagnostic.fixit is not None:
            lines.append(" " * 5 + " | " + paint(_GREEN, diagnostic.fixit))
        return "\n".join(lines) + "\n"


def _display_span(text: str, column: int, length: int) -> tuple[int, int]:
    encoded = text.encode("utf-8")
    before = encoded[: column - 1].decode("utf-8", errors="ignore")
    span = encoded[column - 1 : column - 1 + length].decode("utf-8", errors="ignore")
    start = len(before.expandtabs(_TAB_WIDTH))
    width = len((before + span).expandtabs(_TAB_WIDTH)) - start
    return start, max(width, 1)


def _summary(warnings: int, errors: int) -> str:
    parts: list[str] = []
    if warnings:
        parts.append(f"{warnings} warning{'s' if warnings != 1 else ''}")
    if errors:
        parts.append(f"{errors} error{'s' if errors != 1 else ''}")
    return " and ".join(parts) + " generated."


def _use_color(stream: TextIO) -> bool:
    if os.environ.get("NO_COLOR"):
        return False
    if os.environ.get("CLICOLOR_FORCE", "0") != "0" or os.environ.get("FORCE_COLOR"):
        return True
    return stream.isatty()
