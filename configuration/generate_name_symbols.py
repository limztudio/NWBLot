#!/usr/bin/env python3
# limztudio@gmail.com
#
# Build-time Name-symbol generator.
#
# Drives the three-step capture the user asked for so opt/fin logs read as text without anyone building twice by hand:
#   1. configure + build a NWB_BUILDMODE variant (separate output domain "namesym", does not touch the release tree),
#   2. run that variant's workloads so each executable records every Name literal it touches and, on exit, writes its
#      "<exe>.namesym" sidecar next to itself (core/common/application_entry.h -> NameSymbols::WriteDefaultFile),
#   3. copy those sidecars into the release output so the matching release exe loads them at startup
#      (NameSymbols::LoadDefaultFile) and Name::c_str() resolves its own hashes locally.
#
# This is invoked by the on-demand "nwb_namesym" CMake target (configuration/NameSymbols.cmake), which passes the
# resolved buildmode/release paths for the configuration being built. It can also be run directly for one-off captures.
#
# Note on coverage: capture is runtime-driven, so a name is only recorded if the run actually reaches it. GUI targets
# (testbed / window-capture smokes) need a display; they are driven here through their existing CTest tests so the
# capture harness launches and tears them down. Headless targets (the resource cooker) are run directly.

import argparse
import glob
import os
import shutil
import subprocess
import sys


def log(message):
    print("[namesym] {}".format(message), flush=True)


def run_command(command, cwd=None):
    log("$ {}".format(" ".join(command)))
    completed = subprocess.run(command, cwd=cwd)
    return completed.returncode


def executable_path(directory, name):
    candidate = os.path.join(directory, name)
    if os.name == "nt" and not name.lower().endswith(".exe"):
        candidate += ".exe"
    return candidate


def parse_arguments(argv):
    parser = argparse.ArgumentParser(description="Generate Name-symbol sidecars from a NWB_BUILDMODE run.")
    parser.add_argument("--source-dir", required=True, help="Repository root (where CMakePresets.json lives).")
    parser.add_argument("--configure-preset", required=True, help="Buildmode configure preset name.")
    parser.add_argument("--build-preset", required=True, help="Buildmode build preset name (config-specific).")
    parser.add_argument("--build-dir", required=True, help="Buildmode binaryDir (for ctest).")
    parser.add_argument("--config", required=True, help="Build configuration (dbg/opt/fin).")
    parser.add_argument("--buildmode-bin-dir", required=True, help="Directory where buildmode exes + .namesym land.")
    parser.add_argument("--dest", action="append", default=[], help="Destination dir for the sidecars (repeatable).")
    parser.add_argument("--run", action="append", default=[], help='Headless run spec "exe|||arg|||arg" (repeatable).')
    parser.add_argument("--ctest-regex", action="append", default=[], help="ctest -R regex for GUI targets (repeatable).")
    parser.add_argument("--skip-configure", action="store_true", help="Skip the buildmode configure step.")
    parser.add_argument("--skip-build", action="store_true", help="Skip the buildmode configure + build steps.")
    return parser.parse_args(argv)


def configure_and_build(arguments):
    if arguments.skip_build:
        log("skipping buildmode configure + build (--skip-build)")
        return True

    if not arguments.skip_configure:
        if run_command(["cmake", "--preset", arguments.configure_preset], cwd=arguments.source_dir) != 0:
            log("buildmode configure failed")
            return False

    if run_command(["cmake", "--build", "--preset", arguments.build_preset], cwd=arguments.source_dir) != 0:
        log("buildmode build failed")
        return False

    return True


def run_workloads(arguments):
    # Individual workload failures are not fatal: a crashed/timed-out run still writes whatever it recorded before
    # exiting, and a partial table beats no table. We only hard-fail later if nothing at all was produced.
    for spec in arguments.run:
        parts = spec.split("|||")
        exe = executable_path(arguments.buildmode_bin_dir, parts[0])
        if not os.path.isfile(exe):
            log("WARNING: headless target not found, skipping: {}".format(exe))
            continue

        if run_command([exe] + parts[1:], cwd=arguments.buildmode_bin_dir) != 0:
            log("WARNING: headless run returned nonzero (sidecar still captured if it reached exit): {}".format(parts[0]))

    for regex in arguments.ctest_regex:
        # ctest launches the window-capture harness, which runs each matched GUI target and tears it down. Needs a
        # display; on a headless host the tests skip (return code 77) rather than fail.
        run_command(
            ["ctest", "--test-dir", arguments.build_dir, "-C", arguments.config, "-R", regex, "--output-on-failure"],
            cwd=arguments.source_dir,
        )


def collect_sidecars(arguments):
    produced = sorted(glob.glob(os.path.join(arguments.buildmode_bin_dir, "*.namesym")))
    if not produced:
        log("ERROR: no .namesym sidecars were produced under {}".format(arguments.buildmode_bin_dir))
        return False

    log("captured {} sidecar(s): {}".format(len(produced), ", ".join(os.path.basename(p) for p in produced)))

    copied_any = False
    for dest in arguments.dest:
        os.makedirs(dest, exist_ok=True)
        for sidecar in produced:
            shutil.copy2(sidecar, os.path.join(dest, os.path.basename(sidecar)))
            copied_any = True
        log("bundled sidecars into {}".format(dest))

    if not arguments.dest:
        log("WARNING: no --dest given; sidecars left in the buildmode output only")

    return copied_any or not arguments.dest


def main(argv):
    arguments = parse_arguments(argv)

    if not configure_and_build(arguments):
        return 1

    run_workloads(arguments)

    if not collect_sidecars(arguments):
        return 1

    log("done")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
