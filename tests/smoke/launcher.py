#!/usr/bin/env python3
import argparse
import os
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class SmokeExecutable:
    target: str
    executable: str


@dataclass(frozen=True)
class SmokeScene:
    runtime: str
    backends: dict[str, SmokeExecutable]


SMOKE_SCENES = {
    "transparent-multi": SmokeScene(
        runtime="smoke_runtime",
        backends={
            "hw": SmokeExecutable("nwb_transparent_multi_smoke", "transparent_multi_smoke"),
            "sw": SmokeExecutable("nwb_transparent_multi_sw_smoke", "transparent_multi_sw_smoke"),
        },
    ),
    "transparent-csg": SmokeScene(
        runtime="smoke_runtime",
        backends={
            "hw": SmokeExecutable("nwb_transparent_csg_smoke", "transparent_csg_smoke"),
        },
    ),
    "caustic-sphere": SmokeScene(
        runtime="smoke_runtime",
        backends={
            "hw": SmokeExecutable("nwb_caustic_sphere_smoke", "caustic_sphere_smoke"),
            "sw": SmokeExecutable("nwb_caustic_sphere_sw_smoke", "caustic_sphere_sw_smoke"),
        },
    ),
    "csg-visible": SmokeScene(
        runtime="csg_visible_smoke_runtime",
        backends={
            "hw": SmokeExecutable("nwb_csg_visible_smoke", "csg_visible_smoke"),
            "compute": SmokeExecutable("nwb_csg_visible_compute_emulation_smoke", "csg_visible_compute_emulation_smoke"),
        },
    ),
    "stress-test": SmokeScene(
        runtime="skinning_culling_benchmark_runtime",
        backends={
            "hw": SmokeExecutable("nwb_stress_test_smoke", "stress_test_smoke"),
            "sw": SmokeExecutable("nwb_stress_test_sw_smoke", "stress_test_sw_smoke"),
        },
    ),
    "flicker-test": SmokeScene(
        runtime="skinning_culling_benchmark_runtime",
        backends={
            "hw": SmokeExecutable("nwb_flicker_test_smoke", "flicker_test_smoke"),
            "sw": SmokeExecutable("nwb_flicker_test_sw_smoke", "flicker_test_sw_smoke"),
        },
    ),
}


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def executable_name(base_name: str) -> str:
    return base_name + ".exe" if os.name == "nt" else base_name


def run_checked(command, cwd, env):
    print("+ " + " ".join(str(part) for part in command), flush=True)
    completed = subprocess.run(command, cwd=cwd, env=env)
    if completed.returncode != 0:
        raise SystemExit(completed.returncode)


def parse_args(argv):
    parser = argparse.ArgumentParser(description="Build, recook, and launch an NWB smoke scene.")
    parser.add_argument("--scene", choices=sorted(SMOKE_SCENES), default="transparent-multi")
    parser.add_argument("--backend", default="hw", help="Backend variant for the scene, such as hw/sw/compute.")
    parser.add_argument("--build-dir", type=Path, help="CMake build directory.")
    parser.add_argument("--config", default="dbg")
    parser.add_argument("--jobs", default="8")
    parser.add_argument("--gpudbg", action="store_true")
    parser.add_argument("--spin-angle", help="Pin NWB_TRANSPARENT_MULTI_SPIN_ANGLE, in radians.")
    parser.add_argument("--spin-speed", help="Set NWB_TRANSPARENT_MULTI_SPIN_SPEED.")
    parser.add_argument("--kill-existing", action="store_true", help="Close any running executable with the same name before launch.")
    parser.add_argument("--detach", action="store_true", help="Return after launch instead of waiting for the app.")
    args = parser.parse_args(argv)

    scene = SMOKE_SCENES[args.scene]
    if args.backend not in scene.backends:
        valid = ", ".join(sorted(scene.backends))
        parser.error(f"scene '{args.scene}' does not have backend '{args.backend}' (valid: {valid})")

    return args


def build_environment(args):
    env = os.environ.copy()
    if args.spin_angle is not None:
        env["NWB_TRANSPARENT_MULTI_SPIN_ANGLE"] = args.spin_angle
    if args.spin_speed is not None:
        env["NWB_TRANSPARENT_MULTI_SPIN_SPEED"] = args.spin_speed
    return env


def stop_existing_process(executable: Path):
    if os.name != "nt":
        return

    process_name = executable.stem
    command = [
        "powershell",
        "-NoProfile",
        "-Command",
        f"Get-Process {process_name} -ErrorAction SilentlyContinue | Stop-Process -Force",
    ]
    subprocess.run(command, check=False)


def run(args):
    root = repo_root()
    scene = SMOKE_SCENES[args.scene]
    smoke_executable = scene.backends[args.backend]
    build_dir = args.build_dir or (root / "__cmake" / "build" / "windows-clang-engine-x64")
    env = build_environment(args)

    run_checked(
        [
            "cmake",
            "--build",
            str(build_dir),
            "--target",
            smoke_executable.target,
            "--config",
            args.config,
            "-j",
            str(args.jobs),
        ],
        root,
        env,
    )

    executable = root / "__exec" / "windows" / "x64" / args.config / executable_name(smoke_executable.executable)
    working_directory = build_dir / "Testing" / scene.runtime / args.config
    if not executable.exists():
        raise SystemExit(f"missing executable: {executable}")
    if not working_directory.exists():
        raise SystemExit(f"missing working directory: {working_directory}")

    if args.kill_existing:
        stop_existing_process(executable)

    launch = [str(executable)]
    if args.gpudbg:
        launch.append("--gpudbg")

    print("+ " + " ".join(launch), flush=True)
    process = subprocess.Popen(launch, cwd=working_directory, env=env)
    print(f"launched {executable.name} pid={process.pid}", flush=True)
    if args.detach:
        return 0

    try:
        return process.wait()
    except KeyboardInterrupt:
        print("leaving app running; close the window when done", flush=True)
        return 0


def main(argv):
    return run(parse_args(argv))


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
