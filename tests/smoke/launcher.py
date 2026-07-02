#!/usr/bin/env python3
import argparse
import os
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Sequence, Tuple


def _load_root_launcher():
    root = Path(__file__).resolve().parents[2]
    sys.path.insert(0, str(root))
    import launcher

    return launcher


ROOT_LAUNCHER = _load_root_launcher()


@dataclass(frozen=True)
class SmokeExecutable:
    target: str
    executable: str


@dataclass(frozen=True)
class SmokeScene:
    runtime: str
    backends: Dict[str, SmokeExecutable]


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


def build_smoke_environment(args) -> Dict[str, str]:
    env = os.environ.copy()
    if args.spin_angle is not None:
        env["NWB_TRANSPARENT_MULTI_SPIN_ANGLE"] = args.spin_angle
    if args.spin_speed is not None:
        env["NWB_TRANSPARENT_MULTI_SPIN_SPEED"] = args.spin_speed
    return env


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
    env = build_smoke_environment(args)
    settings = ROOT_LAUNCHER.resolve_launch_settings(args, ROOT_LAUNCHER.DEFAULT_DOMAIN)
    ROOT_LAUNCHER.maybe_configure(
        args,
        settings,
        ROOT_LAUNCHER.merged_required_defines(SMOKE_REQUIRED_DEFINES, ROOT_LAUNCHER.profile_required_defines(args)),
        env,
    )
    settings = ROOT_LAUNCHER.refresh_launch_settings(settings, args.domain)
    ROOT_LAUNCHER.build_target(args, settings, smoke_executable.target, env)
    ROOT_LAUNCHER.build_profile_targets(args, settings, env)

    executable = ROOT_LAUNCHER.resolve_executable_path(
        settings,
        smoke_executable.target,
        args.executable,
        args.executable_name or smoke_executable.executable,
        args.dry_run,
    )
    working_directory = ROOT_LAUNCHER.resolve_working_directory(
        settings,
        args.working_directory,
        settings.build_dir / "Testing" / scene.runtime / settings.config,
    )
    return ROOT_LAUNCHER.launch_with_optional_profile(
        args,
        settings,
        executable,
        working_directory,
        env,
        ROOT_LAUNCHER.normalize_application_args(args.application_args),
    )


def profiles_command(_args) -> int:
    print("smoke scenes:")
    for name in sorted(SMOKE_SCENES):
        scene = SMOKE_SCENES[name]
        backends = ", ".join(sorted(scene.backends))
        print(f"  smoke {name} --backend {{{backends}}}")
    return 0


def add_smoke_options(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("scene_name", nargs="?", help="Smoke scene name.")
    parser.add_argument("--scene", choices=sorted(SMOKE_SCENES), default="transparent-multi")
    parser.add_argument("--backend", default="hw", help="Backend variant for the scene, such as hw/sw/compute.")
    parser.add_argument("--spin-angle", help="Pin NWB_TRANSPARENT_MULTI_SPIN_ANGLE, in radians.")
    parser.add_argument("--spin-speed", help="Set NWB_TRANSPARENT_MULTI_SPIN_SPEED.")


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Build, recook, and launch an NWB smoke scene.")
    ROOT_LAUNCHER.add_common_options(parser)
    parser.add_argument("--profiles", action="store_true", help="List available smoke scene profiles.")
    add_smoke_options(parser)
    return parser


def split_application_args(argv: Sequence[str]) -> Tuple[List[str], List[str]]:
    return ROOT_LAUNCHER.split_application_args(argv)


def main(argv):
    parser_args, application_args = split_application_args(argv)
    args = make_parser().parse_args(parser_args)
    args.application_args = application_args
    if args.profiles:
        return profiles_command(args)
    return smoke_command(args)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
