"""Safely stop matching Linux processes using pidfds."""
import math
import os
import select
import signal
from pathlib import Path

from launcher.constants import APPLICATION_GRACEFUL_STOP_TIMEOUT_SECONDS, PLATFORM_LINUX

LINUX_PF_KTHREAD = 0x00200000


class LinuxProcessStopper:
    @staticmethod
    def stop_existing_process(executable: Path, platform_name: str) -> None:
        if platform_name != PLATFORM_LINUX or not hasattr(os, "pidfd_open") or not hasattr(signal, "pidfd_send_signal"):
            raise SystemExit("--kill-existing requires Linux pidfd support on this host")

        def exited(handle: int, timeout_seconds: float) -> bool:
            poller = select.poll()
            poller.register(handle, select.POLLIN)
            events = poller.poll(math.ceil(timeout_seconds * 1000))
            if any(flags & (select.POLLERR | select.POLLNVAL) for _, flags in events):
                raise OSError("failed to wait for process")
            return any(flags & (select.POLLIN | select.POLLHUP) for _, flags in events)

        target = str(executable.resolve())
        handles = []
        try:
            try:
                process_names = os.listdir("/proc")
                own_pid = os.getpid()
                own_uid = os.geteuid()
                for name in process_names:
                    if not name.isdecimal() or int(name) == own_pid:
                        continue
                    pid = int(name)
                    proc_dir = f"/proc/{pid}"
                    try:
                        handle = os.pidfd_open(pid)
                    except (FileNotFoundError, ProcessLookupError):
                        continue
                    try:
                        if own_uid != 0 and os.stat(proc_dir).st_uid != own_uid:
                            continue
                        image_path = os.readlink(f"{proc_dir}/exe")
                        if image_path == target:
                            if target.endswith(" (deleted)"):
                                raise SystemExit(f"cannot safely identify an ambiguously named process image for '{executable}' (pid {pid})")
                            handles.append((pid, handle))
                            handle = None
                        elif image_path == target + " (deleted)":
                            raise SystemExit(f"cannot safely identify a deleted or ambiguously named process image for '{executable}' (pid {pid})")
                    except (FileNotFoundError, ProcessLookupError):
                        if not exited(handle, 0):
                            try:
                                with open(f"{proc_dir}/status", encoding="utf-8", errors="replace") as status:
                                    kernel_flag = next((value.strip() for key, separator, value in (line.partition(":") for line in status) if key == "Kthread" and separator), None)
                                if kernel_flag is None:
                                    with open(f"{proc_dir}/stat", encoding="utf-8", errors="replace") as stat_file:
                                        stat_text = stat_file.read()
                                    _, separator, fields_text = stat_text.rpartition(")")
                                    fields = fields_text.split()
                                    if not separator or len(fields) < 7 or not fields[6].isdecimal():
                                        raise OSError("invalid process stat flags")
                                    kernel_thread = bool(int(fields[6]) & LINUX_PF_KTHREAD)
                                else:
                                    kernel_thread = kernel_flag == "1"
                            except (FileNotFoundError, ProcessLookupError):
                                kernel_thread = False
                            if not kernel_thread and not exited(handle, 0):
                                raise SystemExit(f"cannot inspect live process {pid} while stopping '{executable}'")
                    finally:
                        if handle is not None:
                            os.close(handle)

                for _pid, handle in handles:
                    try:
                        signal.pidfd_send_signal(handle, signal.SIGTERM)
                    except ProcessLookupError:
                        continue
                for _pid, handle in handles:
                    if not exited(handle, APPLICATION_GRACEFUL_STOP_TIMEOUT_SECONDS):
                        raise SystemExit(f"existing process for '{executable}' did not exit after SIGTERM")
            except OSError as exc:
                raise SystemExit(f"failed to inspect or stop existing processes for '{executable}': {exc}") from exc
        finally:
            for _pid, handle in handles:
                os.close(handle)

