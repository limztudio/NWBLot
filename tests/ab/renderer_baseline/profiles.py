"""Stable, test-owned scene definitions for renderer baseline captures."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Mapping


@dataclass(frozen=True)
class BaselineProfile:
    """One deterministic smoke scene suitable for an immutable image baseline."""

    target: str
    runtime_directory: Path
    window_title: str
    settle_seconds: float
    frozen_environment: Mapping[str, str]
    description: str
    capture_freeze_frame: int = 0
    capture_ready_log: str = ""


PROFILES: Mapping[str, BaselineProfile] = {
    "opaque-texture": BaselineProfile(
        target="nwb_texture_smoke",
        runtime_directory=Path("Testing") / "texture_smoke_runtime",
        window_title="NWB Texture Smoke",
        settle_seconds=4.0,
        frozen_environment={},
        description="Opaque textured material and bindless sampled-image baseline.",
    ),
    "transparent-avboit": BaselineProfile(
        target="nwb_transparent_multi_smoke",
        runtime_directory=Path("Testing") / "smoke_runtime",
        window_title="NWB Transparent Multi Smoke",
        settle_seconds=0.75,
        frozen_environment={"NWB_TRANSPARENT_MULTI_SPIN_ANGLE": "0.6"},
        description="Prepared transparent AVBOIT baseline with immutable material streams.",
        capture_freeze_frame=96,
        capture_ready_log="TransparentMultiSmokeProject: renderer baseline capture ready after",
    ),
    "static-csg": BaselineProfile(
        target="nwb_csg_visible_smoke",
        runtime_directory=Path("Testing") / "csg_visible_smoke_runtime",
        window_title="NWB CSG Visible Smoke",
        settle_seconds=4.0,
        frozen_environment={},
        description="Static opaque CSG interval-production baseline.",
    ),
    "skinned-csg": BaselineProfile(
        target="nwb_csg_skinned_visible_smoke",
        runtime_directory=Path("Testing") / "csg_skinned_visible_smoke_runtime",
        window_title="NWB Skinned CSG Smoke",
        settle_seconds=5.0,
        frozen_environment={},
        description="Runtime-skinning and CSG interval-production baseline.",
    ),
    "soft-shadows": BaselineProfile(
        target="nwb_soft_shadow_test_smoke",
        runtime_directory=Path("Testing") / "skinning_culling_benchmark_runtime",
        window_title="NWB Soft Shadow Test",
        settle_seconds=6.0,
        frozen_environment={"NWB_SOFT_SHADOW_TEST_SPIN_ANGLE": "0.6"},
        description="Hardware/hybrid soft-shadow production-path baseline.",
    ),
    "caustics": BaselineProfile(
        target="nwb_caustic_sphere_smoke",
        runtime_directory=Path("Testing") / "smoke_runtime",
        window_title="NWB Caustic Sphere Smoke",
        settle_seconds=6.0,
        frozen_environment={"NWB_TRANSPARENT_MULTI_SPIN_ANGLE": "0.6"},
        description="Caustic accumulation baseline after the temporal warm-up.",
    ),
    "surfel-gi": BaselineProfile(
        target="nwb_gi_test_smoke",
        runtime_directory=Path("Testing") / "skinning_culling_benchmark_runtime",
        window_title="NWB GI Test",
        settle_seconds=6.0,
        frozen_environment={},
        description="Surfel-GI trace and resolve baseline.",
    ),
    "stress": BaselineProfile(
        target="nwb_stress_test_smoke",
        runtime_directory=Path("Testing") / "skinning_culling_benchmark_runtime",
        window_title="NWB Stress Test Smoke",
        settle_seconds=6.0,
        frozen_environment={"NWB_STRESS_TEST_SPIN_ANGLE": "0.6"},
        description="Skinned opaque/transparent stress-scene baseline.",
    ),
}


def profile_names() -> tuple[str, ...]:
    return tuple(sorted(PROFILES))


def get_profile(name: str) -> BaselineProfile:
    try:
        return PROFILES[name]
    except KeyError as error:
        available = ", ".join(profile_names())
        raise ValueError(f"unknown renderer baseline profile '{name}'; choose one of: {available}") from error
