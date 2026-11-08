"""Validate frozen renderer workload runtime logs."""

from dataclasses import asdict
import math
import re
from types import SimpleNamespace

import reflection_benchmark as reflection
from caustic_workload import CausticWorkload
from window_capture_smoke import STRICT_LOG_FAILURE_MESSAGES, SmokeFailure
from window_capture_validation import validate_expected_log_text

LIT_HARDWARE = "hardware"
LIT_HYBRID = "hybrid"
LIT_SOFTWARE = "software"
LIT_FRAMEBUFFERCAPTURE = "FramebufferCapture:"
LIT_RENDER_SUBMISSION_SUSPENDED = "render submission suspended"
LIT_OPTICAL_INSIDE = "optical_inside"
LIT_EXTENT = "extent"
LIT_TIMING_IN_FLIGHT_RANGES = "timing_in_flight_ranges"
LIT_REFLECTION_POLICY = "reflection_policy"
LIT_REFLECTIONSMOKESTATISTICS = "ReflectionSmokeStatistics:"
LIT_REFLECTIONSMOKEHISTORY = "ReflectionSmokeHistory:"
LIT_REFLECTIONSMOKEFEEDBACK = "ReflectionSmokeFeedback:"
LIT_RENDERERSYSTEM_DISPATCHED_HARDWARE_TRA = "RendererSystem: dispatched hardware transparent shadow traversal"
LIT_RENDERERSYSTEM_DISPATCHED_SOFTWARE_SHA = "RendererSystem: dispatched software shadow traversal"
LIT_SHADOW_ROUTE = "shadow_route"
LIT_NWB_SOFT_SHADOW_TEST_ANGLE = "NWB_SOFT_SHADOW_TEST_ANGLE"
LIT_NWB_SOFT_SHADOW_TEST_SOURCE_RADIUS = "NWB_SOFT_SHADOW_TEST_SOURCE_RADIUS"
SHADOW_ROUTES = { "TransparentMultiSmokeProject: natural hardware shadow route selected on RayQuery-capable hardware": LIT_HARDWARE,
    "TransparentMultiSmokeProject: natural software-only shadow route selected because RayQuery-capable hardware is unavailable": LIT_SOFTWARE, }
DIMENSIONS = re.compile(r"deferred rendering targets ready \((\d+)x(\d+),")


class RendererWorkloadLogPolicy:
    @staticmethod
    def device_material_signature(text):
        text = text.replace("\r\n", "\n")
        devices = re.findall(r"^GraphicsRuntime: created device '(.+)'$", text, re.MULTILINE)
        if len(devices) != 1:
            raise SmokeFailure("one actual selected device identity is required")
        material_routes = {}
        for material, route in re.findall(r"^RendererSystem: material '([^']+)' selected (.+)$", text, re.MULTILINE):
            # A material can serve both indexed ordinary meshes and compute-emulated CSG meshes.
            material_routes.setdefault(material, set()).add(route)
        if not material_routes:
            raise SmokeFailure("material execution-route evidence is missing")
        return {"device": devices[0], "material_routes": { material: sorted(routes) for material, routes in sorted(material_routes.items())}}

    @staticmethod
    def reflection_arguments(workload, require_hardware=False):
        policy = workload.reflection_policy
        return SimpleNamespace(family=policy.family, width=workload.width, height=workload.height, mip_count=max(workload.width, workload.height).bit_length(), ray_budget=policy.ray_budget, optical_queries=policy.optical_queries, screen_steps=policy.screen_steps,
            sampling_seed=policy.sampling_seed, roughness=policy.roughness, history_samples=policy.history_samples, require_hardware=require_hardware or policy.variant.mode in (LIT_HARDWARE, LIT_HYBRID))

    @staticmethod
    def reflection_log(text, workload, require_hardware):
        policy = workload.reflection_policy
        args = RendererWorkloadLogPolicy.reflection_arguments(workload, require_hardware)
        text = text.replace("\r\n", "\n")
        reflection.validate_trial_log(text, args, policy.variant)
        validate_expected_log_text(text, [], [LIT_FRAMEBUFFERCAPTURE, LIT_RENDER_SUBMISSION_SUSPENDED])
        lines = text.splitlines()
        availability = [line for line in lines if line in ("ReflectionSmokeProject: hardware available", "ReflectionSmokeProject: hardware unavailable")]
        if len(availability) != 1:
            raise SmokeFailure("one unambiguous reflection hardware capability marker is required")
        # Check the startup query cap for both optical_clear and optical_inside.
        if policy.family == LIT_OPTICAL_INSIDE:
            prefix = "ReflectionSmokeProject: optical query limit "
            if [line[len(prefix):] for line in lines if line.startswith(prefix)] != [str(policy.optical_queries)]:
                raise SmokeFailure("runtime optical query limit differs from the frozen inside workload")
        return {**RendererWorkloadLogPolicy.device_material_signature(text), LIT_EXTENT: [workload.width, workload.height], "reflection_route": policy.variant.mode, "hardware_available": availability[0].endswith("hardware available"),
            LIT_TIMING_IN_FLIGHT_RANGES: reflection.TIMING_IN_FLIGHT_RANGES, "timing_depth_mip_count": args.mip_count, LIT_REFLECTION_POLICY: asdict(policy)}

    @staticmethod
    def transparent_multi_log(text, workload, require_hardware):
        lines = text.replace("\r\n", "\n").splitlines()
        text = "\n".join(lines)
        for prefix, expected in (("AvboitTimingProbe: in-flight ranges ", "32"), ("AvboitTimingProbe: render unfocused ", "1")):
            values = [line[len(prefix):] for line in lines if line.startswith(prefix)]
            if values != [expected]:
                raise SmokeFailure(f"timing policy differs from its single frozen value: {prefix}{values!r}")
        required = ("TransparentMultiSmokeProject: shared transparent material with three mutable instance overrides created", "TransparentMultiSmokeProject: shutdown",)
        for message in required:
            if lines.count(message) != 1:
                raise SmokeFailure(f"missing or repeated workload lifecycle message: {message}")
        forbidden = (*STRICT_LOG_FAILURE_MESSAGES, LIT_FRAMEBUFFERCAPTURE, LIT_REFLECTIONSMOKESTATISTICS, LIT_REFLECTIONSMOKEHISTORY, LIT_REFLECTIONSMOKEFEEDBACK, LIT_RENDER_SUBMISSION_SUSPENDED,
            "FrameLaggedAsyncLightingSmoke:", "CausticSphereSmokeProject:", "TransparentCsgSmokeProject:")
        validate_expected_log_text(text, [], forbidden)
        extents = {(int(width), int(height)) for width, height in DIMENSIONS.findall(text)}
        if extents != {(workload.width, workload.height)}:
            raise SmokeFailure(f"render extent changed or differs from the workload: {sorted(extents)}")
        routes = [SHADOW_ROUTES.get(line) for line in lines if line.startswith("TransparentMultiSmokeProject: natural ") and " shadow route " in line]
        if len(routes) != 1 or routes[0] not in (LIT_HARDWARE, LIT_SOFTWARE) or (require_hardware and routes[0] != LIT_HARDWARE):
            raise SmokeFailure(f"natural shadow route is missing, contradictory, or unsupported: {routes}")
        if routes == [LIT_HARDWARE]:
            validate_expected_log_text(text, [LIT_RENDERERSYSTEM_DISPATCHED_HARDWARE_TRA], [LIT_RENDERERSYSTEM_DISPATCHED_SOFTWARE_SHA])
        return {**RendererWorkloadLogPolicy.device_material_signature(text), LIT_SHADOW_ROUTE: routes[0], LIT_EXTENT: list(extents.pop()), LIT_TIMING_IN_FLIGHT_RANGES: 32}

    @staticmethod
    def caustic_log(text, workload, require_hardware):
        policy = CausticWorkload().validate_log(text, dict(workload.environment_overrides), capture=False)
        return {**RendererWorkloadLogPolicy.device_material_signature(text), **policy}

    @staticmethod
    def soft_shadow_log(text, workload, require_hardware):
        lines = text.replace("\r\n", "\n").splitlines()
        text = "\n".join(lines)
        for marker in ("ShadowTimingProbe: in-flight ranges 32", "ShadowTimingProbe: render unfocused 1", "ShadowTimingProbe: caustic emission 0", "ShadowTimingProbe: indirect response hemi-ambient", "SoftShadowTestSmokeProject: shutdown"):
            if lines.count(marker) != 1:
                raise SmokeFailure("missing or repeated shadow timing policy: " + marker)
        for prefix in ("ShadowTimingProbe: in-flight ranges ", "ShadowTimingProbe: render unfocused ", "ShadowTimingProbe: caustic emission ", "ShadowTimingProbe: indirect response "):
            if sum(line.startswith(prefix) for line in lines) != 1:
                raise SmokeFailure("contradictory shadow timing policy: " + prefix)
        startup = "SoftShadowTestSmokeProject: opaque + glass characters on a ground plane, 3 coloured lights, angularRadius="
        if sum(line.startswith(startup) for line in lines) != 1:
            raise SmokeFailure("one actual soft-shadow scene startup is required")
        route_prefix = "ShadowTimingProbe: natural shadow route "
        routes = [line[len(route_prefix):] for line in lines if line.startswith(route_prefix)]
        if len(routes) != 1 or routes[0] not in (LIT_HARDWARE, LIT_SOFTWARE) or (require_hardware and routes[0] != LIT_HARDWARE):
            raise SmokeFailure("natural shadow route is missing, contradictory, or unsupported")
        extents = re.findall(r"^ShadowTimingProbe: source extents angular=(\S+) radius=(\S+)$", text, re.MULTILINE)
        if len(extents) != 1:
            raise SmokeFailure("one actual shadow source-extent policy is required")
        expected = dict(workload.environment_overrides)
        try:
            actual_extents = tuple(float(value) for value in extents[0])
            wanted = (float(expected[LIT_NWB_SOFT_SHADOW_TEST_ANGLE]), float(expected[LIT_NWB_SOFT_SHADOW_TEST_SOURCE_RADIUS]))
        except ValueError as error:
            raise SmokeFailure("invalid shadow source extents") from error
        for actual, value in zip(actual_extents, wanted):
            if not math.isfinite(actual) or (actual != 0.0 if value == 0.0 else not math.isclose(actual, value, rel_tol=1e-6, abs_tol=1e-8)):
                raise SmokeFailure("actual shadow source extents differ from the frozen workload")
        forbidden = (*STRICT_LOG_FAILURE_MESSAGES, LIT_FRAMEBUFFERCAPTURE, LIT_RENDER_SUBMISSION_SUSPENDED, "VK_LAYER_KHRONOS_validation", "Vulkan: enabled validation layer", LIT_REFLECTIONSMOKESTATISTICS,
            LIT_REFLECTIONSMOKEHISTORY, LIT_REFLECTIONSMOKEFEEDBACK, "retaining all-lit visibility", "preserving opaque visibility")
        validate_expected_log_text(text, [], forbidden)
        dimensions = {(int(width), int(height)) for width, height in DIMENSIONS.findall(text)}
        if dimensions != {(workload.width, workload.height)}:
            raise SmokeFailure("shadow render extent differs from the frozen workload")
        if routes == [LIT_HARDWARE]:
            validate_expected_log_text(text, [LIT_RENDERERSYSTEM_DISPATCHED_HARDWARE_TRA], [LIT_RENDERERSYSTEM_DISPATCHED_SOFTWARE_SHA])
        return {**RendererWorkloadLogPolicy.device_material_signature(text), LIT_SHADOW_ROUTE: routes[0], LIT_EXTENT: [workload.width, workload.height], "source_extents": actual_extents, "caustic_emission": False, "indirect_response": "hemi-ambient", LIT_TIMING_IN_FLIGHT_RANGES: 32}
