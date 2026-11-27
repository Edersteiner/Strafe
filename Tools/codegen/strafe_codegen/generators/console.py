"""Registers every [[command]] function with the console.

The name is the function's in snake_case, or the one in [[command("name")]]. The
comment above the function becomes its help text. A member that isn't static runs
on the game's App and its class must derive from it.
"""

from __future__ import annotations

import json
import re

from ..diagnostics import Diagnostics, note
from ..model import Attribute, Function, Module
from ..naming import snake_case
from ..output import Output

NAME = "console"
ATTRIBUTES: frozenset[str] = frozenset()
FUNCTION_ATTRIBUTES = frozenset({"command"})

_COMMAND = re.compile(r"[a-z0-9_.]+")


def generate(module: Module, output: Output, diagnostics: Diagnostics) -> None:
    commands: dict[str, Function] = {}
    for header in module.headers:
        for function in header.functions:
            attribute = next((a for a in function.attributes if a.name == "command"), None)
            if attribute is None:
                continue
            name = _command_name(function, attribute, diagnostics)
            if name is None or not _validate(function, diagnostics):
                continue
            other = commands.setdefault(name, function)
            if other is not function:
                diagnostics.error(
                    function.location,
                    f"there is already a command named '{name}'",
                    notes=(note(other.location, f"'{other.qualified_name}' has that name"),),
                )
                continue
            output.register_command(header, _registration(name, function))


def _command_name(function: Function, attribute: Attribute, diagnostics: Diagnostics) -> str | None:
    if not attribute.arguments:
        return snake_case(function.name)
    argument = attribute.arguments[0]
    text = argument.value
    name = text[1:-1] if len(text) >= 2 and text[0] == text[-1] == '"' else None
    if len(attribute.arguments) > 1 or argument.name is not None or name is None:
        diagnostics.error(
            attribute.location, "'command' takes at most one name in quotes, like [[command(\"noclip\")]]"
        )
        return None
    if not _COMMAND.fullmatch(name):
        diagnostics.error(
            attribute.location,
            f"'{name}' isn't a valid command name; use lowercase letters, digits, underscores and dots",
        )
        return None
    return name


def _validate(function: Function, diagnostics: Diagnostics) -> bool:
    problem = None
    if function.is_template:
        problem = "is a template"
    elif function.is_member and not function.class_has_base:
        # Whether the base is App is for the compiler to check.
        problem = "needs an object, and only the game's App has one; make it static, or add it with Console::addCommand"
    elif function.access != "public":
        problem = f"is {function.access}"
    elif "(anonymous namespace)" in function.scope:
        problem = "is in an anonymous namespace, which other files can't name"
    if problem is not None:
        diagnostics.error(function.location, f"'{function.name}' can't be a command because it {problem}")
    return problem is None


def _registration(name: str, function: Function) -> str:
    target = f"::{function.qualified_name}"
    defaults = sum(parameter.has_default for parameter in function.parameters)
    names = ", ".join(f'"{parameter.name or ""}"' for parameter in function.parameters)
    adder = "addMember" if function.is_member else "addFunction"
    parameters = "auto &app, auto &&...arguments" if function.is_member else "auto &&...arguments"
    call = f"app.{function.name}" if function.is_member else target
    # The generic lambda calls by name, so default arguments still apply.
    return (
        f"registry.{adder}<{defaults}>(\n"
        f'      "{name}", {json.dumps(function.help, ensure_ascii=False)}, {{{names}}}, &{target},\n'
        f"      []({parameters}) -> decltype(auto) {{\n"
        f"        return {call}(std::forward<decltype(arguments)>(arguments)...);\n"
        f"      }});"
    )
