"""Command line entry point, called by the strafe.codegen xmake rule.

    python -m strafe_codegen --project DIR --target NAME --out DIR
        [--root DIR ...] [--generators DIR ...] --inputs FILE
        --register-function NAME [--zed-settings FILE]

--inputs names a file listing one header path per line, which keeps long
header lists off the command line.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import cast

from . import generators, zed
from .diagnostics import Diagnostics, note
from .model import Header, Location, Module
from .output import Output, write
from .parse import parse_header


@dataclass(frozen=True)
class _Arguments:
    project: Path
    target: str
    out: Path
    roots: list[Path]
    generators: list[Path]
    inputs: Path
    register_function: str
    zed_settings: Path | None


def main() -> int:
    arguments = _arguments()
    project = arguments.project.resolve()
    roots = sorted({r.resolve() for r in arguments.roots}, key=lambda r: len(r.parts), reverse=True)
    plugins = generators.load([g.resolve() for g in arguments.generators])
    known_attributes = frozenset[str]().union(*(g.ATTRIBUTES for g in plugins))
    diagnostics = Diagnostics()

    paths = [Path(line.strip()).resolve() for line in arguments.inputs.read_text("utf-8").splitlines() if line.strip()]
    headers: list[Header] = []
    for path in sorted(set(paths), key=lambda p: _relative(p, project)):
        text = path.read_text("utf-8")
        source_include_path, include_path = _include_paths(path, roots)
        # Attributes are the only way in, so a header with neither attributes nor
        # a companion include is skipped unparsed.
        if "[[" not in text and include_path not in text:
            continue
        header = Header(
            path=_relative(path, project),
            source_include_path=source_include_path,
            include_path=include_path,
            text=text,
        )
        diagnostics.add_source(header.path, text)
        parse_header(header, known_attributes, diagnostics)
        headers.append(header)

    module = Module(target=arguments.target, headers=headers)
    output = Output(arguments.target, arguments.register_function)
    for plugin in plugins:
        plugin.generate(module, output, diagnostics)
    for header in output.headers_with_companions():
        _check_companion_include(header, diagnostics)
    for header in headers:
        _keep_included_companion(header, output, diagnostics)

    diagnostics.flush()
    if diagnostics.errors:
        return 1
    out = arguments.out.resolve()
    changes = write(out, output.render())
    if changes:
        print(f"[codegen] {arguments.target}: {changes} generated file(s) updated")
    # Every target's folder sits next to this one, so the settings cover them all.
    if arguments.zed_settings is not None and zed.update_file_types(arguments.zed_settings, out.parent):
        print(f"[codegen] {arguments.target}: updated the file types in {arguments.zed_settings.name}")
    return 0


def _arguments() -> _Arguments:
    parser = argparse.ArgumentParser(prog="strafe_codegen")
    _ = parser.add_argument("--project", type=Path, required=True, help="the project root, for relative paths")
    _ = parser.add_argument("--target", required=True, help="the build target's name")
    _ = parser.add_argument("--out", type=Path, required=True, help="the target's generated folder")
    _ = parser.add_argument("--root", type=Path, action="append", default=[], help="an include root of the target")
    _ = parser.add_argument("--generators", type=Path, action="append", default=[], help="a folder of extra generators")
    _ = parser.add_argument("--inputs", type=Path, required=True, help="a file listing the headers to read")
    _ = parser.add_argument(
        "--register-function", required=True, help="the name of the target's generated registration function"
    )
    _ = parser.add_argument(
        "--zed-settings", type=Path, default=None, help="Zed project settings to point at the [[file]] types"
    )
    # argparse only knows Any, so the values are typed once here.
    values = cast(dict[str, object], vars(parser.parse_args()))
    return _Arguments(
        project=cast(Path, values["project"]),
        target=cast(str, values["target"]),
        out=cast(Path, values["out"]),
        roots=cast(list[Path], values["root"]),
        generators=cast(list[Path], values["generators"]),
        inputs=cast(Path, values["inputs"]),
        register_function=cast(str, values["register_function"]),
        zed_settings=cast(Path | None, values["zed_settings"]),
    )


def _relative(path: Path, project: Path) -> str:
    try:
        return path.relative_to(project).as_posix()
    except ValueError:
        return path.as_posix()


def _include_paths(path: Path, roots: list[Path]) -> tuple[str, str]:
    relative = next((path.relative_to(root) for root in roots if path.is_relative_to(root)), Path(path.name))
    return relative.as_posix(), relative.with_name(f"{path.stem}.gen.h").as_posix()


def _find_companion_include(header: Header) -> re.Match[str] | None:
    return re.search(r'#\s*include\s*[<"]' + re.escape(header.include_path) + r'[>"]', header.text)


def _keep_included_companion(header: Header, output: Output, diagnostics: Diagnostics) -> None:
    """A header can include its companion while nothing in it is reflected, after
    a typo or after removing [[reflect]]. An empty companion keeps that include
    compiling, so the warning isn't buried under a missing file error."""
    match = _find_companion_include(header)
    if match is None or output.has_companion(header):
        return
    output.ensure_companion(header)
    offset = len(header.text[: match.start()].encode("utf-8"))
    directive = Location.at_offset(
        header.path, header.text.encode("utf-8"), offset, len(match.group(0).encode("utf-8"))
    )
    diagnostics.warning(directive, f"nothing in this header is [[reflect]], so '{header.include_path}' is empty")


def _check_companion_include(header: Header, diagnostics: Diagnostics) -> None:
    """The companion must be included after every type it describes, or the
    compiler sees TypeInfo for types it doesn't know yet."""
    source = header.text.encode("utf-8")
    match = _find_companion_include(header)
    last = max([*header.records, *header.enums], key=lambda item: item.end_offset)
    line = f'#include "{header.include_path}"'
    if match is None:
        # One line past the last, where the include belongs.
        end = Location(header.path, header.text.count("\n") + (0 if header.text.endswith("\n") else 1) + 1, 1)
        diagnostics.error(
            last.location,
            f"'{last.name}' is [[reflect]], but this header never includes its generated companion",
            notes=(note(end, "add it at the end of the file", fixit=line),),
        )
    else:
        offset = len(header.text[: match.start()].encode("utf-8"))
        if offset < last.end_offset:
            directive = Location.at_offset(header.path, source, offset, len(match.group(0).encode("utf-8")))
            diagnostics.error(
                directive,
                "the generated companion is included above the types it describes",
                notes=(note(last.location, f"'{last.name}' is defined here, so the include must come after it"),),
            )


if __name__ == "__main__":
    sys.exit(main())
