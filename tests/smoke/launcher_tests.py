#!/usr/bin/env python3
import os
from pathlib import Path
import tempfile
import unittest

import launcher


class LauncherPlatformTests(unittest.TestCase):
    def test_host_platform_names_match_cmake_output_names(self):
        self.assertEqual("windows", launcher.host_platform_name("Windows"))
        self.assertEqual("linux", launcher.host_platform_name("Linux"))
        self.assertEqual("darwin", launcher.host_platform_name("Darwin"))

    def test_default_build_dirs_follow_platform_domain_and_arch(self):
        root = Path(os.sep) / "repo"
        self.assertEqual(
            root / "__cmake" / "build" / "windows-clang-engine-x64",
            launcher.default_build_dir(root, "windows", "engine", "x64"),
        )
        self.assertEqual(
            root / "__cmake" / "build" / "linux-clang-x64",
            launcher.default_build_dir(root, "linux", "full", "x64"),
        )

    def test_output_root_matches_engine_and_domain_layouts(self):
        root = Path(os.sep) / "repo"
        self.assertEqual(
            root / "__exec" / "windows" / "x64",
            launcher.output_root(root, "windows", "x64", "engine"),
        )
        self.assertEqual(
            root / "__exec" / "linux" / "x64" / "full",
            launcher.output_root(root, "linux", "x64", "full"),
        )

    def test_infer_output_domain_from_known_preset_names(self):
        build_root = Path(os.sep) / "repo" / "__cmake" / "build"
        self.assertEqual("engine", launcher.infer_output_domain(build_root / "linux-clang-engine-x64", "linux", "x64"))
        self.assertEqual("full", launcher.infer_output_domain(build_root / "linux-clang-x64", "linux", "x64"))
        self.assertEqual("testbed", launcher.infer_output_domain(build_root / "windows-clang-testbed-x64", "windows", "x64"))

    def test_cmake_cache_domain_overrides_directory_name(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            build_dir = Path(temp_dir)
            (build_dir / "CMakeCache.txt").write_text("NWB_OUTPUT_DOMAIN:UNINITIALIZED=full\n", encoding="utf-8")
            self.assertEqual("full", launcher.infer_output_domain(build_dir, "linux", "x64"))


if __name__ == "__main__":
    unittest.main()
