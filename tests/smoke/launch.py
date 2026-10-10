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


DEFINE_ON = "ON"
RUNTIME_SMOKE = "smoke_runtime"
RUNTIME_TEXTURE_SMOKE = "texture_smoke_runtime"
RUNTIME_CSG_VISIBLE_SMOKE = "csg_visible_smoke_runtime"
RUNTIME_CSG_SKINNED_VISIBLE_SMOKE = "csg_skinned_visible_smoke_runtime"
RUNTIME_SKINNING_CULLING_BENCHMARK = "skinning_culling_benchmark_runtime"
DEFAULT_SCENE = "transparent-multi"
ENV_REFLECTION_PREFIX = "NWB_REFLECTION_SMOKE_"
ENV_REFRACTION_CASE = "NWB_REFRACTION_SMOKE_CASE"
ENV_REFRACTION_GEOMETRY = "NWB_REFRACTION_SMOKE_GEOMETRY"
ENV_REFRACTION_ENABLED = "NWB_REFRACTION_SMOKE_ENABLED"
ENV_TRANSPARENT_SPIN_ANGLE = "NWB_TRANSPARENT_MULTI_SPIN_ANGLE"
ENV_TRANSPARENT_SPIN_SPEED = "NWB_TRANSPARENT_MULTI_SPIN_SPEED"
LIST_SEPARATOR = ", "
FLAG_VALUE_ON = "on"
ENABLED_VALUE = "1"
DISABLED_VALUE = "0"
MSG_UNKNOWN_SCENE = "unknown smoke scene '{scene}' (valid: {valid})"
MSG_NO_BACKEND = "scene '{scene_name}' does not have backend '{backend}' (valid: {valid})"
MSG_SCENES_HEADER = "smoke scenes:"
MSG_SCENE_LINE = "  smoke {name} --backend {{{backends}}}"
MSG_RAY_BUDGET = "reflection ray budget must be a nonnegative u32"
TESTING_DIR = "Testing"
MAIN_ENTRY = "__main__"
ATTR_REFRACTION_GEOMETRY = "refraction_geometry"
ATTR_REFLECTION_PREFIX = "reflection_"
REFLECTION_OPT_TEMPORAL = "temporal"
REFLECTION_OPT_SPATIAL = "spatial"
REFLECTION_OPT_DIAGNOSTICS = "diagnostics"
REFLECTION_OPT_FEEDBACK = "feedback"
SAMPLES_METAVAR = "1..256"
ACTION_STORE_TRUE = "store_true"
BACKEND_NATIVE_LABEL = "native"
BACKEND_NATIVE = BACKEND_NATIVE_LABEL


@dataclass(frozen=True)
class SmokeExecutable:
    target: str
    executable: str


@dataclass(frozen=True)
class SmokeScene:
    runtime: str
    backends: Dict[str, SmokeExecutable]


SMOKE_REQUIRED_DEFINES = {
    "NWB_BUILD_LOADER": DEFINE_ON,
    "NWB_BUILD_LOGSERVER": DEFINE_ON,
    "NWB_BUILD_PIPELINE": DEFINE_ON,
    "NWB_BUILD_TESTS": DEFINE_ON,
    "NWB_BUILD_UTILITIES": DEFINE_ON,
}


SMOKE_SCENES = {
    "reflection": SmokeScene(
        runtime=RUNTIME_SMOKE,
        backends={
            BACKEND_NATIVE: SmokeExecutable("nwb_reflection_smoke", "reflection_smoke"),
        },
    ),
    "refraction": SmokeScene(
        runtime=RUNTIME_SMOKE,
        backends={
            BACKEND_NATIVE: SmokeExecutable("nwb_refraction_smoke", "refraction_smoke"),
        },
    ),
    "transparent-multi": SmokeScene(
        runtime=RUNTIME_SMOKE,
        backends={
            BACKEND_NATIVE: SmokeExecutable("nwb_transparent_multi_smoke", "transparent_multi_smoke"),
        },
    ),
    "transparent-csg": SmokeScene(
        runtime=RUNTIME_SMOKE,
        backends={
            BACKEND_NATIVE: SmokeExecutable("nwb_transparent_csg_smoke", "transparent_csg_smoke"),
        },
    ),
    "texture": SmokeScene(
        runtime=RUNTIME_TEXTURE_SMOKE,
        backends={
            BACKEND_NATIVE: SmokeExecutable("nwb_texture_smoke", "texture_smoke"),
        },
    ),
    "caustic-sphere": SmokeScene(
        runtime=RUNTIME_SMOKE,
        backends={
            BACKEND_NATIVE: SmokeExecutable("nwb_caustic_sphere_smoke", "caustic_sphere_smoke"),
        },
    ),
    "csg-visible": SmokeScene(
        runtime=RUNTIME_CSG_VISIBLE_SMOKE,
        backends={
            BACKEND_NATIVE: SmokeExecutable("nwb_csg_visible_smoke", "csg_visible_smoke"),
        },
    ),
    "csg-skinned-visible": SmokeScene(
        runtime=RUNTIME_CSG_SKINNED_VISIBLE_SMOKE,
        backends={
            BACKEND_NATIVE: SmokeExecutable("nwb_csg_skinned_visible_smoke", "csg_skinned_visible_smoke"),
        },
    ),
    "csg-skinned-sphere-visible": SmokeScene(
        runtime=RUNTIME_CSG_SKINNED_VISIBLE_SMOKE,
        backends={
            BACKEND_NATIVE: SmokeExecutable("nwb_csg_skinned_sphere_visible_smoke", "csg_skinned_sphere_visible_smoke"),
        },
    ),
    "csg-skinned-transparent-sphere-visible": SmokeScene(
        runtime=RUNTIME_CSG_SKINNED_VISIBLE_SMOKE,
        backends={
            BACKEND_NATIVE: SmokeExecutable(
                "nwb_csg_skinned_transparent_sphere_visible_smoke",
                "csg_skinned_transparent_sphere_visible_smoke",
            ),
        },
    ),
    "skinning-culling-benchmark": SmokeScene(
        runtime=RUNTIME_SKINNING_CULLING_BENCHMARK,
        backends={
            BACKEND_NATIVE: SmokeExecutable("nwb_skinning_culling_benchmark", "skinning_culling_benchmark"),
        },
    ),
    "skinned-caustic": SmokeScene(
        runtime=RUNTIME_SKINNING_CULLING_BENCHMARK,
        backends={
            BACKEND_NATIVE: SmokeExecutable("nwb_skinned_caustic_smoke", "skinned_caustic_smoke"),
        },
    ),
    "stress-test": SmokeScene(
        runtime=RUNTIME_SKINNING_CULLING_BENCHMARK,
        backends={
            BACKEND_NATIVE: SmokeExecutable("nwb_stress_test_smoke", "stress_test_smoke"),
        },
    ),
    "flicker-test": SmokeScene(
        runtime=RUNTIME_SKINNING_CULLING_BENCHMARK,
        backends={
            BACKEND_NATIVE: SmokeExecutable("nwb_flicker_test_smoke", "flicker_test_smoke"),
        },
    ),
    "soft-shadow-test": SmokeScene(
        runtime=RUNTIME_SKINNING_CULLING_BENCHMARK,
        backends={
            BACKEND_NATIVE: SmokeExecutable("nwb_soft_shadow_test_smoke", "soft_shadow_test_smoke"),
        },
    ),
    "gi-test": SmokeScene(
        runtime=RUNTIME_SKINNING_CULLING_BENCHMARK,
        backends={
            BACKEND_NATIVE: SmokeExecutable("nwb_gi_test_smoke", "gi_test_smoke"),
        },
    ),
}


def build_smoke_environment(args) -> Dict[str, str]:
    env = os.environ.copy()
    if getattr(args, "refraction_case", None):
        env[ENV_REFRACTION_CASE] = args.refraction_case
        env[ENV_REFRACTION_GEOMETRY] = ENABLED_VALUE if getattr(args, ATTR_REFRACTION_GEOMETRY, False) else DISABLED_VALUE
        if getattr(args, ATTR_REFRACTION_GEOMETRY, False):
            env[ENV_REFRACTION_ENABLED] = "0"
    elif getattr(args, ATTR_REFRACTION_GEOMETRY, False):
        raise SystemExit("--refraction-geometry requires --refraction-case")
    if getattr(args, "reflection_case", None):
        env[ENV_REFLECTION_PREFIX + "CASE"] = args.reflection_case
    if getattr(args, "reflection_mode", None):
        env[ENV_REFLECTION_PREFIX + "MODE"] = args.reflection_mode
    if getattr(args, "reflection_debug", None):
        env[ENV_REFLECTION_PREFIX + "DEBUG"] = args.reflection_debug
    if getattr(args, "reflection_ray_budget", None) is not None:
        env[ENV_REFLECTION_PREFIX + "RAY_BUDGET"] = str(args.reflection_ray_budget)
    for option, name in (("roughness", "ROUGHNESS"), ("history_samples", "HISTORY_SAMPLES"),
        ("post_reset_samples", "POST_RESET_SAMPLES"), ("seed", "SEED"), ("optical_queries", "OPTICAL_QUERIES"),
        ("screen_steps", "SCREEN_STEPS"), ("extent", "EXTENT")):
        value = getattr(args, ATTR_REFLECTION_PREFIX + option, None)
        if value is not None:
            env[ENV_REFLECTION_PREFIX + name] = str(value)
    for option in (REFLECTION_OPT_TEMPORAL, REFLECTION_OPT_SPATIAL, REFLECTION_OPT_DIAGNOSTICS, "final_state", REFLECTION_OPT_FEEDBACK):
        value = getattr(args, ATTR_REFLECTION_PREFIX + option, None)
        if value is not None:
            env[ENV_REFLECTION_PREFIX + option.upper()] = ENABLED_VALUE if value == FLAG_VALUE_ON else DISABLED_VALUE
    if args.spin_angle is not None:
        env[ENV_TRANSPARENT_SPIN_ANGLE] = args.spin_angle
    if args.spin_speed is not None:
        env[ENV_TRANSPARENT_SPIN_SPEED] = args.spin_speed
    return env


def smoke_scene_name(args) -> str:
    scene = args.scene_name or args.scene
    if scene not in SMOKE_SCENES:
        valid = LIST_SEPARATOR.join(sorted(SMOKE_SCENES))
        raise SystemExit(MSG_UNKNOWN_SCENE.format(scene=scene, valid=valid))
    return scene


def smoke_command(args) -> int:
    scene_name = smoke_scene_name(args)
    scene = SMOKE_SCENES[scene_name]
    if args.backend not in scene.backends:
        valid = LIST_SEPARATOR.join(sorted(scene.backends))
        raise SystemExit(MSG_NO_BACKEND.format(scene_name=scene_name, backend=args.backend, valid=valid))

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
        settings.build_dir / TESTING_DIR / scene.runtime / settings.config,
    )
    return ROOT_LAUNCHER.launch_with_optional_profile(
        args,
        settings,
        executable,
        working_directory,
        env,
        args.application_args,
    )


def profiles_command(_args) -> int:
    print(MSG_SCENES_HEADER)
    for name in sorted(SMOKE_SCENES):
        scene = SMOKE_SCENES[name]
        backends = LIST_SEPARATOR.join(sorted(scene.backends))
        print(MSG_SCENE_LINE.format(name=name, backends=backends))
    return 0


def reflection_ray_budget(value: str) -> int:
    try:
        budget = int(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError(MSG_RAY_BUDGET) from error
    if budget < 0 or budget > 0xffffffff:
        raise argparse.ArgumentTypeError(MSG_RAY_BUDGET)
    return budget


def add_smoke_options(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("scene_name", nargs="?", help="Smoke scene name.")
    parser.add_argument("--scene", choices=sorted(SMOKE_SCENES), default=DEFAULT_SCENE)
    parser.add_argument("--backend", default=BACKEND_NATIVE, help="Backend variant for the scene; currently native.")
    parser.add_argument("--spin-angle", help="Pin NWB_TRANSPARENT_MULTI_SPIN_ANGLE, in radians.")
    parser.add_argument("--spin-speed", help="Set NWB_TRANSPARENT_MULTI_SPIN_SPEED.")
    parser.add_argument("--reflection-case", choices=("offscreen", "moved", "opaque_glass", "onscreen", "onscreen_moved", "boundary", "floor",
        "feedback_boundary", "feedback_mutation", "feedback_long_miss",
        "rough", "rough_furnace", "rough_glass", "rough_deform", "temporal_camera", "temporal_transform", "temporal_material", "temporal_light", "temporal_deform",
        "optical_reference", "optical_csg_reference", "optical_csg_cap", "optical_csg_cavity", "optical_sliver", "optical_csg_sliver",
        "optical_sub_ulp", "optical_csg_sub_ulp", "optical_group_gap", "optical_csg_group_gap",
        "optical_group_gap_sub_ulp", "optical_csg_group_gap_sub_ulp", "optical_group_entry", "optical_csg_group_entry",
        "optical_clear", "optical_tinted", "optical_tilted", "optical_nested2", "optical_nested3",
        "optical_priority_a", "optical_priority_b", "optical_alpha_before", "optical_alpha_after", "optical_duplicate_identical",
        "optical_duplicate_group", "optical_duplicate_reverse", "optical_mirrored", "optical_disconnected", "optical_same_mesh",
        "optical_torus", "optical_inside", "optical_inside_nested", "optical_unspecified", "optical_mixed", "optical_overflow", "optical_tir",
        "optical_union_single", "optical_union_same_mesh", "optical_coincident_independent", "optical_priority_tie_a", "optical_priority_tie_b"),
        help="Select the reflection fixture; defaults to offscreen mirror markers.")
    parser.add_argument("--reflection-mode", choices=("disabled", "screen", "hardware", "hybrid"),
        help="Select the typed reflection trace mode; the fixture defaults to hardware.")
    parser.add_argument("--reflection-debug", choices=("none", "source", "confidence"),
        help="Select a reflection debug view for interactive inspection; capture assertions use normal radiance.")
    parser.add_argument("--reflection-ray-budget", type=reflection_ray_budget,
        help="Set the typed maximum hardware reflection rays per frame, including zero.")
    parser.add_argument("--reflection-roughness", type=float, help="Authored perceptual roughness in [0,1] for the rough fixture.")
    parser.add_argument("--reflection-history-samples", type=int, choices=range(1, 257), metavar=SAMPLES_METAVAR,
        help="Accepted temporal sample cap for the rough fixture.")
    parser.add_argument("--reflection-post-reset-samples", type=int, choices=range(1, 257), metavar=SAMPLES_METAVAR,
        help="Capture at the first reset frame or after this many accepted post-reset samples.")
    parser.add_argument("--reflection-seed", type=reflection_ray_budget, help="Deterministic u32 sampling seed.")
    parser.add_argument("--reflection-optical-queries", type=int, choices=range(1, 17), metavar="1..16",
        help="Typed maximum scene queries per reflected optical path, including bootstrap and continuations.")
    parser.add_argument("--reflection-screen-steps", type=int, choices=range(8, 257), metavar="8..256",
        help="Typed bound on actual screen traversal iterations.")
    parser.add_argument("--reflection-extent", choices=(BACKEND_NATIVE, "npot"),
        help="Use the 960x720 standard framebuffer or the fixed 953x713 partial-workgroup fixture.")
    for option in (REFLECTION_OPT_TEMPORAL, REFLECTION_OPT_SPATIAL, REFLECTION_OPT_DIAGNOSTICS, "final-state", REFLECTION_OPT_FEEDBACK):
        parser.add_argument("--reflection-" + option, choices=("on", "off"),
            help="Set the test fixture's typed " + option + " control.")
    parser.add_argument("--refraction-case", choices=(
        "single", "separate", "stacked", "intersecting", "nested", "coincident",
        "coincident_tinted", "torus", "same_mesh", "prism",
        "duplicate_single_cool", "duplicate_single_warm", "duplicate_single_cool_tinted", "duplicate_single_warm_tinted",
        "coincident_reversed", "coincident_tinted_reversed", "coincident_priority_swap", "coincident_tinted_priority_swap",
        "coincident_identical", "coincident_tinted_identical", "near_coincident", "coincident_preserved",
        "csg_reference", "csg_cap", "csg_middle", "csg_uncut",
    ), help="Select a visual refraction case; omit to run the original regression scene.")
    parser.add_argument("--refraction-geometry", action=ACTION_STORE_TRUE,
        help="Show the selected case with colored, nonrefractive translucent surfaces.")


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Build, recook, and launch an NWB smoke scene.")
    ROOT_LAUNCHER.add_common_options(parser)
    parser.add_argument("--profiles", action=ACTION_STORE_TRUE, help="List available smoke scene profiles.")
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


if __name__ == MAIN_ENTRY:
    raise SystemExit(main(sys.argv[1:]))
