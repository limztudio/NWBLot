"""Isolate one current UI fixture from inherited selectors and capture controls."""
from __future__ import annotations

import platform

LIT_DEFAULT = "default"
LIT_ALTERNATE = "alternate"
SWITCH_ON = "1"
SWITCH_OFF = "0"


FIXTURE_FLAGS = ("INTERACTIVE", "WINDOW", "EDIT", "POPUP", "POPUP_TOOLS", "NESTED_POPUP", "LIST", "COMBO", "SEARCH_COMBO", "NUMERIC_EDIT", "TEXT_AREA", "RADIO_GROUP", "SLIDER", "PROGRESS", "IMAGE", "TEXTURE_IMAGE",)
SKIN_FIXTURES = tuple(fixture for fixture in FIXTURE_FLAGS if fixture not in ("INTERACTIVE", "EDIT"))
AUXILIARY_SWITCHES = ("NWB_UI_LAYER_INPUT_BINDINGS_SMOKE", "NWB_UI_LAYER_RESIZE_CAPTURE", "NWB_UI_IR_REPLAY")
CAPTURE_ENVIRONMENT = ("NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH", "NWB_SMOKE_FRAMEBUFFER_CAPTURE_FRAME_COUNT", "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME", "NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS", "NWB_GPU_TIMING_FILE",)


def build_fixture_environment(inherited, fixture=None, *, skin=LIT_DEFAULT, force_x11=False):
    if fixture is not None and fixture not in FIXTURE_FLAGS:
        raise ValueError(f"unknown UI fixture: {fixture}")
    if skin not in (LIT_DEFAULT, LIT_ALTERNATE):
        raise ValueError(f"unknown UI fixture skin: {skin}")
    if skin == LIT_ALTERNATE and fixture not in SKIN_FIXTURES:
        raise ValueError("the selected UI fixture has no alternate skin")
    environment = dict(inherited)
    for name in FIXTURE_FLAGS:
        environment[f"NWB_UI_LAYER_{name}"] = SWITCH_ON if name == fixture else SWITCH_OFF
    for name in SKIN_FIXTURES:
        environment[f"NWB_UI_LAYER_{name}_SKIN"] = SWITCH_ON if name == fixture and skin == LIT_ALTERNATE else SWITCH_OFF
    for name in AUXILIARY_SWITCHES:
        environment[name] = SWITCH_OFF
    for name in CAPTURE_ENVIRONMENT:
        environment.pop(name, None)
    if force_x11 and platform.system() == "Linux":
        environment["NWB_LINUX_BACKEND"] = "x11"
    return environment
