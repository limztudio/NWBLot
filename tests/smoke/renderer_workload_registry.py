"""Describe frozen renderer benchmark workloads."""

from dataclasses import dataclass, replace
from pathlib import Path
from typing import Callable

import reflection_benchmark as reflection
from reflection_gpu_coverage import ReflectionGpuCoverage
from caustic_workload import CausticWorkload, PHOTONS as CAUSTIC_PHOTONS, RESOLVE as CAUSTIC_RESOLVE, PRESETS as CAUSTIC_PRESETS
from renderer_workload_log_policy import RendererWorkloadLogPolicy
from window_capture_outcome import SmokeFailure

LIT_REFLECTION_OPTICAL_INSIDE = "reflection-optical-inside"
LIT_AVBOIT_REFRACTION_ACCUMULATION = "avboit-refraction-accumulation"
LIT_RENDER_OPAQUE_REGULAR = "render.opaque_regular"
LIT_RENDER_SHADOW_VISIBILITY = "render.shadow_visibility"
LIT_RENDER_DEFERRED_LIGHTING = "render.deferred_lighting"
LIT_RENDER_DEFERRED_COMPOSITE = "render.deferred_composite"
LIT_RENDER_DEFERRED_PRESENT = "render.deferred_present"
LIT_HARDWARE = "hardware"
LIT_OPTICAL_INSIDE = "optical_inside"
LIT_NWB_SOFT_SHADOW_TEST_ANGLE = "NWB_SOFT_SHADOW_TEST_ANGLE"
LIT_NWB_SOFT_SHADOW_TEST_SOURCE_RADIUS = "NWB_SOFT_SHADOW_TEST_SOURCE_RADIUS"
LIT_NWB_GPU_TIMING_FILE = "NWB_GPU_TIMING_FILE"
LIT_TRANSPARENT_MULTI = "transparent-multi"
LIT_NWB_RENDERER_BASELINE_FIXED_DELTA_SECO = "NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS"
LIT_N_0_016666667 = "0.016666667"
LIT_ROUGH = "rough"
LIT_N_0_03 = "0.03"
LIT_N_0_15 = "0.15"
FRAME = "render.frame"
OCCUPANCY = "render.avboit_occupancy"
CONTROLS = (LIT_RENDER_OPAQUE_REGULAR, LIT_RENDER_SHADOW_VISIBILITY, LIT_RENDER_DEFERRED_LIGHTING)
OBSERVATIONS = (LIT_RENDER_DEFERRED_COMPOSITE, LIT_RENDER_DEFERRED_PRESENT)
AVBOIT = ("render.avboit_clear", OCCUPANCY, "render.avboit_depth_warp", "render.avboit_extinction", "render.avboit_integration", "render.avboit_accumulate")
SHADOW_CONTROLS = (LIT_RENDER_OPAQUE_REGULAR, LIT_RENDER_DEFERRED_LIGHTING, LIT_RENDER_DEFERRED_COMPOSITE, LIT_RENDER_DEFERRED_PRESENT)
SHADOW_PHASES = (LIT_RENDER_SHADOW_VISIBILITY, "render.shadow_opaque_trace", "render.shadow_geometry_downsample", "render.shadow_opaque_temporal", "render.shadow_opaque_resolve",
    "render.shadow_transparent_trace", "render.shadow_transparent_temporal", "render.shadow_transparent_resolve")
SHADOW_INACTIVE = ("render.caustic_photons", "render.caustic_resolve")


@dataclass(frozen=True)
class ReflectionPolicy:
    family: str
    variant: reflection.Variant
    roughness: float
    history_samples: int = 16
    ray_budget: int = 1382400
    optical_queries: int = 16
    screen_steps: int = 96
    sampling_seed: int = 0


@dataclass(frozen=True)
class Workload:
    name: str
    width: int
    height: int
    # AVBOIT raster timing records one range per frame across the whole pass.
    scope_multipliers: tuple
    secondary_scope: str
    environment_overrides: tuple
    validate_log: Callable[[str, "Workload", bool], dict]
    reflection_policy: ReflectionPolicy = None
    inactive_scopes: tuple = ()
    control_scopes: tuple = CONTROLS

    def __post_init__(self):
        controls = self.control_scopes
        if not isinstance(controls, tuple) or not controls or len(set(controls)) != len(controls):
            raise SmokeFailure("workload controls must be a nonempty unique tuple")
        if any(scope not in self.scopes for scope in controls):
            raise SmokeFailure("workload control is not an observed scope")
        if FRAME in controls or self.secondary_scope in controls:
            raise SmokeFailure("workload target cannot also be a control")

    @property
    def observed_scopes(self):
        # Decode inactive scope hashes so unexpected scopes still fail zero-coverage checks.
        return self.scopes + self.inactive_scopes

    @property
    def scopes(self):
        return tuple(scope for scope, _ in self.scope_multipliers)


class RendererWorkloadRegistry:
    @staticmethod
    def reflection_workload(name, family, mode, roughness, temporal, spatial, target_scope):
        variant = reflection.Variant(name, mode, temporal=temporal, spatial=spatial, feedback=False)
        policy = ReflectionPolicy(family, variant, roughness)
        required = tuple(ReflectionGpuCoverage.required_scopes(variant, policy.history_samples))
        inactive = tuple(scope for scope in reflection.KERNELS if scope not in required)
        workload = Workload(name, 960, 720, tuple((scope, 10 if scope == reflection.DEPTH else 1) for scope in required), target_scope, (), RendererWorkloadLogPolicy.reflection_log, policy, inactive)
        _, overrides = reflection.timed_environment({}, RendererWorkloadLogPolicy.reflection_arguments(workload), variant, Path("timing.txt"))
        del overrides[LIT_NWB_GPU_TIMING_FILE]
        return Workload(name, workload.width, workload.height, workload.scope_multipliers, target_scope, tuple(overrides.items()), RendererWorkloadLogPolicy.reflection_log, policy, inactive)

    @staticmethod
    def workloads():
        scopes = (FRAME, *CONTROLS, *OBSERVATIONS, *AVBOIT)
        result = {LIT_TRANSPARENT_MULTI: Workload(LIT_TRANSPARENT_MULTI, 1280, 900, tuple((scope, 1) for scope in scopes), OCCUPANCY, (("NWB_AVBOIT_SMOKE_TIMING", "1"), ("NWB_TRANSPARENT_MULTI_SPIN_ANGLE", "0"),
             (LIT_NWB_RENDERER_BASELINE_FIXED_DELTA_SECO, LIT_N_0_016666667)), RendererWorkloadLogPolicy.transparent_multi_log)}
        definitions = (("reflection-rough-spatial", LIT_ROUGH, LIT_HARDWARE, .4, False, True, reflection.SPATIAL), ("reflection-mirror-spatial", LIT_ROUGH, LIT_HARDWARE, 0.0, False, True, reflection.SPATIAL),
            ("reflection-rough-filtered", LIT_ROUGH, LIT_HARDWARE, .4, True, True, reflection.SPATIAL), ("reflection-screen-depth", "floor", "screen", 0.0, False, False, reflection.DEPTH),
            ("reflection-optical-clear", "optical_clear", LIT_HARDWARE, 0.0, False, False, reflection.HARDWARE), ("reflection-optical-csg-cap", "optical_csg_cap", LIT_HARDWARE, 0.0, False, False, reflection.HARDWARE),
            (LIT_REFLECTION_OPTICAL_INSIDE, LIT_OPTICAL_INSIDE, LIT_HARDWARE, 0.0, False, False, reflection.HARDWARE),)
        for definition in definitions:
            workload = RendererWorkloadRegistry.reflection_workload(*definition)
            result[workload.name] = workload
        refraction_accumulation = result[LIT_REFLECTION_OPTICAL_INSIDE]
        result[LIT_AVBOIT_REFRACTION_ACCUMULATION] = replace(refraction_accumulation, name=LIT_AVBOIT_REFRACTION_ACCUMULATION, scope_multipliers=refraction_accumulation.scope_multipliers + tuple((scope, 1) for scope in AVBOIT),
            secondary_scope="render.avboit_accumulate")
        caustic_scopes = (FRAME, *CONTROLS, *OBSERVATIONS, *AVBOIT, CAUSTIC_PHOTONS, CAUSTIC_RESOLVE)
        for preset in CAUSTIC_PRESETS:
            name = "caustic-" + preset
            result[name] = Workload(name, 1280, 900, tuple((scope, 1) for scope in caustic_scopes), CAUSTIC_RESOLVE, tuple(CausticWorkload.environment(preset).items()), RendererWorkloadLogPolicy.caustic_log, inactive_scopes=(reflection.DEPTH, reflection.BUILD_ARGS, reflection.HARDWARE,
                    reflection.TEMPORAL, reflection.SPATIAL), control_scopes=(*CONTROLS, *OBSERVATIONS, CAUSTIC_PHOTONS))
        shadow_scopes = (FRAME, *SHADOW_CONTROLS, *SHADOW_PHASES)
        for name, angle, radius in (("shadow-zero-extent", "0", "0"), ("shadow-finite-extent", LIT_N_0_03, LIT_N_0_15), ("shadow-zero-directional", "0", LIT_N_0_15), ("shadow-zero-punctual", LIT_N_0_03, "0")):
            result[name] = Workload(name, 1280, 900, tuple((scope, 1) for scope in shadow_scopes), LIT_RENDER_SHADOW_VISIBILITY, (("NWB_SOFT_SHADOW_TEST_TIMING", "1"), (LIT_NWB_SOFT_SHADOW_TEST_ANGLE, angle), (LIT_NWB_SOFT_SHADOW_TEST_SOURCE_RADIUS, radius),
                    ("NWB_SOFT_SHADOW_TEST_SPIN_ANGLE", "0.6"), (LIT_NWB_RENDERER_BASELINE_FIXED_DELTA_SECO, LIT_N_0_016666667)), RendererWorkloadLogPolicy.soft_shadow_log, inactive_scopes=SHADOW_INACTIVE, control_scopes=SHADOW_CONTROLS)
        return result
