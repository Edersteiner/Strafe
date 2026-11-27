"""Names derived from C++ names."""

from __future__ import annotations

import re


def snake_case(name: str) -> str:
    return re.sub(r"(?<=[a-z0-9])([A-Z])", r"_\1", name).lower()
