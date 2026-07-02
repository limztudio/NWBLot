#!/usr/bin/env python3
import argparse
import json
import os
import platform
import re
import shlex
import shutil
import subprocess
import sys
from dataclasses import dataclass, replace
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Sequence, Tuple


CONFIGURATIONS = ("dbg", "opt", "fin")
DEFAULT_ARCH = "x64"
DEFAULT_CONFIG = "dbg"
DEFAULT_DOMAIN = "full"


@dataclass(frozen=True)
class SmokeExecutable:
    target: str
    executable: str


@dataclass(frozen=True)
class SmokeScene:
    runtime: str
    backends: Dict[str, SmokeExecutable]


@dataclass(frozen=True)
class LaunchSettings:
    root: Path
    platform_name: str
    arch: str
    domain: str
    config: str
    configure_preset: str
    build_dir: Path
    cmake: Tuple[str, ...]


@dataclass(frozen=True)
class CMakeTargetInfo:
    name: str
    target_type: str
    artifacts: Tuple[Path, ...]


SMOKE_REQUIRED_DEFINES = {
    "NWB_BUILD_TESTS": "ON",
}


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
    "csg-skinned-visible": SmokeScene(
        runtime="csg_skinned_visible_smoke_runtime",
        backends={
            "hw": SmokeExecutable("nwb_csg_skinned_visible_smoke", "csg_skinned_visible_smoke"),
        },
    ),
    "csg-skinned-sphere-visible": SmokeScene(
        runtime="csg_skinned_visible_smoke_runtime",
        backends={
            "hw": SmokeExecutable("nwb_csg_skinned_sphere_visible_smoke", "csg_skinned_sphere_visible_smoke"),
        },
    ),
    "csg-skinned-transparent-sphere-visible": SmokeScene(
        runtime="csg_skinned_visible_smoke_runtime",
        backends={
            "hw": SmokeExecutable(
                "nwb_csg_skinned_transparent_sphere_visible_smoke",
                "csg_skinned_transparent_sphere_visible_smoke",
            ),
        },
    ),
    "skinning-culling-benchmark": SmokeScene(
        runtime="skinning_culling_benchmark_runtime",
        backends={
            "hw": SmokeExecutable("nwb_skinning_culling_benchmark", "skinning_culling_benchmark"),
        },
    ),
    "skinned-caustic": SmokeScene(
        runtime="skinning_culling_benchmark_runtime",
        backends={
            "hw": SmokeExecutable("nwb_skinned_caustic_smoke", "skinned_caustic_smoke"),
            "sw": SmokeExecutable("nwb_skinned_caustic_sw_smoke", "skinned_caustic_sw_smoke"),
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
    return Path(__file__).resolve().parents[1]


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


def default_configure_preset_name(platform_name: str, domain: str, arch: str) -> str:
    if domain == DEFAULT_DOMAIN:
        return f"{platform_name}-clang-{arch}"
    return f"{platform_name}-clang-{domain}-{arch}"


def default_build_preset_name(platform_name: str, domain: str, config: str) -> str:
    if domain == DEFAULT_DOMAIN:
        return f"{platform_name}-clang-{config}"
    return f"{platform_name}-clang-{domain}-{config}"


def default_build_dir(root: Path, platform_name: str, domain: str, arch: str) -> Path:
    return root / "__cmake" / "build" / default_configure_preset_name(platform_name, domain, arch)


def output_root(root: Path, platform_name: str, arch: str, domain: str) -> Path:
    base = root / "__exec" / platform_name / arch
    if domain == "engine":
        return base
    return base / domain


def target_default_executable_base_name(target: str) -> str:
    if target.startswith("nwb_"):
        return target[len("nwb_") :]
    return target


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
    if name == default_configure_preset_name(platform_name, DEFAULT_DOMAIN, arch):
        return DEFAULT_DOMAIN

    prefix = f"{platform_name}-clang-"
    suffix = f"-{arch}"
    if name.startswith(prefix) and name.endswith(suffix):
        domain = name[len(prefix) : -len(suffix)]
        return domain or DEFAULT_DOMAIN

    return name or "default"


def cmake_command(root: Path, override: Optional[Path], platform_name: Optional[str] = None) -> Tuple[str, ...]:
    if override is not None:
        return (str(override),)

    env_command = os.environ.get("CMAKE_COMMAND")
    if env_command:
        return (env_command,)

    local_bin_dir = "Scripts" if os.name == "nt" else "bin"
    candidate_platform = platform_name or host_platform_name()
    candidate = root / "__cmake" / "tool-venv" / local_bin_dir / executable_name("cmake", candidate_platform)
    if candidate.exists():
        return (str(candidate),)

    return ("cmake",)


def format_command(command: Sequence[object]) -> str:
    parts = [str(part) for part in command]
    if os.name == "nt":
        return subprocess.list2cmdline(parts)
    return " ".join(shlex.quote(part) for part in parts)


def run_checked(command: Sequence[object], cwd: Path, env: Dict[str, str], dry_run: bool = False) -> None:
    print("+ " + format_command(command), flush=True)
    if dry_run:
        return

    completed = subprocess.run([str(part) for part in command], cwd=cwd, env=env)
    if completed.returncode != 0:
        raise SystemExit(completed.returncode)


def parse_define_entries(entries: Iterable[str]) -> Dict[str, str]:
    defines: Dict[str, str] = {}
    for entry in entries:
        if "=" not in entry:
            raise SystemExit(f"CMake define must be KEY=VALUE: {entry}")
        key, value = entry.split("=", 1)
        if not key:
            raise SystemExit(f"CMake define key must not be empty: {entry}")
        defines[key] = value
    return defines


def cmake_define_args(defines: Dict[str, str]) -> List[str]:
    return [f"-D{key}={value}" for key, value in sorted(defines.items())]


def normalize_cache_bool(value: Optional[str]) -> Optional[bool]:
    if value is None:
        return None
    upper_value = value.upper()
    if upper_value in ("1", "ON", "TRUE", "YES"):
        return True
    if upper_value in ("0", "OFF", "FALSE", "NO"):
        return False
    return None


def cache_matches_required_defines(build_dir: Path, required_defines: Dict[str, str]) -> bool:
    for key, required_value in required_defines.items():
        cached_value = read_cmake_cache_value(build_dir, key)
        required_bool = normalize_cache_bool(required_value)
        cached_bool = normalize_cache_bool(cached_value)
        if required_bool is not None or cached_bool is not None:
            if cached_bool != required_bool:
                return False
        elif cached_value != required_value:
            return False
    return True


def file_api_query_path(build_dir: Path) -> Path:
    return build_dir / ".cmake" / "api" / "v1" / "query" / "codemodel-v2"


def file_api_reply_dir(build_dir: Path) -> Path:
    return build_dir / ".cmake" / "api" / "v1" / "reply"


def ensure_file_api_query(build_dir: Path) -> None:
    query = file_api_query_path(build_dir)
    query.parent.mkdir(parents=True, exist_ok=True)
    query.touch()


def latest_file_api_index(build_dir: Path) -> Optional[Path]:
    reply_dir = file_api_reply_dir(build_dir)
    try:
        indexes = list(reply_dir.glob("index-*.json"))
    except OSError:
        return None
    if not indexes:
        return None
    return max(indexes, key=lambda path: (path.stat().st_mtime, path.name))


def file_api_has_reply(build_dir: Path) -> bool:
    return latest_file_api_index(build_dir) is not None


def read_json(path: Path):
    with path.open("r", encoding="utf-8") as file:
        return json.load(file)


def load_cmake_target_info(build_dir: Path, target_name: str, config: str) -> Optional[CMakeTargetInfo]:
    index_path = latest_file_api_index(build_dir)
    if index_path is None:
        return None

    index = read_json(index_path)
    codemodel_file = None
    for item in index.get("objects", []):
        if item.get("kind") != "codemodel":
            continue
        version = item.get("version", {})
        if version.get("major") == 2:
            codemodel_file = item.get("jsonFile")
            break
    if not codemodel_file:
        return None

    reply_dir = index_path.parent
    codemodel = read_json(reply_dir / codemodel_file)
    configurations = codemodel.get("configurations", [])
    configuration = next((entry for entry in configurations if entry.get("name") == config), None)
    if configuration is None and len(configurations) == 1:
        configuration = configurations[0]
    if configuration is None:
        return None

    for target_ref in configuration.get("targets", []):
        if target_ref.get("name") != target_name:
            continue

        target = read_json(reply_dir / target_ref["jsonFile"])
        artifacts = []
        for artifact in target.get("artifacts", []):
            artifact_path = Path(artifact["path"])
            if not artifact_path.is_absolute():
                artifact_path = build_dir / artifact_path
            artifacts.append(artifact_path)

        return CMakeTargetInfo(
            name=target.get("name", target_name),
            target_type=target.get("type", ""),
            artifacts=tuple(artifacts),
        )

    return None


def resolve_repo_root(value: Optional[Path]) -> Path:
    if value is not None:
        return value.resolve()
    return repo_root()


def resolve_launch_settings(args, default_domain: str) -> LaunchSettings:
    root = resolve_repo_root(args.repo_root)
    platform_name = args.platform
    arch = args.arch
    if args.configure_preset:
        configure_preset = args.configure_preset
        build_dir = args.build_dir or root / "__cmake" / "build" / configure_preset
    else:
        requested_domain = args.domain or default_domain
        configure_preset = default_configure_preset_name(platform_name, requested_domain, arch)
        build_dir = args.build_dir or default_build_dir(root, platform_name, requested_domain, arch)
    domain = args.domain or infer_output_domain(build_dir, platform_name, arch)
    return LaunchSettings(
        root=root,
        platform_name=platform_name,
        arch=arch,
        domain=domain,
        config=args.config,
        configure_preset=configure_preset,
        build_dir=build_dir,
        cmake=cmake_command(root, args.cmake, platform_name),
    )


def refresh_launch_settings(settings: LaunchSettings, explicit_domain: Optional[str]) -> LaunchSettings:
    if explicit_domain:
        return settings
    domain = infer_output_domain(settings.build_dir, settings.platform_name, settings.arch)
    if domain == settings.domain:
        return settings
    return replace(settings, domain=domain)


def configure_command(settings: LaunchSettings, build_dir_was_configured: bool, extra_defines: Dict[str, str]) -> List[str]:
    preset_build_dir = settings.root / "__cmake" / "build" / settings.configure_preset
    if settings.build_dir == preset_build_dir:
        return list(settings.cmake) + ["--preset", settings.configure_preset] + cmake_define_args(extra_defines)
    if build_dir_was_configured:
        return list(settings.cmake) + ["-S", str(settings.root), "-B", str(settings.build_dir)] + cmake_define_args(extra_defines)
    raise SystemExit(
        "custom --build-dir is not configured; configure it first or use a matching --configure-preset/default build dir"
    )


def maybe_configure(args, settings: LaunchSettings, required_defines: Dict[str, str], env: Dict[str, str]) -> None:
    build_dir_was_configured = (settings.build_dir / "CMakeCache.txt").exists()
    if not args.dry_run:
        ensure_file_api_query(settings.build_dir)

    extra_defines = parse_define_entries(args.defines)
    extra_defines.update(required_defines)

    needs_configure = (
        args.configure == "always"
        or bool(args.defines)
        or not build_dir_was_configured
        or not file_api_has_reply(settings.build_dir)
        or not cache_matches_required_defines(settings.build_dir, required_defines)
    )
    if not needs_configure:
        return

    if args.configure == "never":
        raise SystemExit(f"CMake configure is required for {settings.build_dir}, but --configure=never was requested")

    command = configure_command(settings, build_dir_was_configured, extra_defines)
    run_checked(command, settings.root, env, args.dry_run)


def build_target(args, settings: LaunchSettings, target: str, env: Dict[str, str]) -> None:
    if args.skip_build:
        return

    command = list(settings.cmake) + [
        "--build",
        str(settings.build_dir),
        "--target",
        target,
        "--config",
        settings.config,
    ]
    if args.jobs:
        command += ["--parallel", str(args.jobs)]
    run_checked(command, settings.root, env, args.dry_run)


def resolve_executable_path(
    settings: LaunchSettings,
    target: str,
    executable_override: Optional[Path],
    executable_base_name: Optional[str],
    dry_run: bool,
) -> Path:
    if executable_override is not None:
        return executable_override if executable_override.is_absolute() else settings.root / executable_override

    target_info = None if dry_run else load_cmake_target_info(settings.build_dir, target, settings.config)
    if target_info is not None:
        if target_info.target_type != "EXECUTABLE":
            raise SystemExit(f"CMake target is not executable: {target}")
        if target_info.artifacts:
            return target_info.artifacts[0]

    if not dry_run:
        print("warning: CMake target metadata unavailable; using repository executable naming convention", flush=True)

    base_name = executable_base_name or target_default_executable_base_name(target)
    return output_root(settings.root, settings.platform_name, settings.arch, settings.domain) / settings.config / executable_name(
        base_name,
        settings.platform_name,
    )


def resolve_working_directory(settings: LaunchSettings, override: Optional[Path], default_directory: Path) -> Path:
    if override is None:
        return default_directory
    return override if override.is_absolute() else settings.root / override


def build_environment(args) -> Dict[str, str]:
    env = os.environ.copy()
    spin_angle = getattr(args, "spin_angle", None)
    spin_speed = getattr(args, "spin_speed", None)
    if spin_angle is not None:
        env["NWB_TRANSPARENT_MULTI_SPIN_ANGLE"] = spin_angle
    if spin_speed is not None:
        env["NWB_TRANSPARENT_MULTI_SPIN_SPEED"] = spin_speed
    return env


def normalize_application_args(args: Sequence[str]) -> List[str]:
    values = list(args)
    return values


def stop_existing_process(executable: Path, platform_name: str) -> None:
    if platform_name == "windows":
        command = ["taskkill", "/F", "/IM", executable.name]
        subprocess.run(command, check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return

    if shutil.which("pkill") is None:
        print("warning: --kill-existing requested, but pkill is not available on this host", flush=True)
        return

    pattern = re.escape(str(executable.resolve()))
    subprocess.run(["pkill", "-f", pattern], check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def launch_process(args, executable: Path, working_directory: Path, env: Dict[str, str], application_args: Sequence[str]) -> int:
    if not args.dry_run:
        if not executable.exists():
            raise SystemExit(f"missing executable: {executable}")
        if not working_directory.exists():
            raise SystemExit(f"missing working directory: {working_directory}")

    if args.kill_existing and not args.dry_run:
        stop_existing_process(executable, host_platform_name())

    launch = [str(executable)]
    if args.gpudbg:
        launch.append("--gpudbg")
    launch += list(application_args)

    print("+ " + format_command(launch), flush=True)
    print(f"  cwd: {working_directory}", flush=True)
    if args.dry_run:
        return 0

    process = subprocess.Popen(launch, cwd=working_directory, env=env)
    print(f"launched {executable.name} pid={process.pid}", flush=True)
    if args.detach:
        return 0

    try:
        return process.wait()
    except KeyboardInterrupt:
        print("leaving app running; close the window when done", flush=True)
        return 0


def run_target_command(args) -> int:
    env = build_environment(args)
    settings = resolve_launch_settings(args, DEFAULT_DOMAIN)
    maybe_configure(args, settings, {}, env)
    settings = refresh_launch_settings(settings, args.domain)
    build_target(args, settings, args.target, env)

    executable = resolve_executable_path(settings, args.target, args.executable, args.executable_name, args.dry_run)
    working_directory = resolve_working_directory(
        settings,
        args.working_directory,
        output_root(settings.root, settings.platform_name, settings.arch, settings.domain),
    )
    return launch_process(args, executable, working_directory, env, normalize_application_args(args.application_args))


def smoke_scene_name(args) -> str:
    scene = args.scene_name or args.scene
    if scene not in SMOKE_SCENES:
        valid = ", ".join(sorted(SMOKE_SCENES))
        raise SystemExit(f"unknown smoke scene '{scene}' (valid: {valid})")
    return scene


def smoke_command(args) -> int:
    scene_name = smoke_scene_name(args)
    scene = SMOKE_SCENES[scene_name]
    if args.backend not in scene.backends:
        valid = ", ".join(sorted(scene.backends))
        raise SystemExit(f"scene '{scene_name}' does not have backend '{args.backend}' (valid: {valid})")

    smoke_executable = scene.backends[args.backend]
    env = build_environment(args)
    settings = resolve_launch_settings(args, DEFAULT_DOMAIN)
    maybe_configure(args, settings, SMOKE_REQUIRED_DEFINES, env)
    settings = refresh_launch_settings(settings, args.domain)
    build_target(args, settings, smoke_executable.target, env)

    executable = resolve_executable_path(
        settings,
        smoke_executable.target,
        args.executable,
        args.executable_name or smoke_executable.executable,
        args.dry_run,
    )
    working_directory = resolve_working_directory(
        settings,
        args.working_directory,
        settings.build_dir / "Testing" / scene.runtime / settings.config,
    )
    return launch_process(args, executable, working_directory, env, normalize_application_args(args.application_args))


def list_profiles_command(_args) -> int:
    print("runnable targets:")
    print("  run testbed")
    print("  run nwb_resource_cooker")
    print("  run nwb_fbx_to_nwb")
    print()
    print("smoke scenes:")
    for name in sorted(SMOKE_SCENES):
        scene = SMOKE_SCENES[name]
        backends = ", ".join(sorted(scene.backends))
        print(f"  smoke {name} --backend {{{backends}}}")
    return 0


def add_common_options(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--repo-root", type=Path, help="Repository root. Defaults to the parent of scripts/.")
    parser.add_argument("--platform", default=host_platform_name(), help="Output platform directory, such as windows/linux/darwin.")
    parser.add_argument("--arch", default=DEFAULT_ARCH, help="Output architecture directory.")
    parser.add_argument("--domain", help="Output domain directory. Defaults to full or the CMake cache.")
    parser.add_argument("--configure-preset", help="CMake configure preset. Defaults from platform/domain/arch.")
    parser.add_argument("--build-dir", type=Path, help="CMake build directory.")
    parser.add_argument("--cmake", type=Path, help="CMake executable. Defaults to CMAKE_COMMAND, repo-local CMake, or cmake on PATH.")
    parser.add_argument("--config", choices=CONFIGURATIONS, default=DEFAULT_CONFIG)
    parser.add_argument("--jobs", default="8", help="Parallel build jobs passed to cmake --build.")
    parser.add_argument(
        "--configure",
        choices=("auto", "always", "never"),
        default="auto",
        help="Run CMake configure when needed, always, or never.",
    )
    parser.add_argument("-D", "--define", dest="defines", action="append", default=[], metavar="KEY=VALUE")
    parser.add_argument("--skip-build", action="store_true", help="Do not build before launching.")
    parser.add_argument("--dry-run", action="store_true", help="Print configure/build/launch commands without executing them.")
    parser.add_argument("--working-directory", type=Path, help="Override launch working directory.")
    parser.add_argument("--executable", type=Path, help="Override executable path.")
    parser.add_argument("--executable-name", help="Override executable base name when CMake metadata is unavailable.")
    parser.add_argument("--gpudbg", action="store_true", help="Append --gpudbg to the launched application.")
    parser.add_argument("--kill-existing", action="store_true", help="Close any running executable with the same name before launch.")
    parser.add_argument("--detach", action="store_true", help="Return after launch instead of waiting for the app.")


def add_smoke_options(parser: argparse.ArgumentParser, allow_positional_scene: bool) -> None:
    if allow_positional_scene:
        parser.add_argument("scene_name", nargs="?", help="Smoke scene name.")
    else:
        parser.set_defaults(scene_name=None)
    parser.add_argument("--scene", choices=sorted(SMOKE_SCENES), default="transparent-multi")
    parser.add_argument("--backend", default="hw", help="Backend variant for the scene, such as hw/sw/compute.")
    parser.add_argument("--spin-angle", help="Pin NWB_TRANSPARENT_MULTI_SPIN_ANGLE, in radians.")
    parser.add_argument("--spin-speed", help="Set NWB_TRANSPARENT_MULTI_SPIN_SPEED.")


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Configure, build, and launch NWB targets.")
    subparsers = parser.add_subparsers(dest="command", required=True)

    run_parser = subparsers.add_parser("run", help="Build and launch a CMake executable target.")
    add_common_options(run_parser)
    run_parser.add_argument("target", help="CMake executable target, such as testbed or nwb_resource_cooker.")
    run_parser.set_defaults(handler=run_target_command)

    smoke_parser = subparsers.add_parser("smoke", help="Build, cook, and launch a smoke scene profile.")
    add_common_options(smoke_parser)
    add_smoke_options(smoke_parser, allow_positional_scene=True)
    smoke_parser.set_defaults(handler=smoke_command)

    profiles_parser = subparsers.add_parser("profiles", help="List built-in launch profiles.")
    profiles_parser.set_defaults(handler=list_profiles_command)

    return parser


def make_smoke_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Build, recook, and launch an NWB smoke scene.")
    add_common_options(parser)
    add_smoke_options(parser, allow_positional_scene=False)
    parser.set_defaults(handler=smoke_command)
    return parser


def split_application_args(argv: Sequence[str]) -> Tuple[List[str], List[str]]:
    values = list(argv)
    if "--" not in values:
        return values, []
    separator = values.index("--")
    return values[:separator], values[separator + 1 :]


def main(argv: Sequence[str]) -> int:
    parser_args, application_args = split_application_args(argv)
    args = make_parser().parse_args(parser_args)
    args.application_args = application_args
    return args.handler(args)


def smoke_main(argv: Sequence[str]) -> int:
    parser_args, application_args = split_application_args(argv)
    args = make_smoke_parser().parse_args(parser_args)
    args.application_args = application_args
    return args.handler(args)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
