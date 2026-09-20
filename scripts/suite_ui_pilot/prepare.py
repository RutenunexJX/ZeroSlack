"""Materialize the pinned, isolated Stage 3 source overlays under ZeroSlack/build."""

import argparse
import hashlib
import json
from pathlib import Path


TEMPLATES = (
    "CMakeLists.txt", "baseline-hook.cmake", "zeroslack-hook.cmake",
    "pilot_style.h", "pilot_tokens.h", "regmap_pilot.h", "regmap_pilot.cpp",
    "profile.cpp", "regmap_contract_test.cpp",
)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def materialize(manifest, roots):
    outputs = {}
    for entry in manifest["overlays"]:
        source = roots[entry["repository"]] / entry["source"]
        data = source.read_bytes()
        if digest(data) != entry["source_sha256"]:
            raise ValueError(f"Baseline changed; rebase and review the overlay: {source}")
        original_size = len(data)
        edits = entry["edits"]
        limit = original_size
        for edit in reversed(edits):
            start, end = edit["start"], edit["end"]
            if not 0 <= start <= end <= limit:
                raise ValueError(f"Invalid or overlapping edit in {source}")
            data = data[:start] + edit["replacement"].encode("utf-8") + data[end:]
            limit = start
        if digest(data) != entry["output_sha256"]:
            raise ValueError(f"Overlay output hash mismatch: {source}")
        outputs[entry["output"]] = data
    return outputs


def main():
    templates = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--zeroslack", type=Path, default=templates.parent.parent)
    parser.add_argument("--regmap", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--check", action="store_true", help="Validate inputs and outputs without writing")
    args = parser.parse_args()
    roots = {"zeroslack": args.zeroslack.resolve(), "regmap": args.regmap.resolve()}
    output = args.output.resolve()
    build = roots["zeroslack"] / "build"
    if output == build or not output.is_relative_to(build):
        parser.error("--output must be a subdirectory of ZeroSlack/build")
    manifest = json.loads((templates / "overlays.json").read_text(encoding="utf-8"))
    outputs = materialize(manifest, roots)
    outputs.update({name: (templates / name).read_bytes() for name in TEMPLATES})
    for name in outputs:
        if Path(name).name != name:
            raise ValueError(f"Output must be a filename: {name}")
    if not args.check:
        output.mkdir(parents=True, exist_ok=True)
        for name, data in outputs.items():
            (output / name).write_bytes(data)
    print(f"Validated {len(manifest['overlays'])} pinned overlays; "
          f"{'would write' if args.check else 'wrote'} {len(outputs)} files to {output}")


if __name__ == "__main__":
    main()
