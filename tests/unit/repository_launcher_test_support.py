from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

LIT_WAIT = "wait"
LIT_TERMINATE = "terminate"
LIT_KILL = "kill"

LIT_RESOLVE_EXECUTABLE_PATH = "resolve_executable_path"
LIT_CMAKE_COMMAND = "cmake_command"
LIT_BUILD_ENVIRONMENT = "build_environment"
LIT_ENSURE_FILE_API_QUERY = "ensure_file_api_query"
LIT_WINDOWS = "windows"
LIT_ARM64 = "arm64"
LIT_CMAKE_EXECUTABLE = "cmake"
LIT_BUILD = "build"
LIT_FULL = "full"
LIT_WINDOWS_CLANG_ARM64 = "windows-clang-arm64"
LIT_TESTBED = "testbed"
LIT_UTF_8 = "utf-8"
LIT_RUN = "run"
LIT_POPEN = "Popen"
LIT_HOST_PLATFORM_NAME = "host_platform_name"
LIT_LAUNCH_PY = "launch.py"
LIT_DRY_RUN = "--dry-run"
LIT_CONFIG = "--config"
LIT_OPT = "opt"
LIT_REPO_ROOT = "--repo-root"
LIT_BUILTINS_PRINT = "builtins.print"
LIT_BUILD_DIR = "--build-dir"
LIT_CONFIGURE_PRESET = "--configure-preset"
LIT_ARCH = "--arch"
LIT_PLATFORM = "--platform"
LIT_DOMAIN = "--domain"
LIT_COLD_CUSTOM_BUILD = "cold custom build"
LIT_HELP = "--help"
LIT_FORBIDDEN_LAUNCH = "A build operation entered a launch path"
LIT_MAIN = "__main__"


class FakeSpawnedProcess:
    def __init__(self, pid, exit_code, events):
        self.pid = pid
        self.exit_code = exit_code
        self.events = events
        self.returncode = None

    def wait(self, timeout=None):
        self.events.append((LIT_WAIT, timeout))
        self.returncode = self.exit_code
        return self.exit_code

    def poll(self):
        return self.returncode

    def terminate(self):
        self.events.append((LIT_TERMINATE, self.pid))

    def kill(self):
        self.events.append((LIT_KILL, self.pid))


class LauncherTestCase(unittest.TestCase):
    def make_temp_root(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        return Path(temporary.name)

    def patch(self, owner, name, **kwargs):
        patcher = mock.patch.object(owner, name, **kwargs)
        result = patcher.start()
        self.addCleanup(patcher.stop)
        return result
