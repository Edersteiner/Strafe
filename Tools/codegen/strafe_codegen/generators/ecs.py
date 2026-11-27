"""Registers every [[component]] struct and class with the ECS, so files and the
console can attach it by name. The reflect generator emits its TypeInfo.

Registers every [[system]] function with the schedule. It runs in the update
stage unless it says [[system(stage = "render")]], and before = "name" or
after = {"a", "b"} order it against other systems of its stage. Names and
order are checked at startup, when the engine's and the game's systems are
known together.
"""

from __future__ import annotations

import difflib
import json
import re

from ..diagnostics import Diagnostics, note
from ..model import Attribute, Function, Module
from ..output import Output

NAME = "ecs"
ATTRIBUTES = frozenset({"component"})
FUNCTION_ATTRIBUTES = frozenset({"system"})

_STAGES = {"update": "Update", "render": "Render"}
_ARGUMENTS = ("stage", "before", "after")
_NAME = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
_STRING = re.compile(r'"([^"\\]*)"')


def generate(module: Module, output: Output, diagnostics: Diagnostics) -> None:
    systems: dict[str, Function] = {}
    for header in module.headers:
        for record in header.records:
            attributes = [a for a in record.attributes if a.name == "component"]
            if not attributes:
                continue
            if any(a.arguments for a in attributes):
                diagnostics.error(attributes[0].location, "'component' takes no arguments")
                continue
            output.register_component(header, f"registry.add<::{record.qualified_name}>();")

        for function in header.functions:
            attribute = next((a for a in function.attributes if a.name == "system"), None)
            if attribute is None or not _validate(function, diagnostics):
                continue
            options = _options(function, attribute, diagnostics)
            if options is None:
                continue
            other = systems.setdefault(function.name, function)
            if other is not function:
                diagnostics.error(
                    function.location,
                    f"there is already a system named '{function.name}'",
                    notes=(note(other.location, f"'{other.qualified_name}' has that name"),),
                )
                continue
            output.register_system(header, _registration(function, *options))


def _validate(function: Function, diagnostics: Diagnostics) -> bool:
    problem = None
    if function.is_template:
        problem = "is a template"
    elif function.is_member:
        problem = "is a member that needs an object; systems are free functions"
    elif function.access != "public":
        problem = f"is {function.access}"
    elif "(anonymous namespace)" in function.scope:
        problem = "is in an anonymous namespace, which other files can't name"
    if problem is not None:
        diagnostics.error(function.location, f"'{function.name}' can't be a system because it {problem}")
    return problem is None


def _options(
    function: Function, attribute: Attribute, diagnostics: Diagnostics
) -> tuple[str, list[str], list[str]] | None:
    """The stage and the before and after names, or None after reporting a problem."""
    stage: str | None = None
    order: dict[str, list[str]] = {"before": [], "after": []}
    ok = True
    for argument in attribute.arguments:
        if argument.name not in _ARGUMENTS:
            what = f"'{argument.name}'" if argument.name else f"'{argument.value}' without a name"
            suggestion = difflib.get_close_matches(argument.name or "", _ARGUMENTS, n=1)
            hint = f"; did you mean '{suggestion[0]}'?" if suggestion else ""
            diagnostics.error(attribute.location, f"'system' takes stage, before and after, not {what}{hint}")
            ok = False
        elif argument.name == "stage":
            stage = _stage(stage, argument.value, attribute, diagnostics)
            ok = ok and stage is not None
        else:
            names = _names(argument.value)
            if names is None:
                diagnostics.error(
                    attribute.location,
                    f'\'{argument.name}\' takes a system name in quotes, or a list like {{"a", "b"}}',
                )
                ok = False
            elif function.name in names:
                diagnostics.error(attribute.location, f"'{function.name}' can't run {argument.name} itself")
                ok = False
            else:
                order[argument.name] += names
    if not ok:
        return None
    return _STAGES[stage or "update"], order["before"], order["after"]


def _stage(previous: str | None, text: str, attribute: Attribute, diagnostics: Diagnostics) -> str | None:
    match = _STRING.fullmatch(text)
    value = match.group(1) if match else None
    if previous is not None:
        diagnostics.error(attribute.location, "'stage' is given twice")
        return None
    if value not in _STAGES:
        suggestion = difflib.get_close_matches(value or text.strip('"'), list(_STAGES), n=1)
        hint = f"; did you mean '{suggestion[0]}'?" if suggestion else ""
        stages = " or ".join(f'"{name}"' for name in _STAGES)
        diagnostics.error(attribute.location, f"'stage' is {stages}, not {text}{hint}")
        return None
    return value


def _names(text: str) -> list[str] | None:
    """The names in "a" or {"a", "b"}, or None when the text is neither."""
    text = text.strip()
    if text.startswith("{") and text.endswith("}"):
        parts = [part.strip() for part in text[1:-1].split(",") if part.strip()]
    else:
        parts = [text]
    names: list[str] = []
    for part in parts:
        match = _STRING.fullmatch(part)
        if match is None or not _NAME.fullmatch(match.group(1)):
            return None
        names.append(match.group(1))
    return names


def _registration(function: Function, stage: str, before: list[str], after: list[str]) -> str:
    def strings(values: list[str]) -> str:
        return "{" + ", ".join(json.dumps(value) for value in values) + "}"

    fields = [
        f".name = {json.dumps(function.name)}",
        f".help = {json.dumps(function.help, ensure_ascii=False)}",
        f".source = {json.dumps(f'{function.location.path}:{function.location.line}')}",
        f".stage = ecs::Stage::{stage}",
    ]
    if before:
        fields.append(f".before = {strings(before)}")
    if after:
        fields.append(f".after = {strings(after)}")
    body = "".join(f"\n      {field}," for field in fields)
    return f"registry.add(ecs::makeSystem<&::{function.qualified_name}>({{{body}\n  }}));"
