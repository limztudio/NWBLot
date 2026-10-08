#!/usr/bin/env python3
# limztudio@gmail.com
# Only Names reached by workloads are captured. GUI captures need a display; headless tools run directly.

import argparse
import glob
import os
import shutil
import subprocess
import sys

MAIN_ENTRY = "__main__"


LOG_PREFIX = "[namesym] "
LOG_COMMAND_PREFIX = "$ "
CMAKE_COMMAND_ENV = "CMAKE_COMMAND"
CMAKE_DEFAULT = "cmake"
CTEST_DEFAULT = "ctest"
CTEST_EXE_WINDOWS = "ctest.exe"
EXE_SUFFIX = ".exe"
OS_WINDOWS = "nt"
SIDECAR_GLOB = "*.namesym"
RUN_SPEC_SEPARATOR = "|||"
PYTHON_SCRIPT_SUFFIX = ".py"
ARG_SOURCE_DIR = "--source-dir"
ARG_CONFIGURE_PRESET = "--configure-preset"
ARG_BUILD_PRESET = "--build-preset"
ARG_BUILD_DIR = "--build-dir"
ARG_CONFIG = "--config"
ARG_BUILDMODE_BIN_DIR = "--buildmode-bin-dir"
ARG_DEST = "--dest"
ARG_MKDIR = "--mkdir"
ARG_RUN = "--run"
ARG_CTEST_REGEX = "--ctest-regex"
ARG_EXPECT_SIDECAR = "--expect-sidecar"
ARG_SKIP_CONFIGURE = "--skip-configure"
ARG_SKIP_BUILD = "--skip-build"
ARG_PRESET_FLAG = "--preset"
ARG_BUILD_FLAG = "--build"
ARG_TEST_DIR_FLAG = "--test-dir"
ARG_TEST_CONFIG_FLAG = "-C"
ARG_TEST_REGEX_FLAG = "-R"
ARG_OUTPUT_ON_FAILURE = "--output-on-failure"
ACTION_APPEND = "append"
ACTION_STORE_TRUE = "store_true"
LOG_JOIN_SEPARATOR = " "
LOG_LIST_SEPARATOR = ", "
DESC_GENERATE = "Generate Name-symbol sidecars from a NWB_BUILDMODE run."
HELP_SOURCE_DIR = "Repository root (where CMakePresets.json lives)."
HELP_CONFIGURE_PRESET = "Buildmode configure preset name."
HELP_BUILD_PRESET = "Buildmode build preset name (config-specific)."
HELP_BUILD_DIR = "Buildmode binaryDir (for ctest)."
HELP_CONFIG = "Build configuration (dbg/opt/fin)."
HELP_BUILDMODE_BIN_DIR = "Directory where buildmode exes + .namesym land."
HELP_DEST = "Destination dir for the sidecars (repeatable)."
HELP_MKDIR = "Directory to create before running workloads (repeatable)."
HELP_RUN = 'Headless run spec "exe|||arg|||arg" (repeatable).'
HELP_CTEST_REGEX = "ctest -R regex for GUI targets (repeatable)."
HELP_EXPECT_SIDECAR = "Sidecar basename that MUST be produced; warn if missing (repeatable)."
HELP_SKIP_CONFIGURE = "Skip the buildmode configure step."
HELP_SKIP_BUILD = "Skip the buildmode configure + build steps."
MSG_SKIP_BUILD = "skipping buildmode configure + build (--skip-build)"
MSG_CONFIGURE_FAILED = "buildmode configure failed"
MSG_BUILD_FAILED = "buildmode build failed"
MSG_STALE_REMOVE_FAIL = "ERROR: could not remove stale sidecar {}: {}"
MSG_CLEARED_STALE = "cleared {} stale sidecar(s) from {}"
MSG_EMPTY_RUN = "WARNING: empty --run spec, skipping"
MSG_TARGET_NOT_FOUND = "WARNING: headless target not found, skipping: {}"
MSG_RUN_NONZERO = "WARNING: headless run returned nonzero (sidecar still captured if it reached an exit handler): {}"
MSG_CTEST_NO_MATCH = "WARNING: ctest found NO tests matching '{}' (GUI sidecars will be missing)"
MSG_CTEST_RETURNED = "WARNING: ctest returned {} for '{}' (GUI run failed/skipped; its sidecars may be missing)"
MSG_NO_SIDECARS = "ERROR: no .namesym sidecars were produced under {}"
MSG_CAPTURED = "captured {} sidecar(s): {}"
MSG_EXPECTED_MISSING = "WARNING: expected sidecar '{}' was NOT produced -- its workload likely failed or was hard-killed before a graceful exit; those symbols will stay as hashes"
MSG_BUNDLED = "bundled {} sidecar(s) into {}"
MSG_NO_DEST = "WARNING: no --dest given; sidecars left in the buildmode output only"
MSG_DONE = "done"


def log(message):
    print(LOG_PREFIX + str(message), flush=True)


# Keep CMAKE_COMMAND as one argv item so executable paths may contain spaces.
def resolve_cmake_command():
    cmake_command = os.environ.get(CMAKE_COMMAND_ENV)
    return [cmake_command] if cmake_command else [CMAKE_DEFAULT]


def resolve_ctest_command():
    # ctest shares CMake's directory; relative paths are evaluated from source-dir.
    cmake_command = resolve_cmake_command()
    if cmake_command == [CMAKE_DEFAULT]:
        return [CTEST_DEFAULT]
    cmake_path = cmake_command[0]
    directory = os.path.dirname(cmake_path)
    base = os.path.basename(cmake_path)
    ctest_base = CTEST_EXE_WINDOWS if os.name == OS_WINDOWS and base.lower().endswith(EXE_SUFFIX) else CTEST_DEFAULT
    if directory:
        return [os.path.join(directory, ctest_base)]
    return [ctest_base]


CTEST_NO_TESTS_EXIT_CODE = 8


def run_command(command, cwd=None):
    log(LOG_COMMAND_PREFIX + LOG_JOIN_SEPARATOR.join(command))
    completed = subprocess.run(command, cwd=cwd)
    return completed.returncode


def executable_path(directory, name):
    candidate = os.path.join(directory, name)
    if os.name == OS_WINDOWS and not name.lower().endswith(EXE_SUFFIX):
        candidate += EXE_SUFFIX
    return candidate


def parse_arguments(argv):
    parser = argparse.ArgumentParser(description=DESC_GENERATE)
    parser.add_argument(ARG_SOURCE_DIR, required=True, help=HELP_SOURCE_DIR)
    parser.add_argument(ARG_CONFIGURE_PRESET, required=True, help=HELP_CONFIGURE_PRESET)
    parser.add_argument(ARG_BUILD_PRESET, required=True, help=HELP_BUILD_PRESET)
    parser.add_argument(ARG_BUILD_DIR, required=True, help=HELP_BUILD_DIR)
    parser.add_argument(ARG_CONFIG, required=True, help=HELP_CONFIG)
    parser.add_argument(ARG_BUILDMODE_BIN_DIR, required=True, help=HELP_BUILDMODE_BIN_DIR)
    parser.add_argument(ARG_DEST, action=ACTION_APPEND, default=[], help=HELP_DEST)
    parser.add_argument(ARG_MKDIR, action=ACTION_APPEND, default=[], help=HELP_MKDIR)
    parser.add_argument(ARG_RUN, action=ACTION_APPEND, default=[], help=HELP_RUN)
    parser.add_argument(ARG_CTEST_REGEX, action=ACTION_APPEND, default=[], help=HELP_CTEST_REGEX)
    parser.add_argument(ARG_EXPECT_SIDECAR, action=ACTION_APPEND, default=[], help=HELP_EXPECT_SIDECAR)
    parser.add_argument(ARG_SKIP_CONFIGURE, action=ACTION_STORE_TRUE, help=HELP_SKIP_CONFIGURE)
    parser.add_argument(ARG_SKIP_BUILD, action=ACTION_STORE_TRUE, help=HELP_SKIP_BUILD)
    return parser.parse_args(argv)


def configure_and_build(arguments):
    if arguments.skip_build:
        log(MSG_SKIP_BUILD)
        return True

    cmake = resolve_cmake_command()

    if not arguments.skip_configure:
        if run_command(cmake + [ARG_PRESET_FLAG, arguments.configure_preset], cwd=arguments.source_dir) != 0:
            log(MSG_CONFIGURE_FAILED)
            return False

    if run_command(cmake + [ARG_BUILD_FLAG, ARG_PRESET_FLAG, arguments.build_preset], cwd=arguments.source_dir) != 0:
        log(MSG_BUILD_FAILED)
        return False

    return True


def clean_stale_sidecars(arguments):
    # Discard interrupted-run sidecars before collecting fresh output.
    removed = 0
    for old in glob.glob(os.path.join(arguments.buildmode_bin_dir, SIDECAR_GLOB)):
        try:
            os.remove(old)
            removed += 1
        except OSError as error:
            log(MSG_STALE_REMOVE_FAIL.format(old, error))
            return False
    if removed:
        log(MSG_CLEARED_STALE.format(removed, arguments.buildmode_bin_dir))
    return True


def run_workloads(arguments):
    for directory in arguments.mkdir:
        os.makedirs(directory, exist_ok=True)

    # Failed workloads can still write useful sidecars.
    for spec in arguments.run:
        parts = spec.split(RUN_SPEC_SEPARATOR)
        if not parts or not parts[0]:
            log(MSG_EMPTY_RUN)
            continue

        is_script = parts[0].lower().endswith(PYTHON_SCRIPT_SUFFIX)
        exe = os.path.abspath(parts[0]) if is_script else executable_path(arguments.buildmode_bin_dir, parts[0])
        if not os.path.isfile(exe):
            log(MSG_TARGET_NOT_FOUND.format(exe))
            continue

        command = [sys.executable, exe] if is_script else [exe]
        if run_command(command + parts[1:], cwd=arguments.buildmode_bin_dir) != 0:
            log(MSG_RUN_NONZERO.format(parts[0]))

    # Graceful GUI exits are required to write sidecars.
    ctest = resolve_ctest_command()
    for regex in arguments.ctest_regex:
        rc = run_command(
            ctest + [ARG_TEST_DIR_FLAG, arguments.build_dir, ARG_TEST_CONFIG_FLAG, arguments.config, ARG_TEST_REGEX_FLAG, regex, ARG_OUTPUT_ON_FAILURE],
            cwd=arguments.source_dir,
        )
        if rc == CTEST_NO_TESTS_EXIT_CODE:
            log(MSG_CTEST_NO_MATCH.format(regex))
        elif rc != 0:
            log(MSG_CTEST_RETURNED.format(rc, regex))


def collect_sidecars(arguments):
    produced = sorted(glob.glob(os.path.join(arguments.buildmode_bin_dir, SIDECAR_GLOB)))
    if not produced:
        log(MSG_NO_SIDECARS.format(arguments.buildmode_bin_dir))
        return False

    names = [os.path.basename(path) for path in produced]
    log(MSG_CAPTURED.format(len(produced), LOG_LIST_SEPARATOR.join(names)))

    for expected in arguments.expect_sidecar:
        if expected not in names:
            log(MSG_EXPECTED_MISSING.format(expected))

    for dest in arguments.dest:
        os.makedirs(dest, exist_ok=True)
        for sidecar in produced:
            shutil.copy2(sidecar, os.path.join(dest, os.path.basename(sidecar)))
        log(MSG_BUNDLED.format(len(produced), dest))

    if not arguments.dest:
        log(MSG_NO_DEST)

    return True


def main(argv):
    arguments = parse_arguments(argv)

    if not configure_and_build(arguments):
        return 1

    # Collection without workloads must preserve existing sidecars.
    if (arguments.run or arguments.ctest_regex) and not clean_stale_sidecars(arguments):
        return 1

    run_workloads(arguments)

    if not collect_sidecars(arguments):
        return 1

    log(MSG_DONE)
    return 0


if __name__ == MAIN_ENTRY:
    sys.exit(main(sys.argv[1:]))
