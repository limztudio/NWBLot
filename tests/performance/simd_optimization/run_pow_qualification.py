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


def run(command, path):
    completed = subprocess.run(command, capture_output=True, text=True, errors="replace")
    log = completed.stdout + completed.stderr
    path.write_text(log, encoding="utf-8")
    return {"command": command, "exit": completed.returncode, "log": str(path), "errors": len(re.findall(r"(?im)^.*(?:fatal error:|error:)", log)), "warnings": len(re.findall(r"(?im)^.*warning:", log))}


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
    parser.add_argument("--strict", action="store_true", help="Add strict FP as extra evidence; the normal opt profile is still required.")
    parser.add_argument("--traps", action="store_true", help="Also run Windows SEH exception category checks.")
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("The standalone linker path currently requires Windows; use the CMake target elsewhere.")
    root = Path(__file__).resolve().parents[3]
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    entries = json.loads((args.build_dir / "compile_commands.json").read_text(encoding="utf-8"))
    entry = next(item for item in entries if item["file"].replace("\\", "/").endswith("/tests/unit/math/math_tests.cpp") and "-DNWB_OPTIMIZE " in item["command"])
    command = shlex.split(entry["command"], posix=False)
    command = [item[1:-1] if item.startswith('"') and item.endswith('"') else item for item in command]
    command = [item for item in command if not item.startswith("-DCMAKE_INTDIR")]
    source = root / "tests/performance/simd_optimization/pow_qualification.cpp"
    object_path, executable = output / "pow_qualification.obj", output / "pow_qualification.exe"
    command[command.index("-o") + 1] = str(object_path)
    command[-1] = str(source)
    if args.strict:
        command.append("-ffp-model=strict")
    metadata = {"source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(), "source_entry": entry, "strict": args.strict, "compiler": run([command[0], "--version"], output / "compiler.log"), "revision": run(["git", "rev-parse", "HEAD"], output / "revision.log"), "compile": run(command, output / "compile.log")}
    metadata["compile"]["removed"] = ["CMAKE_INTDIR definition", "original output and source paths"]
    if metadata["compile"]["exit"] == 0:
        target = next(item for item in command if item.startswith("--target="))
        link = [command[0], target, str(object_path), "-o", str(executable), "-fuse-ld=lld"]
        metadata["link"] = run(link, output / "link.log")
        if metadata["link"]["exit"] == 0:
            metadata["variants"] = {}
            for variant in ("production", "packed_single", "packed_double"):
                record = run([str(executable), variant], output / f"{variant}.log")
                record["observations"] = parse_observations(Path(record["log"]).read_text(encoding="utf-8"))
                record["masked_admitted"] = record["exit"] == 0 and len(record["observations"]) == 16
                if args.traps:
                    record["traps"] = run([str(executable), variant, "traps"], output / f"{variant}_traps.log")
                    record["trap_status"] = {0: "admitted", 1: "rejected", 2: "unavailable"}.get(record["traps"]["exit"], "failed")
                else:
                    record["trap_status"] = "not_requested"
                record["admitted"] = record["masked_admitted"] and (not args.traps or record["trap_status"] == "admitted")
                metadata["variants"][variant] = record
    (output / "results.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    if metadata["compile"]["exit"] != 0 or metadata.get("link", {}).get("exit", 1) != 0:
        print("pow qualification compile/link failed; see", output / "results.json")
        return 2
    for variant, record in metadata["variants"].items():
        totals = {key: sum(row[key] for row in record["observations"]) for key in ("bits", "flags", "errno", "quantized")}
        print(variant, "exit", record["exit"], "differences", totals, "traps", record.get("traps", {}).get("exit", "not requested"))
    if not metadata["variants"]["production"]["masked_admitted"] or any(item["trap_status"] in ("unavailable", "failed") for item in metadata["variants"].values()):
        return 2
    return 1 if any(not item["admitted"] for item in metadata["variants"].values()) else 0


if __name__ == "__main__":
    sys.exit(main())
