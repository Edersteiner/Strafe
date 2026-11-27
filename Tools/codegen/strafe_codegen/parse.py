"""Turns a header into model types with tree-sitter.

tree-sitter gives a syntax tree and nothing more: no macro expansion, no
includes, no types. That is enough, because generated code only names things
and the C++ compiler does the typing.
"""

from __future__ import annotations

import dataclasses
import re
from dataclasses import dataclass

import tree_sitter
import tree_sitter_cpp

from .diagnostics import Diagnostics
from .model import (
    Argument,
    Attribute,
    Enum,
    Enumerator,
    Field,
    Function,
    Header,
    KnownAttributes,
    Location,
    Parameter,
    Record,
)

_LANGUAGE = tree_sitter.Language(tree_sitter_cpp.language())

_RECORD_NODES = {"struct_specifier": "struct", "class_specifier": "class", "union_specifier": "union"}
_DECLARATOR_WRAPPERS = {
    "pointer_declarator",
    "reference_declarator",
    "array_declarator",
    "parenthesized_declarator",
    "attributed_declarator",
}

# tree-sitter-cpp 0.23 can't parse attributes after the enum key, as in
# `enum class [[reflect]] Mode`. They are found by this pattern instead, then
# blanked out so the enum parses and every line and column stays put.
_ENUM_ATTRIBUTES = re.compile(rb"\benum\b(?:\s+(?:class|struct)\b)?\s*((?:\[\[.*?\]\]\s*)+)", re.DOTALL)
# Attributes after an enumerator's name, as in `Count [[skip]]`, don't parse
# either. They are cut out the same way, inside every enum body these find.
_ENUM_HEAD = re.compile(
    rb"\benum\b(?:\s+(?:class|struct)\b)?(?:\s*\[\[.*?\]\])*\s*[A-Za-z_]\w*\s*(?::[^{;]*)?\{", re.DOTALL
)
_ENUMERATOR_ATTRIBUTES = re.compile(rb"([A-Za-z_]\w*)\s*((?:\[\[.*?\]\]\s*)+)", re.DOTALL)

# Attributes the standard defines. Any other attribute without a namespace is
# either ours or a typo of ours.
STANDARD_ATTRIBUTES = frozenset(
    {
        "assume",
        "carries_dependency",
        "deprecated",
        "fallthrough",
        "indeterminate",
        "likely",
        "maybe_unused",
        "no_unique_address",
        "nodiscard",
        "noreturn",
        "unlikely",
    }
)


_NONE_KNOWN = KnownAttributes(frozenset(), frozenset())

_FUNCTION_NODES = ("declaration", "field_declaration", "function_definition")


def _parser() -> tree_sitter.Parser:
    return tree_sitter.Parser(_LANGUAGE)


@dataclass
class _Context:
    header: Header
    source: bytes
    diagnostics: Diagnostics
    known: KnownAttributes
    enum_attributes: dict[int, list[Attribute]]  # By the byte offset of the `enum` keyword.
    enumerator_attributes: dict[int, list[Attribute]]  # By the byte offset of the enumerator's name.
    consumed: set[int]  # node_ids of our attributes that landed on something modelled.

    def location(self, node: tree_sitter.Node) -> Location:
        row, column = node.start_point
        return Location(self.header.path, row + 1, column + 1, node.end_byte - node.start_byte)

    def location_at(self, offset: int, length: int = 1) -> Location:
        return Location.at_offset(self.header.path, self.source, offset, length)

    def text(self, node: tree_sitter.Node | None) -> str:
        if node is None:
            return ""
        return self.source[node.start_byte : node.end_byte].decode("utf-8")


def parse_header(header: Header, known: KnownAttributes, diagnostics: Diagnostics) -> None:
    """Fills the header with every function that carries one of our attributes,
    and every type that does, on itself or on a member."""
    source = header.text.encode("utf-8")
    enum_attributes: dict[int, list[Attribute]] = {}
    enumerator_attributes: dict[int, list[Attribute]] = {}
    blanked = bytearray(source)

    def blank(start: int, end: int) -> None:
        for i in range(start, end):
            if blanked[i] not in b"\r\n":
                blanked[i] = ord(" ")

    for match in _ENUM_ATTRIBUTES.finditer(source):
        start, end = match.span(1)
        enum_attributes[match.start()] = _parse_detached_attributes(source[start:end], start, header, source)
        blank(start, end)
    for head in _ENUM_HEAD.finditer(source):
        body_start = head.end()
        body_end = _closing_brace(source, body_start)
        for match in _ENUMERATOR_ATTRIBUTES.finditer(source, body_start, body_end):
            start, end = match.span(2)
            enumerator_attributes[match.start(1)] = _parse_detached_attributes(source[start:end], start, header, source)
            blank(start, end)

    tree = _parser().parse(bytes(blanked))
    context = _Context(header, source, diagnostics, known, enum_attributes, enumerator_attributes, set())
    _walk(context, tree.root_node, [], is_template=False)
    _check_attributes(context, tree.root_node)


def _closing_brace(source: bytes, start: int) -> int:
    """The offset of the brace closing the one just before start, or the end of
    the source."""
    depth = 1
    for i in range(start, len(source)):
        if source[i] == ord("{"):
            depth += 1
        elif source[i] == ord("}"):
            depth -= 1
            if depth == 0:
                return i
    return len(source)


def _parse_detached_attributes(text: bytes, offset: int, header: Header, source: bytes) -> list[Attribute]:
    """Parses attribute text cut out of an enum head, keeping its real location."""
    prefix = b"struct "
    snippet = prefix + text + b" X {};"
    tree = _parser().parse(snippet)
    # Text comes from the snippet, locations from the real header.
    snippet_context = _Context(header, snippet, Diagnostics(), _NONE_KNOWN, {}, {}, set())
    header_context = _Context(header, source, Diagnostics(), _NONE_KNOWN, {}, {}, set())
    attributes: list[Attribute] = []
    for node in _descendants(tree.root_node, "attribute"):
        real_offset = offset + node.start_byte - len(prefix)
        location = header_context.location_at(real_offset, node.end_byte - node.start_byte)
        attributes.append(_attribute(snippet_context, node, location, node_id=real_offset))
    return attributes


def _walk(context: _Context, node: tree_sitter.Node, scope: list[str], is_template: bool) -> None:
    for child in node.named_children:
        kind = child.type
        if kind == "namespace_definition":
            name = child.child_by_field_name("name")
            names = context.text(name).replace(" ", "").split("::") if name else ["(anonymous namespace)"]
            body = child.child_by_field_name("body")
            if body is not None:
                _walk(context, body, [*scope, *names], is_template=False)
        elif kind == "linkage_specification":
            body = child.child_by_field_name("body")
            if body is not None:
                _walk(context, body, scope, is_template)
        elif kind == "template_declaration":
            _walk(context, child, scope, is_template=True)
        elif kind in _RECORD_NODES or kind == "enum_specifier":
            _type(context, child, scope, is_template)
        elif kind in ("declaration", "type_definition", "function_definition"):
            type_node = child.child_by_field_name("type")
            if type_node is not None and (type_node.type in _RECORD_NODES or type_node.type == "enum_specifier"):
                _type(context, type_node, scope, is_template)
            if kind in _FUNCTION_NODES:
                _function(context, child, scope, "public", is_template, record=None)
        elif kind.startswith("preproc_") or kind == "declaration_list":
            _walk(context, child, scope, is_template)


def _type(context: _Context, node: tree_sitter.Node, scope: list[str], is_template: bool) -> None:
    body = node.child_by_field_name("body")
    name_node = node.child_by_field_name("name")
    if body is None or name_node is None:
        return  # A forward declaration or an anonymous type.
    if node.type == "enum_specifier":
        _enum(context, node, name_node, body, scope)
    else:
        _record(context, node, name_node, body, scope, is_template)


def _record(
    context: _Context,
    node: tree_sitter.Node,
    name_node: tree_sitter.Node,
    body: tree_sitter.Node,
    scope: list[str],
    is_template: bool,
) -> None:
    kind = _RECORD_NODES[node.type]
    name = context.text(name_node)
    record = Record(
        kind=kind,
        name=name,
        scope=scope,
        attributes=_own_attributes(context, node, context.known.types),
        fields=[],
        location=context.location(name_node),
        end_offset=node.end_byte,
        is_template=is_template or name_node.type == "template_type",
    )
    inner_scope = [*scope, name]
    access = "private" if kind == "class" else "public"
    for member in body.named_children:
        if member.type == "access_specifier":
            access = context.text(member).rstrip(":").strip()
        elif member.type == "field_declaration":
            type_node = member.child_by_field_name("type")
            if type_node is not None and (type_node.type in _RECORD_NODES or type_node.type == "enum_specifier"):
                _type(context, type_node, inner_scope, is_template=False)
            record.fields.extend(_fields(context, member, access))
            _function(context, member, inner_scope, access, is_template, record=node)
        elif member.type in ("declaration", "type_definition"):
            type_node = member.child_by_field_name("type")
            if type_node is not None and (type_node.type in _RECORD_NODES or type_node.type == "enum_specifier"):
                _type(context, type_node, inner_scope, is_template=False)
        elif member.type == "function_definition":
            _function(context, member, inner_scope, access, is_template, record=node)
        elif member.type in _RECORD_NODES or member.type == "enum_specifier":
            _type(context, member, inner_scope, is_template=False)
        elif member.type == "template_declaration":
            _walk(context, member, inner_scope, is_template=True)

    if record.attributes or any(f.attributes for f in record.fields):
        context.header.records.append(record)
        _consume(context, record.attributes)
        for field in record.fields:
            _consume(context, field.attributes)


def _fields(context: _Context, node: tree_sitter.Node, access: str) -> list[Field]:
    declarators = node.children_by_field_name("declarator")
    if not declarators:
        return []
    names: list[tuple[tree_sitter.Node, bool]] = []
    for declarator in declarators:
        name, is_reference, is_function = _unwrap_declarator(declarator)
        if is_function:
            return []  # A member function.
        if name is not None:
            names.append((name, is_reference))

    is_static = any(c.type == "storage_class_specifier" and context.text(c) == "static" for c in node.children)
    is_bitfield = any(c.type == "bitfield_clause" for c in node.children)
    qualifiers = [context.text(c) for c in node.children if c.type == "type_qualifier"]
    type_text = " ".join([*qualifiers, context.text(node.child_by_field_name("type"))])
    attributes = _own_attributes(context, node, context.known.types)
    return [
        Field(
            name=context.text(name),
            type=type_text,
            access=access,
            attributes=attributes,
            location=context.location(name),
            is_static=is_static,
            is_reference=is_reference,
            is_bitfield=is_bitfield,
        )
        for name, is_reference in names
    ]


def _unwrap_declarator(node: tree_sitter.Node) -> tuple[tree_sitter.Node | None, bool, bool]:
    """Returns the declared name, whether it is a reference, and whether it is a function."""
    is_reference = False
    while node.type in _DECLARATOR_WRAPPERS:
        is_reference = is_reference or node.type == "reference_declarator"
        inner = node.child_by_field_name("declarator")
        if inner is None:
            inner = next((c for c in node.named_children if c.type != "attribute_declaration"), None)
        if inner is None:
            return None, is_reference, False
        node = inner
    if node.type == "function_declarator":
        return None, is_reference, True
    if node.type == "field_identifier":
        return node, is_reference, False
    return None, is_reference, False


def _function(
    context: _Context,
    node: tree_sitter.Node,
    scope: list[str],
    access: str,
    is_template: bool,
    record: tree_sitter.Node | None,
) -> None:
    attributes = _own_attributes(context, node, context.known.functions)
    declarator = node.child_by_field_name("declarator")
    while declarator is not None and declarator.type in _DECLARATOR_WRAPPERS:
        inner = declarator.child_by_field_name("declarator")
        # A reference declarator doesn't name the one it wraps.
        declarator = inner or next((c for c in declarator.named_children if c.type.endswith("declarator")), None)
    if not attributes or declarator is None or declarator.type != "function_declarator":
        return
    name = declarator.child_by_field_name("declarator")
    parameters = declarator.child_by_field_name("parameters")
    if name is None or parameters is None or name.type not in ("identifier", "field_identifier"):
        return

    is_static = any(c.type == "storage_class_specifier" and context.text(c) == "static" for c in node.children)
    context.header.functions.append(
        Function(
            name=context.text(name),
            scope=scope,
            attributes=attributes,
            parameters=_parameters(context, parameters),
            help=_comment_above(context, node),
            location=context.location(name),
            access=access,
            is_member=record is not None and not is_static,
            class_has_base=record is not None and any(c.type == "base_class_clause" for c in record.children),
            is_template=is_template,
        )
    )
    _consume(context, attributes)


def _parameters(context: _Context, node: tree_sitter.Node) -> list[Parameter]:
    found: list[Parameter] = []
    for child in node.named_children:
        if child.type not in ("parameter_declaration", "optional_parameter_declaration"):
            continue
        declarator = child.child_by_field_name("declarator")
        if declarator is None and context.text(child) == "void":
            continue
        name = None
        if declarator is not None:
            name = next(_descendants(declarator, "identifier"), None)
        found.append(
            Parameter(
                name=context.text(name) if name is not None else None,
                has_default=child.type == "optional_parameter_declaration",
            )
        )
    return found


def _comment_above(context: _Context, node: tree_sitter.Node) -> str:
    """The comment that ends on the line above, joined into one line."""
    lines: list[str] = []
    row = node.start_point[0]
    comment = node.prev_sibling
    while comment is not None and comment.type == "comment" and comment.end_point[0] == row - 1:
        before = comment.prev_sibling
        if before is not None and before.type != "comment" and before.end_point[0] == comment.start_point[0]:
            break  # It trails whatever is before it on the same line.
        row = comment.start_point[0]
        text = context.text(comment)
        if text.startswith("/*"):
            block = [line.strip().lstrip("*").strip() for line in text[2:-2].splitlines()]
            lines[:0] = [line for line in block if line]
        else:
            lines.insert(0, text.lstrip("/").strip())
        comment = before
    return " ".join(line for line in lines if line and not line.startswith("NOLINT"))


def _enum(
    context: _Context, node: tree_sitter.Node, name_node: tree_sitter.Node, body: tree_sitter.Node, scope: list[str]
) -> None:
    attributes = _ours(context.enum_attributes.get(node.start_byte, []), context.known.types)
    is_scoped = any(c.type in ("class", "struct") for c in node.children)
    underlying = node.child_by_field_name("base")
    enumerators: list[Enumerator] = []
    for child in body.named_children:
        name = child.child_by_field_name("name")
        if child.type != "enumerator" or name is None:
            continue
        enumerators.append(
            Enumerator(
                name=context.text(name),
                value=context.text(child.child_by_field_name("value")) or None,
                attributes=_ours(context.enumerator_attributes.get(name.start_byte, []), context.known.types),
                location=context.location(name),
            )
        )
    if not attributes and not any(e.attributes for e in enumerators):
        return
    context.header.enums.append(
        Enum(
            name=context.text(name_node),
            scope=scope,
            is_scoped=is_scoped,
            underlying_type=context.text(underlying) or None,
            attributes=attributes,
            enumerators=enumerators,
            location=context.location(name_node),
            end_offset=node.end_byte,
        )
    )
    _consume(context, attributes)
    for enumerator in enumerators:
        _consume(context, enumerator.attributes)


def _ours(attributes: list[Attribute], names: frozenset[str]) -> list[Attribute]:
    return [a for a in attributes if a.namespace is None and a.name in names]


def _own_attributes(context: _Context, node: tree_sitter.Node, names: frozenset[str]) -> list[Attribute]:
    """The attributes with these names written directly on this declaration."""
    attributes: list[Attribute] = []
    for declaration in (c for c in node.children if c.type == "attribute_declaration"):
        for attribute in (c for c in declaration.named_children if c.type == "attribute"):
            attributes.append(_attribute(context, attribute, context.location(attribute), node_id=attribute.start_byte))
    return _ours(attributes, names)


def _attribute(context: _Context, node: tree_sitter.Node, location: Location, node_id: int) -> Attribute:
    prefix = node.child_by_field_name("prefix")
    name = node.child_by_field_name("name")
    arguments: list[Argument] = []
    argument_list = next((c for c in node.named_children if c.type == "argument_list"), None)
    if argument_list is not None:
        for argument in argument_list.named_children:
            if argument.type == "assignment_expression":
                left = argument.child_by_field_name("left")
                right = argument.child_by_field_name("right")
                arguments.append(Argument(context.text(left), context.text(right)))
            else:
                arguments.append(Argument(None, context.text(argument)))
    return Attribute(
        name=context.text(name),
        namespace=context.text(prefix) or None,
        arguments=tuple(arguments),
        location=location,
        node_id=node_id,
    )


def _consume(context: _Context, attributes: list[Attribute]) -> None:
    context.consumed.update(a.node_id for a in attributes)


def _check_attributes(context: _Context, root: tree_sitter.Node) -> None:
    """Warns about unknown unscoped attributes, ours placed where nothing reads
    them, and parse errors around our attributes."""
    found: list[Attribute] = [
        _attribute(context, node, context.location(node), node_id=node.start_byte)
        for node in _descendants(root, "attribute")
    ]
    for attributes in [*context.enum_attributes.values(), *context.enumerator_attributes.values()]:
        found.extend(attributes)

    for attribute in found:
        if attribute.namespace is not None or attribute.name in STANDARD_ATTRIBUTES:
            continue
        if attribute.name not in context.known.all:
            suggestion = _closest(attribute.name, context.known.all)
            hint = f"; did you mean '{suggestion}'?" if suggestion else ""
            # Unscoped, so the name starts the attribute, and only the name is underlined.
            name = dataclasses.replace(attribute.location, length=len(attribute.name.encode("utf-8")))
            context.diagnostics.warning(name, f"unknown attribute '{attribute.name}'{hint}", fixit=suggestion)
        elif attribute.node_id not in context.consumed:
            context.diagnostics.warning(
                attribute.location, f"'{attribute.name}' has no effect on this kind of declaration"
            )

    names = context.known.all
    for error in _descendants(root, "ERROR"):
        text = context.text(error)
        if "[[" in text and any(name in text for name in names):
            context.diagnostics.warning(
                context.location(error), "this declaration didn't parse, so codegen may have missed it"
            )


def _closest(name: str, candidates: frozenset[str]) -> str | None:
    import difflib

    matches = difflib.get_close_matches(name, sorted(candidates), n=1, cutoff=0.6)
    return matches[0] if matches else None


def _descendants(node: tree_sitter.Node, kind: str):
    stack = [node]
    while stack:
        current = stack.pop()
        if current.type == kind:
            yield current
        stack.extend(reversed(current.children))
