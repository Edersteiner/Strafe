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


@dataclass
class Header:
    path: str  # Relative to the project root.
    include_path: str  # How the header's generated companion is included.
    text: str
    records: list[Record] = field(default_factory=list)
    enums: list[Enum] = field(default_factory=list)

    def has_attribute(self, name: str) -> bool:
        return any(any(a.name == name for a in item.attributes) for item in [*self.records, *self.enums])


@dataclass
class Module:
    """Every parsed header of one build target."""

    target: str
    headers: list[Header]
