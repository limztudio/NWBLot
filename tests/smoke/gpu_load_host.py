"""Collect passive Windows GPU host capability inventory."""

import json
import os
import shutil
import subprocess

from window_capture_smoke import SmokeFailure


class GpuHostInventory:
    @staticmethod
    def collect():
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
