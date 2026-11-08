#!/usr/bin/env python3
"""Build the isolated environment for a stress measurement."""

import platform

from window_capture_smoke import SmokeFailure

LIT_ENABLED_FLAG = "1"
LIT_NWB = "NWB_"
LIT_GPU_TIMING_TXT = "gpu_timing.txt"
LIT_CPU_GPU_TIMING_TXT = "cpu_gpu_timing.txt"


class StressLaunchEnvironment:
    @staticmethod
    def launch_environment(base, args, output):
        for key in ("VK_INSTANCE_LAYERS", "VK_LOADER_LAYERS_ENABLE"):
            if base.get(key, "").strip():
                raise SmokeFailure(f"unrequested Vulkan layer override: {key}")
        result = {key: value for key, value in base.items() if not key.startswith(LIT_NWB)}
        if platform.system() == "Linux":
            result["NWB_LINUX_BACKEND"] = "x11"
        result.update(NWB_STRESS_SMOKE_TIMING=LIT_ENABLED_FLAG, NWB_STRESS_CHARACTERS_PER_CLASS=str(args.characters_per_class), NWB_STRESS_CSG_PROFILE=args.csg_profile, NWB_REFLECTION_SCREEN_STEPS=str(args.reflection_screen_steps),
            NWB_SOFTWARE_SHADOW_BACKEND=args.software_shadow_backend, NWB_SOFTWARE_SHADOW_COVERAGE=args.software_shadow_coverage, NWB_SOFTWARE_SHADOW_BLOCKER_SEARCH=args.software_shadow_blocker_search,
            NWB_SOFTWARE_SHADOW_CAPTURE_CADENCE=args.software_shadow_capture_cadence, NWB_SHADOW_TRANSPARENT_SAMPLING=args.shadow_transparent_sampling, NWB_SHADOW_RECEIVER_RESOLUTION=args.shadow_receiver_resolution,
            NWB_SOFTWARE_SHADOW_BUDGET_MIB=str(args.software_shadow_budget_mib), NWB_SOFTWARE_SHADOW_DIRECTIONAL_RESOLUTION=str(args.software_shadow_directional_resolution), NWB_SOFTWARE_SHADOW_POINT_RESOLUTION=str(args.software_shadow_point_resolution),
            NWB_CAUSTIC_PHOTON_GRID_DIVISOR=str(args.caustic_photon_grid_divisor), NWB_SURFEL_GI_RESOLVE_RESOLUTION=args.surfel_gi_resolve_resolution, NWB_GPU_TIMING_FILE=str(output / LIT_GPU_TIMING_TXT))
        if args.cpu_diagnostics:
            result.update(NWB_STRESS_CPU_DIAGNOSTICS=LIT_ENABLED_FLAG, NWB_STRESS_CPU_TIMING_FILE=str(output / LIT_CPU_GPU_TIMING_TXT))
        if not args.animate:
            result.update(NWB_STRESS_TEST_SPIN_ANGLE=str(args.spin_angle), NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS=str(args.fixed_delta_seconds))
        if args.reflection_diagnostics:
            result["NWB_STRESS_REFLECTION_DIAGNOSTICS"] = LIT_ENABLED_FLAG
        return result
