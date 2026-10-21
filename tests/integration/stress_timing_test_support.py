"""Shared argument, log, and record builders for the stress presentation timing test modules."""


from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))
import stress_timing_smoke as smoke


LIT_SURFELGIQUALITYSMOKE_REQUESTED_RESOLVE = "SurfelGiQualitySmoke: requested resolve_factor=4"
LIT_RENDERERSYSTEM_DISPATCHED_SURFEL_GI_RE = "RendererSystem: dispatched surfel GI resolve (factor=4, source=1280x900, resolve=320x225)"
LIT_N_96 = "96"
LIT_FPS_16_PRESENTATIONS_480_SECONDS_30_FI = "fps=16 presentations=480 seconds=30 first=80 last=560"
LIT_RENDERER_EXE = "renderer.exe"
LIT_EXECUTABLE = "--executable"
LIT_WORKING_DIRECTORY = "--working-directory"
LIT_NO_LOGSERVER = "--no-logserver"
LIT_UTF_8 = "utf-8"
LIT_FIXTURE = "fixture"


def stress_arguments(runtime, extra=()):
    executable = runtime / LIT_RENDERER_EXE
    executable.write_bytes(LIT_FIXTURE.encode(LIT_UTF_8))
    return smoke.parse_args([LIT_EXECUTABLE, str(executable), LIT_WORKING_DIRECTORY, str(runtime), LIT_NO_LOGSERVER, *extra])


def shadow_record(backend=0, directional_resolution=512, point_resolution=256, budget_bytes=268435456, coverage=0, blocker_search=0, capture_cadence=0):
    return (smoke.SOFTWARE_SHADOW_SETTINGS + f"backend={backend} directional_resolution={directional_resolution} "
        f"point_resolution={point_resolution} budget_bytes={budget_bytes} coverage={coverage} blocker_search={blocker_search} capture_cadence={capture_cadence}")


def shadow_quality_record(sampling=0, factor=4, width=1280, height=900):
    return (smoke.SHADOW_QUALITY_SETTINGS + f"{sampling} receiver_factor={factor}\n" + smoke.SHADOW_RECEIVER_GRID + f"factor={factor} full={width}x{height} receiver={(width+factor-1)//factor}x{(height+factor-1)//factor}")


def workload_record(characters_per_class=10):
    if characters_per_class == 5:
        return (smoke.WORKLOAD + "characters_per_class=5 total=10 transparent=5 opaque=5 layout=zigzag_v1 " "rows=2 columns=5 row_spacing_x=1.44 row_stagger_x=0.36 front_z=-0.55 back_z=0.55 body_scale=1 "
            "camera_x=0 camera_y=1.8 camera_z=-4.8 camera_pitch=0.2 vertical_fov=1.0471976 " "near_plane=0.001 far_plane=10000 aspect=0")
    return (smoke.WORKLOAD + "characters_per_class=10 total=20 transparent=10 opaque=10 layout=two_rows_v1 " "rows=2 columns=10 row_spacing_x=0.72 row_stagger_x=0.18 front_z=-0.55 back_z=0.55 body_scale=1 "
        "camera_x=0 camera_y=2.7 camera_z=-7.2 camera_pitch=0.25 vertical_fov=1.0471976 " "near_plane=0.001 far_plane=10000 aspect=0")


def valid_log(characters_per_class=10):
    lines = [marker for marker in smoke.REQUIRED if marker != smoke.SHUTDOWN]
    lines.append(smoke.SPAWN + f"{characters_per_class * 2} spinning characters ({characters_per_class} transparent + " f"{characters_per_class} opaque) over ground, directional + point light")
    lines.append(workload_record(characters_per_class))
    lines.append("RendererSystem: deferred rendering targets ready (1280x900, format test)")
    # Synthetic fixed simulation delta is 1/60, while genuine wall/count evidence yields 16 FPS.
    lines.append("Fixture: fixed simulation delta 0.016666667")
    lines.append("CausticQualitySmoke: requested photon_grid_divisor=1")
    lines.append(LIT_SURFELGIQUALITYSMOKE_REQUESTED_RESOLVE)
    lines.append(LIT_RENDERERSYSTEM_DISPATCHED_SURFEL_GI_RE)
    lines.append("RendererSystem: dispatched hardware caustic producer (131072 photons/frame, 2 temporal phases, " "262144 full-grid budget, 2 caustic lights, 10 refractive instances)")
    lines.append(shadow_quality_record())
    lines.append(smoke.REFLECTION_QUALITY_SETTINGS + LIT_N_96)
    for index in range(60):
        lines.append(smoke.INTERVAL + f"avg=16 presentations=8 seconds=0.5 first={80+8*index} last={88+8*index}")
    lines.append(smoke.LIT_STRESSTESTSMOKEPROJECT_PRESENTATION_PA + "samples=480 p50ms=62.5 p95ms=70.0 maxms=120.0 stalls50ms=3")
    lines.append(smoke.DONE + LIT_FPS_16_PRESENTATIONS_480_SECONDS_30_FI)
    lines.append(smoke.SHUTDOWN)
    return "\n\n".join(lines) + "\n"


def reflection_record(**changes):
    row = dict(sequence=1, generation=1, frame=10, graphics_frame=10, hardware_ready=1, transport_enabled=1, candidates=10, hardware_rays=10, exterior_eligible_rays=0, query_budget_units=0, physical_queries=0, bootstrap_events=0,
        transparent_paths=0, unsupported_paths=10, screen_attempts=0, screen_hits=0, screen_returns=0, screen_iterations=0, screen_limit_misses=0)
    row.update(changes)
    return ("StressReflectionStatistics: sequence={sequence} generation={generation} frame={frame} " "graphics_frame={graphics_frame} hardware_ready={hardware_ready} transport_enabled={transport_enabled} "
        "candidates={candidates} hardware_rays={hardware_rays} exterior_eligible_rays={exterior_eligible_rays} " "query_budget_units={query_budget_units} physical_queries={physical_queries} bootstrap_events={bootstrap_events} "
        "transparent_paths={transparent_paths} unsupported_paths={unsupported_paths} " "screen_attempts={screen_attempts} screen_hits={screen_hits} screen_returns={screen_returns} "
        "screen_iterations={screen_iterations} screen_limit_misses={screen_limit_misses}").format(**row)


def reflection_log(*records):
    return valid_log().replace(smoke.START, smoke.REFLECTION_ENABLED + "\n" + smoke.START).replace(smoke.SHUTDOWN, "\n".join(records) + "\n" + smoke.SHUTDOWN)
