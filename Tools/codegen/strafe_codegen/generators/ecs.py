"""Registers every [[component]] struct and class with the ECS, so files and the
console can attach it by name. The reflect generator emits its TypeInfo.
"""

from __future__ import annotations

from ..diagnostics import Diagnostics
from ..model import Module
from ..output import Output

NAME = "ecs"
ATTRIBUTES = frozenset({"component"})
FUNCTION_ATTRIBUTES: frozenset[str] = frozenset()


def generate(module: Module, output: Output, diagnostics: Diagnostics) -> None:
    for header in module.headers:
        for record in header.records:
            attributes = [a for a in record.attributes if a.name == "component"]
            if not attributes:
                continue
            if any(a.arguments for a in attributes):
                diagnostics.error(attributes[0].location, "'component' takes no arguments")
                continue
            output.register_component(header, f"registry.add<::{record.qualified_name}>();")
