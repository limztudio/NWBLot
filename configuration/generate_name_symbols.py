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
import shlex
import shutil
import subprocess
import sys


def log(message):
    print("[namesym] {}".format(message), flush=True)


# Resolve the CMake executable the same way launcher.py does: CMAKE_COMMAND overrides everything, otherwise fall back to
# the bare "cmake"/"ctest" on PATH. The project's CMake lives in a local Python venv that is not on PATH, so honoring
# CMAKE_COMMAND lets the namesym target drive it without each caller having to edit the tree.
def resolve_cmake_command():
    return shlex.split(os.environ.get("CMAKE_COMMAND", "")) or ["cmake"]


def resolve_ctest_command():
    # ctest lives alongside cmake. When CMAKE_COMMAND names a cmake binary, derive ctest by replacing the basename's
    # "cmake" with "ctest" (handles both real binaries and the venv's "cmake"/"ctest" wrapper pair). We deliberately do
    # NOT validate with os.path.isfile here: this script's CWD is the build directory (ninja runs custom commands there),
    # while run_command() invokes ctest with cwd=source-dir, so a relative path only resolves correctly there. Shelling
    # out with a path that is valid relative to source-dir is exactly what we want.
    cmake_command = resolve_cmake_command()
    if cmake_command == ["cmake"]:
        return ["ctest"]
    cmake_path = cmake_command[0]
    directory = os.path.dirname(cmake_path)
    base = os.path.basename(cmake_path)
    ctest_base = "ctest.exe" if os.name == "nt" and base.lower().endswith(".exe") else "ctest"
    if directory:
        return [os.path.join(directory, ctest_base)]
    return [ctest_base]


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
    parser.add_argument("--mkdir", action="append", default=[], help="Directory to create before running workloads (repeatable).")
    parser.add_argument("--run", action="append", default=[], help='Headless run spec "exe|||arg|||arg" (repeatable).')
    parser.add_argument("--ctest-regex", action="append", default=[], help="ctest -R regex for GUI targets (repeatable).")
    parser.add_argument("--expect-sidecar", action="append", default=[], help="Sidecar basename that MUST be produced; warn if missing (repeatable).")
    parser.add_argument("--skip-configure", action="store_true", help="Skip the buildmode configure step.")
    parser.add_argument("--skip-build", action="store_true", help="Skip the buildmode configure + build steps.")
    return parser.parse_args(argv)


def configure_and_build(arguments):
    if arguments.skip_build:
        log("skipping buildmode configure + build (--skip-build)")
        return True

    cmake = resolve_cmake_command()

    if not arguments.skip_configure:
        if run_command(cmake + ["--preset", arguments.configure_preset], cwd=arguments.source_dir) != 0:
            log("buildmode configure failed")
            return False

    if run_command(cmake + ["--build", "--preset", arguments.build_preset], cwd=arguments.source_dir) != 0:
        log("buildmode build failed")
        return False

    return True


def clean_stale_sidecars(arguments):
    # A capture must reflect THIS run only. WriteDefaultFile writes "<exe>.namesym" into the (persistent) buildmode bin
    # dir, so a workload that crashes or is hard-killed before its graceful exit would otherwise leave a PRIOR run's
    # sidecar in place -- which collect would then copy as if freshly captured. Clearing first makes a post-run empty
    # glob correctly hard-fail, and prevents a silent stale/partial-stale mix from reaching the release logserver.
    removed = 0
    for old in glob.glob(os.path.join(arguments.buildmode_bin_dir, "*.namesym")):
        try:
            os.remove(old)
            removed += 1
        except OSError as error:
            log("WARNING: could not remove stale sidecar {}: {}".format(old, error))
    if removed:
        log("cleared {} stale sidecar(s) from {}".format(removed, arguments.buildmode_bin_dir))


def run_workloads(arguments):
    for directory in arguments.mkdir:
        os.makedirs(directory, exist_ok=True)

    # Headless runs. A nonzero exit is only a warning: a run can still write its sidecar on a non-clean exit (the app
    # entry also writes it on init-failure / exception), and a partial table beats none. The post-run collect against a
    # freshly-cleaned dir is what actually decides whether anything was produced.
    for spec in arguments.run:
        parts = spec.split("|||")
        if not parts or not parts[0]:
            log("WARNING: empty --run spec, skipping")
            continue

        exe = executable_path(arguments.buildmode_bin_dir, parts[0])
        if not os.path.isfile(exe):
            log("WARNING: headless target not found, skipping: {}".format(exe))
            continue

        if run_command([exe] + parts[1:], cwd=arguments.buildmode_bin_dir) != 0:
            log("WARNING: headless run returned nonzero (sidecar still captured if it reached an exit handler): {}".format(parts[0]))

    # GUI runs via ctest. IMPORTANT: a window-capture test that HARD-KILLS its app (TerminateProcess) prevents the
    # app's graceful-exit sidecar write -- such a target must support a graceful / auto-quit shutdown to contribute
    # symbols (otherwise this run produces nothing and is caught by --expect-sidecar below).
    ctest = resolve_ctest_command()
    for regex in arguments.ctest_regex:
        rc = run_command(
            ctest + ["--test-dir", arguments.build_dir, "-C", arguments.config, "-R", regex, "--output-on-failure"],
            cwd=arguments.source_dir,
        )
        if rc == 8:
            log("WARNING: ctest found NO tests matching '{}' (GUI sidecars will be missing)".format(regex))
        elif rc != 0:
            log("WARNING: ctest returned {} for '{}' (GUI run failed/skipped; its sidecars may be missing)".format(rc, regex))


def collect_sidecars(arguments):
    produced = sorted(glob.glob(os.path.join(arguments.buildmode_bin_dir, "*.namesym")))
    if not produced:
        log("ERROR: no .namesym sidecars were produced under {}".format(arguments.buildmode_bin_dir))
        return False

    names = [os.path.basename(path) for path in produced]
    log("captured {} sidecar(s): {}".format(len(produced), ", ".join(names)))

    for expected in arguments.expect_sidecar:
        if expected not in names:
            log("WARNING: expected sidecar '{}' was NOT produced -- its workload likely failed or was hard-killed before a graceful exit; those symbols will stay as hashes".format(expected))

    for dest in arguments.dest:
        os.makedirs(dest, exist_ok=True)
        for sidecar in produced:
            shutil.copy2(sidecar, os.path.join(dest, os.path.basename(sidecar)))
        log("bundled {} sidecar(s) into {}".format(len(produced), dest))

    if not arguments.dest:
        log("WARNING: no --dest given; sidecars left in the buildmode output only")

    return True


def main(argv):
    arguments = parse_arguments(argv)

    if not configure_and_build(arguments):
        return 1

    # Only clear when we will actually regenerate sidecars; a pure collect (no workloads) must keep what is there.
    if arguments.run or arguments.ctest_regex:
        clean_stale_sidecars(arguments)

    run_workloads(arguments)

    if not collect_sidecars(arguments):
        return 1

    log("done")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
