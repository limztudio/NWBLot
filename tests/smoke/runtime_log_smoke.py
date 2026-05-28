#!/usr/bin/env python3
import argparse
from pathlib import Path
import sys
import time


sys.path.insert(0, str(Path(__file__).resolve().parent))

from testbed_window_capture_smoke import (  # noqa: E402
    SKIP_EXIT_CODE,
    SmokeFailure,
    SmokeSkip,
    build_launch_environment,
    collect_log_delta,
    launch_logserver,
    launch_testbed,
    read_process_tail,
    terminate_process,
    write_status,
)


def wait_for_process_exit(process, timeout_seconds):
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        exit_code = process.poll()
        if exit_code is not None:
            return exit_code
        time.sleep(0.1)

    raise SmokeFailure(f"process did not exit within {timeout_seconds:.1f} seconds")


def parse_args(argv):
    parser = argparse.ArgumentParser(description="Launch an NWB executable and validate runtime log output.")
    parser.add_argument("--executable", required=True, help="Path to the executable.")
    parser.add_argument("--working-directory", type=Path, default=Path.cwd(), help="Working directory for launched processes.")
    parser.add_argument("--timeout", type=float, default=180.0, help="Seconds to wait for the executable to finish.")
    parser.add_argument("--logserver-executable", help="Path to nwb_logserver/logserver. Defaults to a sibling of --executable.")
    parser.add_argument("--no-logserver", action="store_true", help="Do not start a logserver or pass log CLI options.")
    parser.add_argument("--log-port", type=int, default=0, help="Logserver port. Defaults to an unused localhost port.")
    parser.add_argument("--expect-log-message", action="append", default=[], help="Required substring in the logserver output.")
    parser.add_argument("--application-arg", action="append", default=[], help="Extra argument to pass to the launched application.")
    parser.add_argument("--software-vulkan", choices=("auto", "on", "off"), default="off", help="Linux Vulkan ICD selection.")
    args = parser.parse_args(argv)
    if args.timeout <= 0.0:
        parser.error("--timeout must be positive")
    args.working_directory = args.working_directory.resolve()
    return args


def run(args):
    executable = Path(args.executable).resolve()
    if not executable.exists():
        raise SmokeFailure(f"executable does not exist: {executable}")

    env = build_launch_environment(args)
    logserver_process = None
    runtime_process = None
    try:
        logserver_process, log_port, log_directory, log_baseline = launch_logserver(args, executable, env)
        runtime_process = launch_testbed(args, executable, env, log_port)
        exit_code = wait_for_process_exit(runtime_process, args.timeout)
        tail = read_process_tail(runtime_process)
        log_text = collect_log_delta(log_directory, log_baseline, "logserver_*.log") if log_directory else tail

        if exit_code != 0:
            raise SmokeFailure(f"runtime exited with {exit_code}\n{tail}\n{log_text[-4000:]}")

        for needle in args.expect_log_message:
            if needle not in log_text:
                raise SmokeFailure(f"missing log message '{needle}'\n{log_text[-4000:]}")

        benchmark_lines = [line for line in log_text.splitlines() if "SkinnedConeBenchmark:" in line]
        write_status("\n".join(benchmark_lines[-32:]) if benchmark_lines else "runtime completed")
        return 0
    finally:
        terminate_process(runtime_process, "runtime")
        terminate_process(logserver_process, "logserver")


def main(argv):
    try:
        return run(parse_args(argv))
    except SmokeSkip as exc:
        write_status(f"SKIP: {exc}")
        return SKIP_EXIT_CODE
    except SmokeFailure as exc:
        write_status(f"FAIL: {exc}")
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
