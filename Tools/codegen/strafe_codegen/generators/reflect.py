"""Emits strafe::reflect::TypeInfo<T> for every [[reflect]] struct, class and enum."""

from __future__ import annotations

from ..diagnostics import Diagnostics
from ..model import Enum, Module, Record
from ..output import Output

NAME = "reflect"
ATTRIBUTES = frozenset({"reflect", "property"})

_INCLUDE = "strafe/core/reflect.h"


def generate(module: Module, output: Output, diagnostics: Diagnostics) -> None:
    for header in module.headers:
        sections: list[str] = []
        for record in header.records:
            if _is_reflected(record):
                if _validate(record, diagnostics):
                    sections.append(_record(record))
                    output.register(header, f"registry.add<::{record.qualified_name}>();")
            else:
                for field in record.fields:
                    for attribute in field.attributes:
                        if attribute.name == "property":
                            diagnostics.warning(
                                attribute.location,
                                f"'property' has no effect because '{record.name}' isn't [[reflect]]",
                            )
        for enum in header.enums:
            if _is_reflected(enum):
                sections.append(_enum(enum))
                output.register(header, f"registry.add<::{enum.qualified_name}>();")
        if sections:
            body = "\n\n".join(sections)
            output.section(
                header, f"namespace strafe::reflect {{\n\n{body}\n\n}} // namespace strafe::reflect", (_INCLUDE,)
            )


def _is_reflected(item: Record | Enum) -> bool:
    return any(a.name == "reflect" for a in item.attributes)


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
            problem = f"is {field.access}, and reflected fields must be public"
        elif field.is_reference:
            problem = "is a reference, which has no member pointer"
        elif field.is_bitfield:
            problem = "is a bit-field, which has no member pointer"
        if problem:
            diagnostics.error(field.location, f"field '{field.name}' of '{record.name}' {problem}")
            ok = False
    return ok


def _fields(record: Record):
    return [f for f in record.fields if not f.is_static]


def _record(record: Record) -> str:
    type_name = f"::{record.qualified_name}"
    fields = _fields(record)
    lines = [
        "template <>",
        f"struct TypeInfo<{type_name}> {{",
        f"  using Type = {type_name};",
        f'  static constexpr std::string_view name = "{record.qualified_name}";',
    ]
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
    lines = [
        "template <>",
        f"struct TypeInfo<{type_name}> {{",
        f"  using Type = {type_name};",
        f'  static constexpr std::string_view name = "{enum.qualified_name}";',
        f"  static constexpr std::array<Enumerator<Type>, {len(enum.enumerators)}> enumerators{{{{",
    ]
    lines += [f'      {{"{e.name}", Type::{e.name}}},' for e in enum.enumerators]
    lines += ["  }};", "};"]
    return "\n".join(lines)
