"""Fixed caustic workload settings and runtime evidence validation."""

import math
import re

from caustic_footprint import LIT_POPULATED, LIT_SPARSE, LIT_CAMERA_HEIGHT
from window_capture_smoke import STRICT_LOG_FAILURE_MESSAGES, SmokeFailure
from window_capture_validation import validate_expected_log_text

LIT_NWB_CAUSTIC_SMOKE_CAMERA_PRESET = "NWB_CAUSTIC_SMOKE_CAMERA_PRESET"
LIT_NWB_CAUSTIC_SMOKE_ENABLED = "NWB_CAUSTIC_SMOKE_ENABLED"
LIT_DISABLED = "disabled"
LIT_CAUSTICSPHERESMOKEPROJECT_CAUSTICS = "CausticSphereSmokeProject: caustics "
LIT_TRANSPARENTMULTISMOKEPROJECT_SHUTDOWN = "TransparentMultiSmokeProject: shutdown"
LIT_HARDWARE = "hardware"
PHOTONS = "render.caustic_photons"
RESOLVE = "render.caustic_resolve"
PRESETS = {LIT_POPULATED: 2.2, LIT_SPARSE: 4.4}
FRAME_COUNT = 360
WIDTH, HEIGHT = 1280, 900
PRODUCER = re.compile(r"^RendererSystem: dispatched hardware caustic producer \((\d+) photons/frame, " r"(\d+) temporal phases, (\d+) full-grid budget, (\d+) caustic lights, (\d+) refractive instances\)$", re.MULTILINE)


class CausticWorkload:
    def __init__(self, width=WIDTH, height=HEIGHT):
        self.width = width
        self.height = height

    @staticmethod
    def environment(preset, enabled=True):
        if preset not in PRESETS:
            raise SmokeFailure("unknown caustic camera preset")
        return {"NWB_CAUSTIC_SMOKE_TIMING": "1", "NWB_AVBOIT_SMOKE_TIMING": "1", LIT_NWB_CAUSTIC_SMOKE_CAMERA_PRESET: preset, LIT_NWB_CAUSTIC_SMOKE_ENABLED: "1" if enabled else "0", "NWB_REFRACTION_SMOKE_ENABLED": "0", "NWB_REFRACTION_SMOKE_HARDWARE": "1",
            "NWB_REFLECTION_SMOKE_MODE": LIT_DISABLED, "NWB_CAUSTIC_SMOKE_REFLECTION_COMPARISON": "0", "NWB_TRANSPARENT_MULTI_SPIN_ANGLE": "0", "NWB_TRANSPARENT_MULTI_SPIN_SPEED": "0",
            "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME": "0", "NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS": "0.016666667"}

    @staticmethod
    def single_match(pattern, text, label):
        matches = re.findall(pattern, text, re.MULTILINE)
        if len(matches) != 1:
            raise SmokeFailure("one unambiguous " + label + " is required")
        return matches[0]

    def validate_log(self, text, settings, capture=False):
        text = text.replace("\r\n", "\n")
        lines = text.splitlines()
        preset = settings[LIT_NWB_CAUSTIC_SMOKE_CAMERA_PRESET]
        enabled = settings[LIT_NWB_CAUSTIC_SMOKE_ENABLED] == "1"
        if settings != CausticWorkload.environment(preset, enabled):
            raise SmokeFailure("caustic settings differ from the complete fixed policy")
        required = ("AvboitTimingProbe: in-flight ranges 32", "AvboitTimingProbe: render unfocused 1", "AvboitTimingProbe: caustic in-flight ranges 32", "CausticSphereSmokeProject: reflection mode 0", "CausticSphereSmokeProject: camera refraction disabled",
            LIT_CAUSTICSPHERESMOKEPROJECT_CAUSTICS + ("enabled" if enabled else LIT_DISABLED), "CausticTimingProbe: scene single-static-sphere-ground-v1", LIT_TRANSPARENTMULTISMOKEPROJECT_SHUTDOWN)
        for marker in required:
            if lines.count(marker) != 1:
                raise SmokeFailure("missing or repeated caustic lifecycle/policy: " + marker)
        route_messages = { "TransparentMultiSmokeProject: natural hardware shadow route selected on RayQuery-capable hardware": LIT_HARDWARE, }
        routes = [route_messages.get(line) for line in lines if line.startswith("TransparentMultiSmokeProject: natural ") and " shadow route " in line]
        if len(routes) != 1 or routes[0] != LIT_HARDWARE:
            raise SmokeFailure("one supported caustic shadow route is required")
        validate_expected_log_text(text, ["RendererSystem: dispatched hardware transparent shadow traversal"], ["RendererSystem: dispatched software shadow traversal"])
        # Reject contradictory values as well as requiring the selected value once.
        for prefix in ("AvboitTimingProbe: in-flight ranges ", "AvboitTimingProbe: render unfocused ", "AvboitTimingProbe: caustic in-flight ranges ", "CausticSphereSmokeProject: reflection mode ",
            "CausticSphereSmokeProject: camera refraction ", LIT_CAUSTICSPHERESMOKEPROJECT_CAUSTICS, "CausticTimingProbe: scene "):
            if sum(line.startswith(prefix) for line in lines) != 1:
                raise SmokeFailure("contradictory caustic policy: " + prefix)
        disabled = CausticWorkload.single_match(r"^CausticTimingProbe: reflection diagnostics (\S+) temporal (\S+) spatial (\S+) feedback (\S+)$", text, "reflection auxiliary settings")
        if any(value not in ("0", "false") for value in disabled):
            raise SmokeFailure("reflection diagnostics/history/filter/feedback must be disabled")
        camera = CausticWorkload.single_match(r"^CausticTimingProbe: camera (\S+) distance (\S+) height (\S+)$", text, "camera setting")
        pose = CausticWorkload.single_match(r"^CausticTimingProbe: fixed delta (\S+) yaw (\S+) sphere scale (\S+)$", text, "fixed scene pose")
        expected = (PRESETS[preset], .85, .016666667, 0.0, .7)
        actual = tuple(float(value) for value in (*camera[1:], *pose))
        if camera[0] != preset or any(not math.isfinite(a) or not math.isclose(a, b, rel_tol=1e-6, abs_tol=1e-8) for a, b in zip(actual, expected)):
            raise SmokeFailure("camera/pose/geometry differs from its fixed workload")
        light = tuple(float(value) for value in CausticWorkload.single_match(r"^CausticTimingProbe: directional pitch (\S+) yaw (\S+) intensity (\S+)$", text, "directional light"))
        fov = float(CausticWorkload.single_match(r"^CausticTimingProbe: vertical FOV radians (\S+)$", text, "camera vertical FOV"))
        if not math.isclose(fov, math.pi / 3, rel_tol=1e-6) \
            or any(not math.isclose(a, b, rel_tol=1e-6) for a, b in zip(light, (.9, .65, 2.0))):
            raise SmokeFailure("actual camera projection or light changed")
        phase = CausticWorkload.single_match(r"^CausticTimingProbe: photon phases (\d+)$", text, "photon temporal schedule")
        if tuple(map(int, phase)) != (2,):
            raise SmokeFailure("photon temporal schedule changed")
        producers = [tuple(map(int, row)) for row in PRODUCER.findall(text)]
        if enabled:
            if len(producers) != 1 or producers[0][2:] != (262144, 1, 1) or producers[0][1] != 2 \
                or producers[0][0] * producers[0][1] != 262144:
                raise SmokeFailure("actual hardware photon budget, phase count, light or refractor count changed")
        elif producers:
            raise SmokeFailure("disabled caustics still dispatched photons")
        dimensions = {(int(w), int(h)) for w, h in re.findall(r"deferred rendering targets ready \((\d+)x(\d+),", text)}
        if dimensions != {(self.width, self.height)}:
            raise SmokeFailure("caustic render extent changed")
        forbidden = (*STRICT_LOG_FAILURE_MESSAGES, "software caustic producer (", "Reflection resolve: hardware", "Reflection resolve: screen-space", "AVBOIT refraction resolve:", "ReflectionSmokeStatistics:",
            "ReflectionSmokeHistory:", "ReflectionSmokeFeedback:", "render submission suspended", "FrameLaggedAsyncLightingSmoke:", "natural software-only shadow route", "retaining all-lit visibility")
        if not capture:
            forbidden += ("FramebufferCapture:", "VK_LAYER_KHRONOS_validation", "Vulkan: enabled validation layer")
        validate_expected_log_text(text, [], forbidden)
        result = {"camera_preset": preset, "camera_distance": actual[0], LIT_CAMERA_HEIGHT: actual[1], "fixed_delta_seconds": actual[2], "yaw": actual[3], "sphere_scale": actual[4], "extent": [self.width, self.height],
            "shadow_route": routes[0], "reflection_mode": LIT_DISABLED, "camera_refraction": False, "caustics": enabled, "photon_schedule": list(map(int, phase)), "initial_producer": list(producers[0]) if producers else None,
            "timing_in_flight_ranges": 32, "directional_light": list(light), "vertical_fov_radians": fov}
        if capture:
            if lines.count("FramebufferCapture: capture ready") != 1:
                raise SmokeFailure("one actual completed framebuffer readback is required")
            source = int(CausticWorkload.single_match(r"^FramebufferCapture: graphics source frame (\d+)$", text, "captured graphics source frame"))
            if source < FRAME_COUNT - 1:
                raise SmokeFailure("capture predates the requested stationary presentation window")
            result["graphics_source_frame"] = source
        return result

    @staticmethod
    def validate_warmup(scopes):
        # Require twelve actual completed GPU observations, covering six checkerboard parity cycles.
        for name in ("render.frame", PHOTONS):
            value = scopes.get(name)
            if value is None or value["gpu_samples"] < 12 or value["total_ms"] <= 0:
                raise SmokeFailure("caustic warm-up requires at least12 actual completed frame/photon ranges before measurement")
