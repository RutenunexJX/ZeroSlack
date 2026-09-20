"""Run the real RegMap control/workflow matrix from an isolated pilot build."""

import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

WORKFLOWS = [
    "appliesWorkbookTheme", "structuresCompetitionShellResponsively",
    "batchEditsAndCopiesCompleteRanges", "batchEditsNumericRangesSafely",
    "showsUnifiedSyncStateAndGeneratedResults", "reportsBlockedUnsavedSyncAndRecovers",
    "navigatesBitfieldWithKeyboard", "cancelsBitfieldDragWithEscape",
    "navigatesBlocksFromAddressMap",
]

def run_case(case, executables, output, environment, sdk):
    theme, scale, variant = case
    name = f"{theme}-{scale}-{variant}"
    dest = output / name
    dest.mkdir(parents=True, exist_ok=True)
    env = dict(environment, QT_SCALE_FACTOR=str(scale),
               QT_REDUCE_MOTION="1" if variant == "reduced" else "0")
    if sdk:
        env.update(REGMAP_TEST_THEME=theme,
                   REGMAP_UI_STYLE="classic" if variant == "classic" else "suiteui",
                   REGMAP_UI_ARTIFACT_DIR=str(dest))
    else:
        env.update(PILOT_THEME=theme,
                   REGMAP_PILOT_STYLE="classic" if variant == "classic" else "qlementine",
                   PILOT_ARTIFACT_DIR=str(dest))
    result = {"case": name, "processes": []}
    for exe, functions in zip(executables, ([], WORKFLOWS)):
        log = dest / f"{exe.stem}.log"
        start = time.monotonic()
        try:
            p = subprocess.run([str(exe), *functions, "-o", f"{log},txt"], env=env,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=90)
            code = p.returncode
            console = p.stdout.decode("utf-8", errors="replace")
        except subprocess.TimeoutExpired as exc:
            code, console = 124, str(exc)
        (dest / f"{exe.stem}.console.txt").write_text(console, encoding="utf-8")
        result["processes"].append({"exe": str(exe), "exit": code,
            "seconds": round(time.monotonic() - start, 3), "log": str(log)})
    result["passed"] = all(p["exit"] == 0 for p in result["processes"])
    return result

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--font-dir", type=Path, required=True)
    parser.add_argument("--runtime-dir", type=Path, action="append", default=[])
    parser.add_argument("--workers", type=int, default=3)
    parser.add_argument("--sdk", action="store_true", help="Use the installed SuiteUi integration in the real RegMap build")
    args = parser.parse_args()
    build, output, fonts = args.build.resolve(), args.output.resolve(), args.font_dir.resolve()
    executables = ([build / "tests/regmap_suiteui_control_test.exe", build / "tests/regmap_gui_tests.exe"]
                   if args.sdk else [build / "regmap_contract_test.exe", build / "regmap/tests/regmap_gui_tests.exe"])
    if not all(p.is_file() for p in executables) or not fonts.is_dir():
        parser.error("Both test executables and the isolated font directory must exist")
    if args.workers < 1:
        parser.error("--workers must be positive")
    environment = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_QPA_FONTDIR=str(fonts))
    environment.pop("PILOT_NATIVE_REVIEW", None)
    environment.pop("REGMAP_UI_REVIEW", None)
    environment["PATH"] = os.pathsep.join(str(p.resolve()) for p in args.runtime_dir) + os.pathsep + environment["PATH"]
    cases = [(t, s, v) for t in ("light", "dark") for s in (1, 1.25, 1.5, 2)
             for v in ("classic", "animated", "reduced")]
    results = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as pool:
        for result in pool.map(lambda case: run_case(case, executables, output, environment, args.sdk), cases):
            results.append(result)
            print(result["case"], "PASS" if result["passed"] else "FAIL", flush=True)
    manifest = {"integration": "sdk" if args.sdk else "prototype", "cases": results, "passed": sum(r["passed"] for r in results),
        "total": len(results), "executables": {str(p): hashlib.sha256(p.read_bytes()).hexdigest()
                                                for p in executables},
        "fonts": {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                  for p in fonts.iterdir() if p.is_file()}}
    (output / "matrix-results.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    raise SystemExit(0 if manifest["passed"] == len(results) else 1)


if __name__ == "__main__":
    main()
