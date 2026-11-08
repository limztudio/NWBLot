"""Shared capture and analysis contract for temporal CSG evidence."""

LIT_CAPTURE_TIMEOUT_MARGIN_SECONDS = 45.0
LIT_MINIMUM_FRAME_INTERVAL = 1
LIT_MAXIMUM_FRAME_INTERVAL = 64

LIT_PARTIAL_SUFFIX = ".partial"
LIT_TEMPORAL_WARMUP_COMPLETE = "CsgVisibleSmokeProject: temporal warmup complete elapsed_seconds="
LIT_TEMPORAL_SERIES_COMPLETE = "CsgVisibleSmokeProject: temporal series complete samples=32"

LIT_BACKGROUND = "background"
LIT_SPHERE = "sphere"
LIT_PIXEL = "pixel"
LIT_NORMAL = "normal"
LIT_POSITION = "position"
LIT_CAPSULE = "capsule"
LIT_EVENT_DATA = "event_data"
LIT_SPAN_PRESENT = "span_present"
LIT_EVENT_ORDER = "event_order"
LIT_FIRST_FRONT = "first_front"
LIT_SECOND_FRONT = "second_front"
LIT_SPAN_STATE = "span_state"
LIT_MANIFEST_JSON = "manifest.json"
LIT_RUNTIME_LOG = "runtime.log"
LIT_NWB_CSG_GI_TEMPORAL = "NWB_CSG_GI_TEMPORAL"
LIT_NWB_GPU_TIMING_FILE = "NWB_GPU_TIMING_FILE"
LIT_ANIMATED = "animated"
LIT_EXECUTABLE = "--executable"
LIT_WORKING_DIRECTORY = "--working-directory"
LIT_TIMEOUT = "--timeout"
LIT_EXPECT_LOG_MESSAGE = "--expect-log-message"
LIT_CSGVISIBLESMOKEPROJECT_TEMPORAL_WARMUP = "CsgVisibleSmokeProject: temporal warmup started graphics frame"
LIT_CSGVISIBLESMOKEPROJECT_SHUTDOWN = "CsgVisibleSmokeProject: shutdown"
LIT_RENDERERSYSTEM_DISPATCHED_SURFEL_GI_RE = "RendererSystem: dispatched surfel GI resolve"
LIT_GPU_VALIDATION = "--gpu-validation"
LIT_SOFTWARE = "software"
LIT_LOADER_HARDWARE_RAY_TRACING_DISABLED_B = "Loader: hardware ray tracing disabled before device creation"
LIT_REJECT_LOG_MESSAGE = "--reject-log-message"
LIT_RENDERERSYSTEM_FRAME_LAGGED_ASYNC_LIGH = "RendererSystem: frame-lagged async lighting active history accepted"
LIT_LOGSERVER_EXECUTABLE = "--logserver-executable"
LIT_CHANNEL_JUMP = "channel_jump"
LIT_MEAN_RGB = "mean_rgb"
LIT_HORIZONTAL_JUMP_FRACTION = "horizontal_jump_fraction"
LIT_VERTICAL_JUMP_FRACTION = "vertical_jump_fraction"
LIT_UTF_8 = "utf-8"
LIT_GRAPHICS_FRAME = "graphics_frame"
LIT_REGIONS = "regions"
LIT_DISPLAY_RGB = "display_rgb"
LIT_DECODED = "decoded"
LIT_COMPONENT_TOLERANCE = "component_tolerance"
LIT_DIAGNOSTIC_POINTS = "diagnostic_points"
LIT_EVENT_STATE = "event_state"
LIT_FAILED_SPAN_FLAGS = "failed_span_flags"
LIT_STATE = "state"
LIT_P95_TEMPORAL_RANGE = "p95_temporal_range"
LIT_SAMPLES = "samples"
LIT_FRAMES = "frames"
LIT_VIEW = "view"
LIT_MOTION = "motion"
LIT_STATIC = "static"
LIT_OVERLAPPING = "overlapping"
LIT_ROUTE = "route"
LIT_NATURAL = "natural"
LIT_LAGGED_LIGHTING = "lagged_lighting"
LIT_BOX = "box"
LIT_DIRECT = "direct"
LIT_CAP_STATE = "cap_state"
LIT_FULL = "full"
LIT_STORE_TRUE = "store_true"


SAMPLE_COUNT = 32
MINIMUM_FRAME = 360
WARMUP_SECONDS = 30
FRAME_INTERVAL = 8
EXPECTED_EXTENT = (1280, 900)
REGIONS = {"plane": (454, 324, 32, 32), "box": (826, 324, 32, 32), "sphere": (454, 576, 32, 32), "capsule": (826, 576, 32, 32), "box_upper_band": (836, 290, 40, 8), "sphere_upper_band": (414, 552, 40, 16), "capsule_stripes": (805, 576, 32, 40),
    LIT_BACKGROUND: (640, 450, 32, 32)}
STATIC_EXTERIOR_REGIONS = {"sphere_exterior": (502, 578, 6, 24), "capsule_exterior": (882, 576, 6, 24), "box_exterior": (896, 324, 3, 20)}
ANIMATED_INTERIOR_REGIONS = {"box_inner_wall": (832, 316, 24, 8)}
QUALITY_LIMITS = {"channel_jump": 20, "max_frame_jump_fraction": .10, "static_p95_temporal_range": 6, "colored_core_minimum": 32, "tint_margin": 4, "direct_core_minimum": 16, "direct_background_separation": 8, "background_tolerance": 2}
# Analytical ray-cutter hits for the 1280x900 static camera across four yaw phases.
GEOMETRY_PROBES = { "plane": {"pixel": (430, 320), "normal": (0., 0., -1.), "position": (-1.747126065, 1.829965754, 0.)}, "box": {"pixel": (835, 315), "normal": (-.179029573, 0., -.983843693), "position": (1.721953360, 1.934668680, .365114160)},
    LIT_SPHERE: {LIT_PIXEL: (415, 570), LIT_NORMAL: (.318619304, -.078551107, -.944622392), LIT_POSITION: (-1.881587477, -.259938935, .032526944)},
    LIT_CAPSULE: {LIT_PIXEL: (805, 570), LIT_NORMAL: (-.177317767, 0., -.984153651), LIT_POSITION: (1.361729587, -.241470787, -.086929462)}}
GEOMETRY_LIMITS = {"normal_l2_error": .04, "position_component_max": .06, "position_arithmetic_allowance": .005}
CAP_STATE_REQUIRED_PROBES = {name: probe["pixel"] for name, probe in GEOMETRY_PROBES.items()}
# This interior rim block previously lost its receiver span when raster events arrived BACK first.
CAP_STATE_REQUIRED_PROBES.update({f"capsule_{x}_{y}": (x, y) for y in range(604, 608) for x in range(784, 787)})
CAP_STATE_PROBES = dict(CAP_STATE_REQUIRED_PROBES)
CAP_STATE_PROBES.update({f"capsule_{x}_{y}": (x, y) for y in range(604, 608) for x in (783, 787)})
STATE_CHANNELS = {"cap_state": ("raw_removed_count", "nearest_cap_visible", "owns_gbuffer_depth"), "interval_state": ("receiver_span_count", "peel_id_present", "peel_cap_coverage"), "event_state": ("raw_count", "front_count", "back_count"),
    LIT_EVENT_DATA: (LIT_SPAN_PRESENT, "matching_event_pair", "ordered_event_depths"), LIT_EVENT_ORDER: (LIT_SPAN_PRESENT, LIT_FIRST_FRONT, LIT_SECOND_FRONT), LIT_SPAN_STATE: ("span_count", "span_flags", "event_count")}
EVENT_DATA_FLAGS_MARKER = "CsgVisibleSmokeProject: event-data red stores missing-span flags divided by 33"
EVENT_ORDER_MARKER = "CsgVisibleSmokeProject: event-order RGB stores span/flags and validated raw FRONT=1 BACK=0.5"
SPAN_STATE_MARKER = "CsgVisibleSmokeProject: span-state RGB stores span count, flags, raw event count divided by 33"
OVERLAPPING_MARKER = "CsgVisibleSmokeProject: overlapping plane receiver created z_offset=0.125"
SPAN_EXPECTATIONS = {name: {"span_count": 2 if name == "plane" else 1, "span_flags": 0, "event_count": 4 if name == "plane" else 2} for name in GEOMETRY_PROBES}
