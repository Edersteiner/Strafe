"""Generator plugins.

A generator is a module with four names:

    NAME        a short identifier, used in messages
    ATTRIBUTES  the attribute names it reads on types and their members, so the
                parser keeps them and doesn't report them as unknown
    FUNCTION_ATTRIBUTES
                the same for functions
    generate(module, output, diagnostics)
                reads the model and adds sections and files to output

Built-in generators run first, in the order below, then those from any extra
folder, sorted by file name, so the output never depends on discovery order.
"""

from __future__ import annotations

import importlib.util
from pathlib import Path
from typing import Protocol, cast

from ..diagnostics import Diagnostics
from ..model import Module
from ..output import Output
from . import console, reflect


class Generator(Protocol):
    # Read-only, so a module's plain NAME = "..." matches.
    @property
    def NAME(self) -> str: ...
    @property
    def ATTRIBUTES(self) -> frozenset[str]: ...
    @property
    def FUNCTION_ATTRIBUTES(self) -> frozenset[str]: ...

    def generate(self, module: Module, output: Output, diagnostics: Diagnostics) -> None: ...


BUILTIN: list[Generator] = [reflect, console]


def load(extra_folders: list[Path]) -> list[Generator]:
    generators = list(BUILTIN)
    for folder in extra_folders:
        for path in sorted(folder.glob("*.py")):
            if path.name.startswith("_"):
                continue
            spec = importlib.util.spec_from_file_location(f"strafe_codegen_plugin_{path.stem}", path)
            if spec is None or spec.loader is None:
                raise RuntimeError(f"Can't load generator {path}")
            plugin = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(plugin)
            for name in ("NAME", "ATTRIBUTES", "FUNCTION_ATTRIBUTES", "generate"):
                if not hasattr(plugin, name):
                    raise RuntimeError(f"Generator {path} has no {name}")
            generators.append(cast(Generator, cast(object, plugin)))
    return generators
