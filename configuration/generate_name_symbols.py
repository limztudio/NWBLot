#!/usr/bin/env python3
# limztudio@gmail.com
#
# Build and run the isolated NWB_BUILDMODE variant, then copy Name sidecars to the release output.
# Invoked by nwb_namesym or directly. Coverage includes only names reached by the workloads;
# GUI captures use CTest and need a display, while headless tools run directly.

import argparse
import glob
import os
import shutil
import subprocess
import sys


def log(message):
    print("[namesym] {}".format(message), flush=True)


# Keep CMAKE_COMMAND as one argv item so executable paths may contain spaces.
def resolve_cmake_command():
    cmake_command = os.environ.get("CMAKE_COMMAND")
    return [cmake_command] if cmake_command else ["cmake"]


def resolve_ctest_command():
    # Resolve ctest beside cmake. Relative paths are checked when launched from source-dir,
    # not against this script's build-directory working directory.
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


CTEST_NO_TESTS_EXIT_CODE = 8


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
    # Clear persistent sidecars so collection cannot mistake output from an interrupted run for current output.
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

    # Failed workloads may still write useful sidecars; collect_sidecars checks the fresh output.
    for spec in arguments.run:
        parts = spec.split("|||")
        if not parts or not parts[0]:
            log("WARNING: empty --run spec, skipping")
            continue

        is_script = parts[0].lower().endswith(".py")
        exe = os.path.abspath(parts[0]) if is_script else executable_path(arguments.buildmode_bin_dir, parts[0])
        if not os.path.isfile(exe):
            log("WARNING: headless target not found, skipping: {}".format(exe))
            continue

        command = [sys.executable, exe] if is_script else [exe]
        if run_command(command + parts[1:], cwd=arguments.buildmode_bin_dir) != 0:
            log("WARNING: headless run returned nonzero (sidecar still captured if it reached an exit handler): {}".format(parts[0]))

    # GUI workloads must exit gracefully; a hard kill prevents the sidecar write.
    ctest = resolve_ctest_command()
    for regex in arguments.ctest_regex:
        rc = run_command(
            ctest + ["--test-dir", arguments.build_dir, "-C", arguments.config, "-R", regex, "--output-on-failure"],
            cwd=arguments.source_dir,
        )
        if rc == CTEST_NO_TESTS_EXIT_CODE:
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
