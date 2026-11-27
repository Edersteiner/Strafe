"""Emits strafe::reflect::TypeInfo<T> for every [[reflect]] struct, class and enum.

[[skip]] leaves a field or enumerator out. [[file]] or [[file("ext")]] makes a
record a file type with that extension, the snake_case type name by default,
and lists it in the target's file_types.txt for editor integration.
"""

from __future__ import annotations

import re

from ..diagnostics import Diagnostics, note
from ..model import Attribute, Enum, Field, Module, Record
from ..naming import snake_case
from ..output import Output

NAME = "reflect"
ATTRIBUTES = frozenset({"reflect", "property", "skip", "file"})
FUNCTION_ATTRIBUTES: frozenset[str] = frozenset()

FILE_TYPES = "file_types.txt"

_INCLUDE = "strafe/core/reflect.h"
_EXTENSION = re.compile(r"[a-z0-9_]+")


def generate(module: Module, output: Output, diagnostics: Diagnostics) -> None:
    extensions: dict[str, Record] = {}
    for header in module.headers:
        sections: list[str] = []
        for record in header.records:
            _check_misplaced(record.attributes, ("skip",), "a struct or class", diagnostics)
            if not _is_reflected(record):
                _check_unreflected(record, diagnostics)
                continue
            extension = _file_extension(record, diagnostics)
            if extension is not None and not _claim_extension(extension, record, extensions, diagnostics):
                extension = None
            if _validate(record, diagnostics):
                sections.append(_record(record, extension))
                output.register_type(header, f"registry.add<::{record.qualified_name}>();")
        for enum in header.enums:
            _check_misplaced(enum.attributes, ("skip", "file"), "an enum", diagnostics)
            if _is_reflected(enum):
                sections.append(_enum(enum))
                output.register_type(header, f"registry.add<::{enum.qualified_name}>();")
            else:
                for enumerator in enum.enumerators:
                    for attribute in _named(enumerator.attributes, "skip"):
                        diagnostics.warning(
                            attribute.location, f"'skip' has no effect because '{enum.name}' isn't [[reflect]]"
                        )
        if sections:
            body = "\n\n".join(sections)
            output.section(
                header, f"namespace strafe::reflect {{\n\n{body}\n\n}} // namespace strafe::reflect", (_INCLUDE,)
            )
    if extensions:
        output.file(FILE_TYPES, "".join(f"{extension}\n" for extension in sorted(extensions)))


def _named(attributes: list[Attribute], name: str) -> list[Attribute]:
    return [a for a in attributes if a.name == name]


def _is_reflected(item: Record | Enum) -> bool:
    return bool(_named(item.attributes, "reflect"))


def _is_skipped(item: Field) -> bool:
    return bool(_named(item.attributes, "skip"))


def _check_misplaced(attributes: list[Attribute], names: tuple[str, ...], kind: str, diagnostics: Diagnostics) -> None:
    for attribute in attributes:
        if attribute.name in names:
            diagnostics.warning(attribute.location, f"'{attribute.name}' has no effect on {kind}")


def _check_unreflected(record: Record, diagnostics: Diagnostics) -> None:
    attributes = [*_named(record.attributes, "file")]
    for field in record.fields:
        attributes += [a for a in field.attributes if a.name in ("property", "skip")]
    for attribute in attributes:
        diagnostics.warning(
            attribute.location, f"'{attribute.name}' has no effect because '{record.name}' isn't [[reflect]]"
        )


def _file_extension(record: Record, diagnostics: Diagnostics) -> str | None:
    attributes = _named(record.attributes, "file")
    if not attributes:
        return None
    attribute = attributes[0]
    if not attribute.arguments:
        return snake_case(record.name)
    argument = attribute.arguments[0]
    text = argument.value
    extension = text[1:-1] if len(text) >= 2 and text[0] == text[-1] == '"' else None
    if len(attribute.arguments) > 1 or argument.name is not None or extension is None:
        diagnostics.error(attribute.location, "'file' takes at most one extension in quotes, like [[file(\"scene\")]]")
        return None
    if not _EXTENSION.fullmatch(extension):
        fixed = re.sub(r"[^a-z0-9_]", "", extension.lower())
        diagnostics.error(
            attribute.location,
            f"'{extension}' isn't a valid file extension; use lowercase letters, digits and underscores, without a dot",
            fixit=f'file("{fixed}")' if fixed else None,
        )
        return None
    return extension


def _claim_extension(extension: str, record: Record, extensions: dict[str, Record], diagnostics: Diagnostics) -> bool:
    other = extensions.get(extension)
    if other is not None:
        diagnostics.error(
            record.location,
            f"'{record.name}' and '{other.name}' both use the file extension '.{extension}'",
            notes=(note(other.location, f"'{other.name}' is declared here"),),
        )
        return False
    extensions[extension] = record
    return True


def _validate(record: Record, diagnostics: Diagnostics) -> bool:
    ok = True
    if record.is_template:
        diagnostics.error(record.location, f"templates can't be [[reflect]] yet ('{record.name}')")
        ok = False
    if record.kind == "union":
        diagnostics.error(record.location, f"unions can't be [[reflect]] ('{record.name}')")
        ok = False
    if any(scope == "(anonymous namespace)" for scope in record.scope):
        diagnostics.error(
            record.location, f"'{record.name}' is in an anonymous namespace, which other files can't name"
        )
        ok = False
    for field in _fields(record):
        problem = None
        if field.access != "public":
            problem = f"is {field.access}, and reflected fields must be public or [[skip]]"
        elif field.is_reference:
            problem = "is a reference, which has no member pointer, so it needs [[skip]]"
        elif field.is_bitfield:
            problem = "is a bit-field, which has no member pointer, so it needs [[skip]]"
        if problem:
            diagnostics.error(field.location, f"field '{field.name}' of '{record.name}' {problem}")
            ok = False
    return ok


def _fields(record: Record) -> list[Field]:
    return [f for f in record.fields if not f.is_static and not _is_skipped(f)]


def _record(record: Record, extension: str | None) -> str:
    type_name = f"::{record.qualified_name}"
    fields = _fields(record)
    lines = [
        "template <>",
        f"struct TypeInfo<{type_name}> {{",
        f"  using Type = {type_name};",
        f'  static constexpr std::string_view name = "{record.qualified_name}";',
    ]
    if extension is not None:
        lines.append(f'  static constexpr std::string_view file_extension = "{extension}";')
    if fields:
        lines.append("  static constexpr auto fields = std::make_tuple(")
        entries = [f'      field("{f.name}", &Type::{f.name})' for f in fields]
        lines.append(",\n".join(entries) + ");")
    else:
        lines.append("  static constexpr auto fields = std::make_tuple();")
    lines.append("};")
    return "\n".join(lines)


def _enum(enum: Enum) -> str:
    type_name = f"::{enum.qualified_name}"
    enumerators = [e for e in enum.enumerators if not _named(e.attributes, "skip")]
    lines = [
        "template <>",
        f"struct TypeInfo<{type_name}> {{",
        f"  using Type = {type_name};",
        f'  static constexpr std::string_view name = "{enum.qualified_name}";',
        f"  static constexpr std::array<Enumerator<Type>, {len(enumerators)}> enumerators{{{{",
    ]
    lines += [f'      {{"{e.name}", Type::{e.name}}},' for e in enumerators]
    lines += ["  }};", "};"]
    return "\n".join(lines)
