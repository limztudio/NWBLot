"""Build the pow qualification probe from an existing Windows opt compile entry."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys

LIT_UTF_8 = "utf-8"
LIT_COMMAND = "command"
LIT_EXIT = "exit"
LIT_LOG = "log"
LIT_STORE_TRUE = "store_true"
LIT_O = "-o"
LIT_COMPILE = "compile"
LIT_LINK = "link"
LIT_VARIANTS = "variants"
LIT_PRODUCTION = "production"
LIT_OBSERVATIONS = "observations"
LIT_MASKED_ADMITTED = "masked_admitted"
LIT_TRAPS = "traps"
LIT_TRAP_STATUS = "trap_status"
LIT_ADMITTED = "admitted"
LIT_UNAVAILABLE = "unavailable"
LIT_FAILED = "failed"
LIT_RESULTS_JSON = "results.json"


def run(command, path):
    completed = subprocess.run(command, capture_output=True, text=True, errors="replace")
    log = completed.stdout + completed.stderr
    path.write_text(log, encoding=LIT_UTF_8)
    return {LIT_COMMAND: command, LIT_EXIT: completed.returncode, LIT_LOG: str(path), "errors": len(re.findall(r"(?im)^.*(?:fatal error:|error:)", log)), "warnings": len(re.findall(r"(?im)^.*warning:", log))}


def parse_observations(log):
    mode = None
    rows = []
    for line in log.splitlines():
        if line.startswith("rounding="):
            mode = int(line.split("=")[1])
        if line.startswith("summary "):
            row = dict(item.split("=") for item in line.split()[1:])
            rows.append({"rounding": mode, **{key: value if key == "category" else int(value) for key, value in row.items()}})
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--strict", action=LIT_STORE_TRUE, help="Add strict FP as extra evidence; the normal opt profile is still required.")
    parser.add_argument("--traps", action=LIT_STORE_TRUE, help="Also run Windows SEH exception category checks.")
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("The standalone linker path currently requires Windows; use the CMake target elsewhere.")
    root = Path(__file__).resolve().parents[3]
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    entries = json.loads((args.build_dir / "compile_commands.json").read_text(encoding=LIT_UTF_8))
    entry = next(item for item in entries if item["file"].replace("\\", "/").endswith("/tests/unit/math/math_tests.cpp") and "-DNWB_OPTIMIZE " in item[LIT_COMMAND])
    command = shlex.split(entry[LIT_COMMAND], posix=False)
    command = [item[1:-1] if item.startswith('"') and item.endswith('"') else item for item in command]
    command = [item for item in command if not item.startswith("-DCMAKE_INTDIR")]
    source = root / "tests/performance/simd_optimization/pow_qualification.cpp"
    object_path, executable = output / "pow_qualification.obj", output / "pow_qualification.exe"
    command[command.index(LIT_O) + 1] = str(object_path)
    command[-1] = str(source)
    if args.strict:
        command.append("-ffp-model=strict")
    metadata = {"source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(), "source_entry": entry, "strict": args.strict, "compiler": run([command[0], "--version"], output / "compiler.log"), "revision": run(["git", "rev-parse", "HEAD"], output / "revision.log"), LIT_COMPILE: run(command, output / "compile.log")}
    metadata[LIT_COMPILE]["removed"] = ["CMAKE_INTDIR definition", "original output and source paths"]
    if metadata[LIT_COMPILE][LIT_EXIT] == 0:
        target = next(item for item in command if item.startswith("--target="))
        link = [command[0], target, str(object_path), LIT_O, str(executable), "-fuse-ld=lld"]
        metadata[LIT_LINK] = run(link, output / "link.log")
        if metadata[LIT_LINK][LIT_EXIT] == 0:
            metadata[LIT_VARIANTS] = {}
            for variant in (LIT_PRODUCTION, "packed_single", "packed_double"):
                record = run([str(executable), variant], output / f"{variant}.log")
                record[LIT_OBSERVATIONS] = parse_observations(Path(record[LIT_LOG]).read_text(encoding=LIT_UTF_8))
                record[LIT_MASKED_ADMITTED] = record[LIT_EXIT] == 0 and len(record[LIT_OBSERVATIONS]) == 16
                if args.traps:
                    record[LIT_TRAPS] = run([str(executable), variant, LIT_TRAPS], output / f"{variant}_traps.log")
                    record[LIT_TRAP_STATUS] = {0: LIT_ADMITTED, 1: "rejected", 2: LIT_UNAVAILABLE}.get(record[LIT_TRAPS][LIT_EXIT], LIT_FAILED)
                else:
                    record[LIT_TRAP_STATUS] = "not_requested"
                record[LIT_ADMITTED] = record[LIT_MASKED_ADMITTED] and (not args.traps or record[LIT_TRAP_STATUS] == LIT_ADMITTED)
                metadata[LIT_VARIANTS][variant] = record
    (output / LIT_RESULTS_JSON).write_text(json.dumps(metadata, indent=2) + "\n", encoding=LIT_UTF_8)
    if metadata[LIT_COMPILE][LIT_EXIT] != 0 or metadata.get(LIT_LINK, {}).get(LIT_EXIT, 1) != 0:
        print("pow qualification compile/link failed; see", output / LIT_RESULTS_JSON)
        return 2
    for variant, record in metadata[LIT_VARIANTS].items():
        totals = {key: sum(row[key] for row in record[LIT_OBSERVATIONS]) for key in ("bits", "flags", "errno", "quantized")}
        print(variant, LIT_EXIT, record[LIT_EXIT], "differences", totals, LIT_TRAPS, record.get(LIT_TRAPS, {}).get(LIT_EXIT, "not requested"))
    if not metadata[LIT_VARIANTS][LIT_PRODUCTION][LIT_MASKED_ADMITTED] or any(item[LIT_TRAP_STATUS] in (LIT_UNAVAILABLE, LIT_FAILED) for item in metadata[LIT_VARIANTS].values()):
        return 2
    return 1 if any(not item[LIT_ADMITTED] for item in metadata[LIT_VARIANTS].values()) else 0


if __name__ == "__main__":
    sys.exit(main())
