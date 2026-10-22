#!/usr/bin/env python3
"""Passive Windows PDH GPU-engine observations for smoke timing campaigns.

These counters expose engine load, not GPU frequency. Publication-window
alignment is approximate; raw timestamps and invalid samples remain available.
"""

import ctypes
from datetime import datetime, timezone
import json
import math
import os
from pathlib import Path
import re
import shutil
import statistics
import subprocess
import threading
import time

from window_capture_smoke import SmokeFailure

COUNTER_PATH = r"\GPU Engine(*)\Utilization Percentage"
PID_INSTANCE = re.compile(r"^pid_(\d+)_(luid_.+_phys_\d+_eng_\d+_engtype_.+)$", re.IGNORECASE)
VALID_STATUSES = (0, 1)
MORE_DATA = 0x800007D2
NO_DATA = 0x800007D5
INVALID_DATA = 0xC0000BC6
CSTATUS_INVALID_DATA = 0xC0000BBA


def host_inventory():
    if os.name != "nt":
        raise SmokeFailure("passive GPU-engine profiling requires Windows host inventory")
    shell = shutil.which("pwsh.exe") or shutil.which("powershell.exe")
    if shell is None:
        raise SmokeFailure("Windows host inventory requires PowerShell")
    script = r"""$ErrorActionPreference='Stop'
$gpu=Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion
$sets=Get-Counter -ListSet '*GPU*' -ErrorAction SilentlyContinue
$paths=@($sets | ForEach-Object {$_.Paths})
$vendor=Get-Command nvidia-smi -ErrorAction SilentlyContinue
[ordered]@{gpu=@($gpu);gpu_counter_paths=$paths;gpu_clock_counter_candidates=@($paths | Where-Object {$_ -match 'clock|frequency'});nvidia_smi=if($vendor){$vendor.Source}else{$null};clock_values_collected=$false;gpu_temperature_values_collected=$false} | ConvertTo-Json -Depth 8 -Compress
"""
    result = subprocess.run([shell, "-NoProfile", "-NonInteractive", "-Command", script], check=True, capture_output=True, text=True, timeout=30,
        creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    return json.loads(result.stdout.lstrip("\ufeff"))


def reduce_instances(items, fixture_pid):
    """Keep per-engine sums separate; engines execute concurrently."""
    engines, invalid, unmatched = {}, [], []
    for item in items:
        name, status, value = item["instance"], item["status"], item.get("value")
        if status not in VALID_STATUSES or value is None or not math.isfinite(value) or value < 0:
            invalid.append(item)
            continue
        match = PID_INSTANCE.fullmatch(name)
        if match is None:
            unmatched.append(item)
            continue
        pid, engine = int(match[1]), match[2].lower()
        row = engines.setdefault(engine, {"fixture_percent": 0., "other_processes_percent": 0.})
        row["fixture_percent" if pid == fixture_pid else "other_processes_percent"] += value
    return {"engines": engines, "invalid_instances": invalid, "unmatched_instances": unmatched,
        "fixture_busiest_engine_percent": max((row["fixture_percent"] for row in engines.values()), default=None),
        "other_processes_busiest_engine_percent": max((row["other_processes_percent"] for row in engines.values()), default=None),
        "fixture_instances_present": any(PID_INSTANCE.fullmatch(item["instance"]) and int(PID_INSTANCE.fullmatch(item["instance"])[1]) == fixture_pid
            and item["status"] in VALID_STATUSES and item.get("value") is not None and math.isfinite(item["value"]) and item["value"] >= 0 for item in items)}


def retained_publication_bounds(observations, start, stop, durations):
    if start < 0 or stop <= start or stop > len(observations) or stop > len(durations):
        raise SmokeFailure("telemetry lacks retained publication-header observations")
    if any(not math.isfinite(value) or value <= 0 for value in durations[start:stop]):
        raise SmokeFailure("telemetry retained publication durations must be finite and positive")
    # A header is written after the publication interval that it describes.
    # Counter completion and buffered log publication can lag GPU execution.
    return observations[start] - durations[start], observations[stop - 1]


def summarize_samples(samples, bounds):
    start, stop = bounds
    selected = [sample for sample in samples if start <= sample["monotonic_seconds"] <= stop]
    valid = [sample for sample in selected if sample.get("query_status") == 0 and sample.get("fixture_instances_present")]
    result = {"retained_publication_bounds_monotonic_seconds": [start, stop], "retained_samples": len(selected), "valid_fixture_samples": len(valid),
        "eligible": len(valid) >= 2, "interpretation": "1 Hz host observations aligned approximately to timing publication windows. Per-engine process sums may exceed 100%; concurrent engines are not summed into a GPU percentage. No GPU clock or causal DVFS inference."}
    for field in ("fixture_busiest_engine_percent", "other_processes_busiest_engine_percent"):
        values = [sample[field] for sample in valid if sample.get(field) is not None]
        result[field] = {"mean": statistics.fmean(values), "minimum": min(values), "maximum": max(values)} if values else None
    return result


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
            self.close()
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
            # Instances may appear between sizing and reading. Retain this gap;
            # the next 1 Hz observation retries without a blocking sample loop.
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
                        "query_status": status, "counter_instances": instances, **reduce_instances(instances, self.fixture_pid)}
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
        bounds = retained_publication_bounds(self.header_observations, start, stop, durations)
        result = {"provider": "Windows PDH GPU Engine / Utilization Percentage", "counter_path": COUNTER_PATH, "sample_period_seconds": self.period,
            "gpu_frequency": {"available": False, "reason": "This sampler records engine load only. No frequency values are acquired; consult the campaign host inventory for exposed counters and tools."},
            "gpu_temperature": {"available": False, "reason": "Available ACPI thermal zones do not identify the GPU."},
            "errors": self.errors, "header_observations_monotonic_seconds": self.header_observations, **summarize_samples(self.samples, bounds),
            "raw_samples": str(self.directory / "gpu_load.jsonl")}
        result["eligible"] = result["eligible"] and not self.errors
        (self.directory / "gpu_load_summary.json").write_text(json.dumps(result, indent=2, allow_nan=False) + "\n", encoding="utf-8")
        return result

    def close(self):
        self.stopped.set()
        self.thread.join(timeout=5)
        if self.thread.is_alive():
            raise SmokeFailure("GPU telemetry sampler did not stop within five seconds")
