#!/usr/bin/env python3
"""Generate a bounded MinGW .def file for the ZeroSlack developer DLL."""

from __future__ import annotations

import argparse
import pathlib
import re
import subprocess
import sys


_EXPORT_PATTERN = re.compile(
    r'^\s*(?P<symbol>"[^"]+"|\S+)\s+@\s+\d+(?P<qualifiers>.*)$'
)

# These are header-defined Qt container implementation templates. A consumer
# instantiates them locally; exporting every copy consumes thousands of PE
# ordinals without exposing a ZeroSlack ABI entry point.
_FILTERED_PREFIXES = (
    "_ZN17QArrayDataPointerI",
    "_ZNK17QArrayDataPointerI",
)

_REQUIRED_EXPORTS = {
    "_ZN12MyCodeEditor16staticMetaObjectE",
    "_ZN23ApplicationThemeManager16staticMetaObjectE",
    "_ZN23ApplicationThemeManager8instanceEv",
}


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dlltool", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument(
        "--object-root",
        action="append",
        required=True,
        type=pathlib.Path,
    )
    return parser.parse_args()


def _keep_symbol(symbol: str) -> bool:
    unquoted = symbol.strip('"')
    if unquoted.startswith((".refptr.", ".weak.")):
        return False
    return not unquoted.startswith(_FILTERED_PREFIXES)


def _object_files(roots: list[pathlib.Path]) -> list[pathlib.Path]:
    objects: set[pathlib.Path] = set()
    for root in roots:
        if root.is_dir():
            objects.update(path.resolve() for path in root.rglob("*.obj"))
    return sorted(objects, key=lambda path: path.as_posix().casefold())


def main() -> int:
    args = _parse_args()
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)

    objects = _object_files(args.object_root)
    if not objects:
        print("no MinGW objects found for export generation", file=sys.stderr)
        return 2

    response = pathlib.Path(f"{output}.rsp")
    raw_def = pathlib.Path(f"{output}.raw")
    response_text = (
        "\n".join(f'"{path.as_posix()}"' for path in objects) + "\n"
    )
    previous_response = (
        response.read_text(encoding="utf-8", errors="replace")
        if response.is_file()
        else ""
    )
    newest_object_mtime = max(path.stat().st_mtime_ns for path in objects)
    if (
        output.is_file()
        and previous_response == response_text
        and output.stat().st_mtime_ns >= newest_object_mtime
    ):
        print(f"ZeroSlack MinGW exports: up to date ({len(objects)} objects)")
        return 0
    response.write_text(response_text, encoding="utf-8")

    command = [
        str(args.dlltool),
        "--export-all-symbols",
        "--output-def",
        str(raw_def),
        f"@{response.as_posix()}",
    ]
    completed = subprocess.run(
        command,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if completed.returncode:
        sys.stderr.write(completed.stdout)
        sys.stderr.write(completed.stderr)
        return completed.returncode

    exports: dict[str, bool] = {}
    for line in raw_def.read_text(encoding="utf-8", errors="replace").splitlines():
        match = _EXPORT_PATTERN.match(line)
        if not match:
            continue
        symbol = match.group("symbol")
        if not _keep_symbol(symbol):
            continue
        qualifiers = match.group("qualifiers").split()
        exports[symbol] = "DATA" in qualifiers

    missing = sorted(_REQUIRED_EXPORTS.difference(exports))
    if missing:
        print(
            "required shared-core exports are missing: " + ", ".join(missing),
            file=sys.stderr,
        )
        return 3
    if not 1_000 <= len(exports) < 65_000:
        print(
            f"unsafe MinGW export count: {len(exports)} (expected 1000..64999)",
            file=sys.stderr,
        )
        return 4

    lines = ["EXPORTS"]
    for symbol in sorted(exports, key=str.casefold):
        suffix = " DATA" if exports[symbol] else ""
        lines.append(f"\t{symbol}{suffix}")
    temporary = output.with_suffix(output.suffix + ".tmp")
    temporary.write_text("\n".join(lines) + "\n", encoding="utf-8")
    temporary.replace(output)
    print(
        f"ZeroSlack MinGW exports: {len(exports)} from {len(objects)} objects"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
