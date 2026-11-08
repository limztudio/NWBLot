#!/usr/bin/env python3
"""Passive Windows PDH GPU-engine observations for smoke timing campaigns.

These counters expose engine load, not GPU frequency. Publication-window
alignment is approximate; raw timestamps and invalid samples remain available.
"""

import ctypes
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import threading
import time

from gpu_load_analysis import GpuLoadAnalysis, VALID_STATUSES
from window_capture_smoke import SmokeFailure, write_status

COUNTER_PATH = r"\GPU Engine(*)\Utilization Percentage"
MORE_DATA = 0x800007D2
NO_DATA = 0x800007D5
INVALID_DATA = 0xC0000BC6
CSTATUS_INVALID_DATA = 0xC0000BBA










class PdhEngineQuery:
    """One persistent wildcard query; no subprocess is launched per sample."""

    def __init__(self):
        if os.name != "nt":
            raise SmokeFailure("passive GPU-engine telemetry requires Windows PDH")
        dword, handle = ctypes.c_uint32, ctypes.c_void_p

        class CounterUnion(ctypes.Union):
            _fields_ = [("double_value", ctypes.c_double), ("large_value", ctypes.c_int64)]

        class CounterValue(ctypes.Structure):
            _fields_ = [("status", dword), ("value", CounterUnion)]

        class CounterItem(ctypes.Structure):
            _fields_ = [("name", ctypes.c_wchar_p), ("counter", CounterValue)]

        self.item_type = CounterItem
        self.api = ctypes.WinDLL("pdh.dll")
        signatures = {
            "PdhOpenQueryW": ([ctypes.c_wchar_p, ctypes.c_size_t, ctypes.POINTER(handle)], dword),
            "PdhAddEnglishCounterW": ([handle, ctypes.c_wchar_p, ctypes.c_size_t, ctypes.POINTER(handle)], dword),
            "PdhCollectQueryData": ([handle], dword),
            "PdhGetFormattedCounterArrayW": ([handle, dword, ctypes.POINTER(dword), ctypes.POINTER(dword), ctypes.c_void_p], dword),
            "PdhCloseQuery": ([handle], dword),
        }
        for name, (arguments, result) in signatures.items():
            method = getattr(self.api, name)
            method.argtypes, method.restype = arguments, result
        self.query, self.counter = handle(), handle()
        self._require(self.api.PdhOpenQueryW(None, 0, ctypes.byref(self.query)), "open")
        try:
            self._require(self.api.PdhAddEnglishCounterW(self.query, COUNTER_PATH, 0, ctypes.byref(self.counter)), "add engine counter")
        except Exception:
            try:
                self.close()
            except Exception as cleanup_error:
                write_status(f"PDH cleanup after setup failure: {cleanup_error}")
            raise

    @staticmethod
    def _require(status, operation):
        if status:
            raise SmokeFailure(f"PDH {operation} failed: 0x{status:08x}")

    def collect(self):
        status = self.api.PdhCollectQueryData(self.query)
        if status in (NO_DATA, INVALID_DATA, CSTATUS_INVALID_DATA):
            return status, []
        self._require(status, "collect")
        size, count = ctypes.c_uint32(), ctypes.c_uint32()
        status = self.api.PdhGetFormattedCounterArrayW(self.counter, 0x200 | 0x8000, ctypes.byref(size), ctypes.byref(count), None)
        if status in (NO_DATA, INVALID_DATA, CSTATUS_INVALID_DATA):
            return status, []
        if status != MORE_DATA or not size.value:
            self._require(status or INVALID_DATA, "size engine samples")
        storage = ctypes.create_string_buffer(size.value)
        status = self.api.PdhGetFormattedCounterArrayW(self.counter, 0x200 | 0x8000, ctypes.byref(size), ctypes.byref(count), storage)
        if status in (NO_DATA, INVALID_DATA, CSTATUS_INVALID_DATA, MORE_DATA):
            # Skip transient PDH buffer races and retry on the next 1 Hz sample.
            return status, []
        self._require(status, "read engine samples")
        items = ctypes.cast(storage, ctypes.POINTER(self.item_type))
        return 0, [{"instance": items[index].name, "status": items[index].counter.status,
            "value": items[index].counter.value.double_value if items[index].counter.status in VALID_STATUSES else None} for index in range(count.value)]

    def close(self):
        if self.query:
            status = self.api.PdhCloseQuery(self.query)
            self.query = ctypes.c_void_p()
            self._require(status, "close")


class WindowsGpuTelemetry:
    def __init__(self, directory, fixture_pid, period_seconds=1.):
        self.directory, self.fixture_pid = Path(directory), fixture_pid
        self.period = period_seconds
        self.query = PdhEngineQuery()
        self.stopped = threading.Event()
        self.samples, self.errors, self.header_observations = [], [], []
        self.thread = threading.Thread(target=self._run, name="smoke-gpu-load", daemon=True)
        self.thread.start()

    def _run(self):
        try:
            with (self.directory / "gpu_load.jsonl").open("x", encoding="utf-8") as stream:
                while not self.stopped.is_set():
                    begin = time.monotonic()
                    status, instances = self.query.collect()
                    sample = {"monotonic_seconds": begin, "utc": datetime.now(timezone.utc).isoformat(), "fixture_pid": self.fixture_pid,
                        "query_status": status, "counter_instances": instances, **GpuLoadAnalysis.reduce_instances(instances, self.fixture_pid)}
                    self.samples.append(sample)
                    stream.write(json.dumps(sample, allow_nan=False) + "\n")
                    stream.flush()
                    self.stopped.wait(max(0, self.period - (time.monotonic() - begin)))
        except Exception as error:
            self.errors.append(str(error))
        finally:
            try:
                self.query.close()
            except Exception as error:
                self.errors.append(str(error))

    def observe_timing_headers(self, durations):
        observed = time.monotonic()
        self.header_observations.extend([observed] * (len(durations) - len(self.header_observations)))

    def finish(self, start, stop, durations):
        self.close()
        bounds = GpuLoadAnalysis.retained_publication_bounds(self.header_observations, start, stop, durations)
        result = {"provider": "Windows PDH GPU Engine / Utilization Percentage", "counter_path": COUNTER_PATH, "sample_period_seconds": self.period,
            "gpu_frequency": {"available": False, "reason": "This sampler records engine load only. No frequency values are acquired; consult the campaign host inventory for exposed counters and tools."},
            "gpu_temperature": {"available": False, "reason": "Available ACPI thermal zones do not identify the GPU."},
            "errors": self.errors, "header_observations_monotonic_seconds": self.header_observations, **GpuLoadAnalysis.summarize_samples(self.samples, bounds),
            "raw_samples": str(self.directory / "gpu_load.jsonl")}
        result["eligible"] = result["eligible"] and not self.errors
        (self.directory / "gpu_load_summary.json").write_text(json.dumps(result, indent=2, allow_nan=False) + "\n", encoding="utf-8")
        return result

    def close(self):
        self.stopped.set()
        self.thread.join(timeout=5)
        if self.thread.is_alive():
            raise SmokeFailure("GPU telemetry sampler did not stop within five seconds")
