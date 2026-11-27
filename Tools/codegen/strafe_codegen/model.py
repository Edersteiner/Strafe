"""What the parser finds in headers. Generators read only these types."""

from __future__ import annotations

from dataclasses import dataclass, field


@dataclass(frozen=True)
class Location:
    path: str  # Relative to the project root, with '/' separators.
    line: int
    column: int  # In bytes, from 1, like the compilers.
    length: int = 1  # In bytes, for underlining the whole span.

    def text(self) -> str:
        return f"{self.path}:{self.line}:{self.column}"

    @staticmethod
    def at_offset(path: str, source: bytes, offset: int, length: int = 1) -> Location:
        line = source.count(b"\n", 0, offset) + 1
        column = offset - (source.rfind(b"\n", 0, offset) + 1) + 1
        return Location(path, line, column, length)


@dataclass(frozen=True)
class Argument:
    name: str | None  # None for a positional argument.
    value: str  # The argument's source text.


@dataclass(frozen=True)
class Attribute:
    name: str
    namespace: str | None
    arguments: tuple[Argument, ...]
    location: Location
    node_id: int  # Identifies the parse node, to report attributes nothing used.

    def argument(self, name: str) -> str | None:
        return next((a.value for a in self.arguments if a.name == name), None)


@dataclass
class Field:
    name: str
    type: str  # Source text, for messages and plugins. The compiler does the real typing.
    access: str  # "public", "protected" or "private".
    attributes: list[Attribute]
    location: Location
    is_static: bool = False
    is_reference: bool = False
    is_bitfield: bool = False


@dataclass
class Enumerator:
    name: str
    value: str | None  # Source text of an explicit value.
    attributes: list[Attribute]
    location: Location


@dataclass
class Record:
    """A struct, class or union."""

    kind: str  # "struct", "class" or "union".
    name: str
    scope: list[str]  # Enclosing namespaces and classes, outermost first.
    attributes: list[Attribute]
    fields: list[Field]
    location: Location
    end_offset: int  # Byte offset just past the definition.
    is_template: bool = False

    @property
    def qualified_name(self) -> str:
        return "::".join([*self.scope, self.name])


@dataclass
class Enum:
    name: str
    scope: list[str]
    is_scoped: bool
    underlying_type: str | None
    attributes: list[Attribute]
    enumerators: list[Enumerator]
    location: Location
    end_offset: int

    @property
    def qualified_name(self) -> str:
        return "::".join([*self.scope, self.name])


@dataclass(frozen=True)
class Parameter:
    name: str | None  # None when the declaration leaves it out.
    has_default: bool


@dataclass
class Function:
    name: str
    scope: list[str]  # Enclosing namespaces and classes, outermost first.
    attributes: list[Attribute]
    parameters: list[Parameter]
    help: str  # The comment directly above, as one line.
    location: Location
    access: str  # "public" outside a class.
    is_member: bool  # A member that needs an object.
    class_has_base: bool  # Whether that member's class derives from another.
    is_template: bool = False

    @property
    def qualified_name(self) -> str:
        return "::".join([*self.scope, self.name])


@dataclass
class Header:
    path: str  # Relative to the project root.
    source_include_path: str  # How the header itself is included.
    include_path: str  # How the header's generated companion is included.
    text: str
    records: list[Record] = field(default_factory=list)
    enums: list[Enum] = field(default_factory=list)
    functions: list[Function] = field(default_factory=list)


@dataclass(frozen=True)
class KnownAttributes:
    """The names the generators read, by what they can stand on."""

    types: frozenset[str]  # Records, enums, fields and enumerators.
    functions: frozenset[str]

    @property
    def all(self) -> frozenset[str]:
        return self.types | self.functions


@dataclass
class Module:
    """Every parsed header of one build target."""

    target: str
    headers: list[Header]
