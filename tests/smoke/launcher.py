#!/usr/bin/env python3
import argparse
import os
import platform
import re
import shlex
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Optional


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
    "soft-shadow-test": SmokeScene(
        runtime="skinning_culling_benchmark_runtime",
        backends={
            "hw": SmokeExecutable("nwb_soft_shadow_test_smoke", "soft_shadow_test_smoke"),
            "sw": SmokeExecutable("nwb_soft_shadow_test_sw_smoke", "soft_shadow_test_sw_smoke"),
        },
    ),
}


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def host_platform_name(system_name: Optional[str] = None) -> str:
    system = system_name or platform.system()
    if system == "Windows":
        return "windows"
    if system == "Linux":
        return "linux"
    if system == "Darwin":
        return "darwin"
    if system:
        return system.lower()
    return sys.platform.lower()


def executable_name(base_name: str, platform_name: str) -> str:
    return base_name + ".exe" if platform_name == "windows" else base_name


def default_build_preset_name(platform_name: str, domain: str, arch: str) -> str:
    if domain == "full":
        return f"{platform_name}-clang-{arch}"
    return f"{platform_name}-clang-{domain}-{arch}"


def default_build_dir(root: Path, platform_name: str, domain: str, arch: str) -> Path:
    return root / "__cmake" / "build" / default_build_preset_name(platform_name, domain, arch)


def output_root(root: Path, platform_name: str, arch: str, domain: str) -> Path:
    base = root / "__exec" / platform_name / arch
    if domain == "engine":
        return base
    return base / domain


def read_cmake_cache_value(build_dir: Path, key: str) -> Optional[str]:
    cache = build_dir / "CMakeCache.txt"
    try:
        with cache.open("r", encoding="utf-8", errors="replace") as cache_file:
            for line in cache_file:
                prefix = f"{key}:"
                if line.startswith(prefix):
                    _, value = line.rstrip("\n").split("=", 1)
                    return value
    except OSError:
        return None
    return None


def infer_output_domain(build_dir: Path, platform_name: str, arch: str) -> str:
    cached_domain = read_cmake_cache_value(build_dir, "NWB_OUTPUT_DOMAIN")
    if cached_domain:
        return cached_domain

    name = build_dir.name
    if name == default_build_preset_name(platform_name, "full", arch):
        return "full"

    prefix = f"{platform_name}-clang-"
    suffix = f"-{arch}"
    if name.startswith(prefix) and name.endswith(suffix):
        domain = name[len(prefix) : -len(suffix)]
        return domain or "full"

    return name or "default"


def cmake_command(root: Path, override: Optional[Path]) -> list[str]:
    if override is not None:
        return [str(override)]

    env_command = os.environ.get("CMAKE_COMMAND")
    if env_command:
        return [env_command]

    local_bin_dir = "Scripts" if os.name == "nt" else "bin"
    candidate = root / "__cmake" / "tool-venv" / local_bin_dir / executable_name("cmake", host_platform_name())
    if candidate.exists():
        return [str(candidate)]

    return ["cmake"]


def format_command(command) -> str:
    parts = [str(part) for part in command]
    if os.name == "nt":
        return subprocess.list2cmdline(parts)
    return " ".join(shlex.quote(part) for part in parts)


def run_checked(command, cwd, env):
    print("+ " + format_command(command), flush=True)
    completed = subprocess.run(command, cwd=cwd, env=env)
    if completed.returncode != 0:
        raise SystemExit(completed.returncode)


def parse_args(argv):
    parser = argparse.ArgumentParser(description="Build, recook, and launch an NWB smoke scene.")
    parser.add_argument("--scene", choices=sorted(SMOKE_SCENES), default="transparent-multi")
    parser.add_argument("--backend", default="hw", help="Backend variant for the scene, such as hw/sw/compute.")
    parser.add_argument("--build-dir", type=Path, help="CMake build directory.")
    parser.add_argument("--platform", default=host_platform_name(), help="Output platform directory, such as windows/linux/darwin.")
    parser.add_argument("--arch", default="x64", help="Output architecture directory.")
    parser.add_argument("--domain", help="Output domain directory. Defaults to the CMake cache or inferred preset domain.")
    parser.add_argument("--cmake", type=Path, help="CMake executable. Defaults to CMAKE_COMMAND, repo-local CMake, or cmake on PATH.")
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
    if not args.platform:
        parser.error("--platform must not be empty")
    if not args.arch:
        parser.error("--arch must not be empty")
    if args.domain is not None and not args.domain:
        parser.error("--domain must not be empty")

    return args


def build_environment(args):
    env = os.environ.copy()
    if args.spin_angle is not None:
        env["NWB_TRANSPARENT_MULTI_SPIN_ANGLE"] = args.spin_angle
    if args.spin_speed is not None:
        env["NWB_TRANSPARENT_MULTI_SPIN_SPEED"] = args.spin_speed
    return env


def stop_existing_process(executable: Path, platform_name: str):
    if platform_name == "windows":
        command = ["taskkill", "/F", "/IM", executable.name]
        subprocess.run(command, check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return

    if shutil.which("pkill") is None:
        print("warning: --kill-existing requested, but pkill is not available on this host", flush=True)
        return

    pattern = re.escape(str(executable.resolve()))
    subprocess.run(["pkill", "-f", pattern], check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def run(args):
    root = repo_root()
    scene = SMOKE_SCENES[args.scene]
    smoke_executable = scene.backends[args.backend]
    domain_hint = args.domain or "engine"
    build_dir = args.build_dir or default_build_dir(root, args.platform, domain_hint, args.arch)
    domain = args.domain or infer_output_domain(build_dir, args.platform, args.arch)
    env = build_environment(args)

    run_checked(
        cmake_command(root, args.cmake)
        + [
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

    executable = output_root(root, args.platform, args.arch, domain) / args.config / executable_name(smoke_executable.executable, args.platform)
    working_directory = build_dir / "Testing" / scene.runtime / args.config
    if not executable.exists():
        raise SystemExit(f"missing executable: {executable}")
    if not working_directory.exists():
        raise SystemExit(f"missing working directory: {working_directory}")

    if args.kill_existing:
        stop_existing_process(executable, host_platform_name())

    launch = [str(executable)]
    if args.gpudbg:
        launch.append("--gpudbg")

    print("+ " + format_command(launch), flush=True)
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
