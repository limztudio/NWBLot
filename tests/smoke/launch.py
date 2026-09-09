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
    "NWB_BUILD_LOADER": "ON",
    "NWB_BUILD_LOGSERVER": "ON",
    "NWB_BUILD_PIPELINE": "ON",
    "NWB_BUILD_TESTS": "ON",
    "NWB_BUILD_UTILITIES": "ON",
}


SMOKE_SCENES = {
    "reflection": SmokeScene(
        runtime="smoke_runtime",
        backends={
            "native": SmokeExecutable("nwb_reflection_smoke", "reflection_smoke"),
        },
    ),
    "refraction": SmokeScene(
        runtime="smoke_runtime",
        backends={
            "native": SmokeExecutable("nwb_refraction_smoke", "refraction_smoke"),
        },
    ),
    "transparent-multi": SmokeScene(
        runtime="smoke_runtime",
        backends={
            "native": SmokeExecutable("nwb_transparent_multi_smoke", "transparent_multi_smoke"),
        },
    ),
    "transparent-csg": SmokeScene(
        runtime="smoke_runtime",
        backends={
            "native": SmokeExecutable("nwb_transparent_csg_smoke", "transparent_csg_smoke"),
        },
    ),
    "texture": SmokeScene(
        runtime="texture_smoke_runtime",
        backends={
            "native": SmokeExecutable("nwb_texture_smoke", "texture_smoke"),
        },
    ),
    "caustic-sphere": SmokeScene(
        runtime="smoke_runtime",
        backends={
            "native": SmokeExecutable("nwb_caustic_sphere_smoke", "caustic_sphere_smoke"),
        },
    ),
    "csg-visible": SmokeScene(
        runtime="csg_visible_smoke_runtime",
        backends={
            "native": SmokeExecutable("nwb_csg_visible_smoke", "csg_visible_smoke"),
        },
    ),
    "csg-skinned-visible": SmokeScene(
        runtime="csg_skinned_visible_smoke_runtime",
        backends={
            "native": SmokeExecutable("nwb_csg_skinned_visible_smoke", "csg_skinned_visible_smoke"),
        },
    ),
    "csg-skinned-sphere-visible": SmokeScene(
        runtime="csg_skinned_visible_smoke_runtime",
        backends={
            "native": SmokeExecutable("nwb_csg_skinned_sphere_visible_smoke", "csg_skinned_sphere_visible_smoke"),
        },
    ),
    "csg-skinned-transparent-sphere-visible": SmokeScene(
        runtime="csg_skinned_visible_smoke_runtime",
        backends={
            "native": SmokeExecutable(
                "nwb_csg_skinned_transparent_sphere_visible_smoke",
                "csg_skinned_transparent_sphere_visible_smoke",
            ),
        },
    ),
    "skinning-culling-benchmark": SmokeScene(
        runtime="skinning_culling_benchmark_runtime",
        backends={
            "native": SmokeExecutable("nwb_skinning_culling_benchmark", "skinning_culling_benchmark"),
        },
    ),
    "skinned-caustic": SmokeScene(
        runtime="skinning_culling_benchmark_runtime",
        backends={
            "native": SmokeExecutable("nwb_skinned_caustic_smoke", "skinned_caustic_smoke"),
        },
    ),
    "stress-test": SmokeScene(
        runtime="skinning_culling_benchmark_runtime",
        backends={
            "native": SmokeExecutable("nwb_stress_test_smoke", "stress_test_smoke"),
        },
    ),
    "flicker-test": SmokeScene(
        runtime="skinning_culling_benchmark_runtime",
        backends={
            "native": SmokeExecutable("nwb_flicker_test_smoke", "flicker_test_smoke"),
        },
    ),
    "soft-shadow-test": SmokeScene(
        runtime="skinning_culling_benchmark_runtime",
        backends={
            "native": SmokeExecutable("nwb_soft_shadow_test_smoke", "soft_shadow_test_smoke"),
        },
    ),
    "gi-test": SmokeScene(
        runtime="skinning_culling_benchmark_runtime",
        backends={
            "native": SmokeExecutable("nwb_gi_test_smoke", "gi_test_smoke"),
        },
    ),
}


def build_smoke_environment(args) -> Dict[str, str]:
    env = os.environ.copy()
    if getattr(args, "refraction_case", None):
        env["NWB_REFRACTION_SMOKE_CASE"] = args.refraction_case
        env["NWB_REFRACTION_SMOKE_GEOMETRY"] = "1" if getattr(args, "refraction_geometry", False) else "0"
        if getattr(args, "refraction_geometry", False):
            env["NWB_REFRACTION_SMOKE_ENABLED"] = "0"
    elif getattr(args, "refraction_geometry", False):
        raise SystemExit("--refraction-geometry requires --refraction-case")
    if getattr(args, "reflection_case", None):
        env["NWB_REFLECTION_SMOKE_CASE"] = args.reflection_case
    if getattr(args, "reflection_mode", None):
        env["NWB_REFLECTION_SMOKE_MODE"] = args.reflection_mode
    if getattr(args, "reflection_debug", None):
        env["NWB_REFLECTION_SMOKE_DEBUG"] = args.reflection_debug
    if getattr(args, "reflection_ray_budget", None) is not None:
        env["NWB_REFLECTION_SMOKE_RAY_BUDGET"] = str(args.reflection_ray_budget)
    for option, name in (("roughness", "ROUGHNESS"), ("history_samples", "HISTORY_SAMPLES"),
        ("post_reset_samples", "POST_RESET_SAMPLES"), ("seed", "SEED")):
        value = getattr(args, "reflection_" + option, None)
        if value is not None:
            env["NWB_REFLECTION_SMOKE_" + name] = str(value)
    for option in ("temporal", "spatial", "diagnostics", "final_state"):
        value = getattr(args, "reflection_" + option, None)
        if value is not None:
            env["NWB_REFLECTION_SMOKE_" + option.upper()] = "1" if value == "on" else "0"
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


def reflection_ray_budget(value: str) -> int:
    try:
        budget = int(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("reflection ray budget must be a nonnegative u32") from error
    if budget < 0 or budget > 0xffffffff:
        raise argparse.ArgumentTypeError("reflection ray budget must be a nonnegative u32")
    return budget


def add_smoke_options(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("scene_name", nargs="?", help="Smoke scene name.")
    parser.add_argument("--scene", choices=sorted(SMOKE_SCENES), default="transparent-multi")
    parser.add_argument("--backend", default="native", help="Backend variant for the scene; currently native.")
    parser.add_argument("--spin-angle", help="Pin NWB_TRANSPARENT_MULTI_SPIN_ANGLE, in radians.")
    parser.add_argument("--spin-speed", help="Set NWB_TRANSPARENT_MULTI_SPIN_SPEED.")
    parser.add_argument("--reflection-case", choices=("offscreen", "moved", "opaque_glass", "onscreen", "onscreen_moved", "boundary", "floor",
        "rough", "rough_furnace", "rough_glass", "rough_deform", "temporal_camera", "temporal_transform", "temporal_material", "temporal_light", "temporal_deform"),
        help="Select the reflection fixture; defaults to offscreen mirror markers.")
    parser.add_argument("--reflection-mode", choices=("disabled", "screen", "hardware", "hybrid"),
        help="Select the typed reflection trace mode; the fixture defaults to hardware.")
    parser.add_argument("--reflection-debug", choices=("none", "source", "confidence"),
        help="Select a reflection debug view for interactive inspection; capture assertions use normal radiance.")
    parser.add_argument("--reflection-ray-budget", type=reflection_ray_budget,
        help="Set the typed maximum hardware reflection rays per frame, including zero.")
    parser.add_argument("--reflection-roughness", type=float, help="Authored perceptual roughness in [0,1] for the rough fixture.")
    parser.add_argument("--reflection-history-samples", type=int, choices=range(1, 257), metavar="1..256",
        help="Accepted temporal sample cap for the rough fixture.")
    parser.add_argument("--reflection-post-reset-samples", type=int, choices=range(1, 257), metavar="1..256",
        help="Capture at the first reset frame or after this many accepted post-reset samples.")
    parser.add_argument("--reflection-seed", type=reflection_ray_budget, help="Deterministic u32 sampling seed.")
    for option in ("temporal", "spatial", "diagnostics", "final-state"):
        parser.add_argument("--reflection-" + option, choices=("on", "off"),
            help="Set the test fixture's typed " + option + " control.")
    parser.add_argument("--refraction-case", choices=(
        "single", "separate", "stacked", "intersecting", "nested", "coincident",
        "coincident_tinted", "torus", "same_mesh", "prism",
        "duplicate_single_cool", "duplicate_single_warm", "duplicate_single_cool_tinted", "duplicate_single_warm_tinted",
        "coincident_reversed", "coincident_tinted_reversed", "coincident_priority_swap", "coincident_tinted_priority_swap",
        "coincident_identical", "coincident_tinted_identical", "near_coincident", "coincident_preserved",
    ), help="Select a visual refraction case; omit to run the original regression scene.")
    parser.add_argument("--refraction-geometry", action="store_true",
        help="Show the selected case with colored, nonrefractive translucent surfaces.")


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
