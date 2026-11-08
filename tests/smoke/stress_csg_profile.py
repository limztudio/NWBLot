"""Validation of the requested CSG stress workload and dispatch route."""

import math
import re

from window_capture_outcome import SmokeFailure


LIT_TOKEN_FIELD_PATTERN = r"=(\S+)"
LIT_ENABLED_FLAG = "1"
LIT_STRESSTESTSMOKEPROJECT_DEVICE_CAPABILI = "StressTestSmokeProject: device capability "
LIT_TRANSPARENT = "transparent"
LIT_OPAQUE = "opaque"
LIT_FRONT_Z = "front_z"
LIT_BACK_Z = "back_z"
LIT_OBSERVED = "observed"
LIT_SIGNATURE = "signature"
LIT_REQUESTED = "requested"
LIT_VERIFIED = "verified"
LIT_NONE = "none"
LIT_ENABLED = "enabled"
LIT_WAIST_BANDS = "waist_bands"
LIT_RECEIVERS = "receivers"
LIT_MOTION = "motion"
CSG_PROFILE = "StressTestSmokeProject: CSG "
CSG_DISPATCH = "RendererSystem: dispatched CSG light-space shadows "
CSG_DEVICE = LIT_STRESSTESTSMOKEPROJECT_DEVICE_CAPABILI


class CsgProfileEvidence:
    def __init__(self, args, interval_marker, completion_marker):
        self._args = args
        self._interval_marker = interval_marker
        self._completion_marker = completion_marker

    def verify(self, text):
        args = self._args
        requested = args.csg_profile
        lines = [line.strip() for line in text.splitlines()]
        records = [line for line in lines if line.startswith(CSG_PROFILE)]
        dispatches = [line for line in lines if line.startswith(CSG_DISPATCH)]
        if requested == LIT_NONE:
            if records or dispatches:
                raise SmokeFailure("unrequested CSG profile or dispatch changes the stress workload")
            return {LIT_REQUESTED: LIT_NONE, LIT_ENABLED: False, LIT_VERIFIED: True}
        if requested != LIT_WAIST_BANDS or len(records) != 1 or len(dispatches) != 1:
            raise SmokeFailure("waist_bands requires exactly one profile and actual CSG shadow dispatch record")
        fields = ("profile", LIT_RECEIVERS, LIT_TRANSPARENT, LIT_OPAQUE, "cutters", "half_x", "half_y", "half_z", "center_y", "amplitude_y", LIT_FRONT_Z, LIT_BACK_Z, LIT_MOTION)
        match = re.fullmatch(re.escape(CSG_PROFILE) + " ".join(re.escape(field) + LIT_TOKEN_FIELD_PATTERN for field in fields), records[0])
        if not match:
            raise SmokeFailure("malformed CSG stress profile signature")
        observed = dict(zip(fields, match.groups()))
        expected = dict(profile=LIT_WAIST_BANDS, receivers=20, transparent=10, opaque=10, cutters=2, half_x=4.5, half_y=.08, half_z=.65, center_y=.9, amplitude_y=.1, front_z=-.55, back_z=.55, motion="crowd_yaw")
        for field, value in expected.items():
            if isinstance(value, str):
                matches = observed[field] == value
            else:
                try:
                    observed[field] = int(observed[field]) if isinstance(value, int) else float(observed[field])
                    matches = math.isfinite(observed[field]) and math.isclose(observed[field], value, rel_tol=1e-6, abs_tol=1e-6)
                except (ValueError, OverflowError):
                    matches = False
            if not matches:
                raise SmokeFailure(f"CSG stress profile disagrees with requested {field}")
        devices = [line for line in lines if line.startswith(CSG_DEVICE)]
        capability = re.match(re.escape(CSG_DEVICE) + r"meshlets=[01] rayquery=([01]) raypipeline=[01] accelstruct=([01]) ", devices[0]) if len(devices) == 1 else None
        dispatch = re.fullmatch(re.escape(CSG_DISPATCH) + r"\(hardware_compose=([01]), ([0-9]+) instances\)", dispatches[0])
        if not capability or not dispatch:
            raise SmokeFailure("CSG stress requires enabled-device capability and actual map route evidence")
        hardware = capability.groups() == (LIT_ENABLED_FLAG, LIT_ENABLED_FLAG)
        if int(dispatch[1]) != int(hardware) or int(dispatch[2]) < observed[LIT_RECEIVERS]:
            raise SmokeFailure("CSG stress map route or instance population disagrees with the active workload")
        if "--disable-hardware-ray-tracing" in args.application_arg:
            disabled = "Loader: hardware ray tracing disabled before device creation"
            if hardware or disabled not in lines:
                raise SmokeFailure("requested software CSG stress lacks loader policy or software route evidence")
        first_interval = next((index for index, line in enumerate(lines) if line.startswith(self._interval_marker)), -1)
        completion = next((index for index, line in enumerate(lines) if line.startswith(self._completion_marker)), -1)
        if not lines.index(records[0]) < lines.index(dispatches[0]) < first_interval < completion:
            raise SmokeFailure("CSG setup and dispatch must precede the first measurement interval")
        return {LIT_REQUESTED: requested, LIT_ENABLED: True, LIT_OBSERVED: observed, "hardware_compose": hardware, "map_instances": int(dispatch[2]), LIT_SIGNATURE: records[0], LIT_VERIFIED: True}
