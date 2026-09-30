"""Qualify opt-in skin dependency arguments at the real pipeline launcher boundary."""

import importlib.util
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
SPECIFICATION = importlib.util.spec_from_file_location("nwb_skin_dependency_launcher", ROOT / "pipeline" / "launch.py")
PIPELINE = importlib.util.module_from_spec(SPECIFICATION)
SPECIFICATION.loader.exec_module(PIPELINE)

FLAG = "--include-skin-dependencies"
REPO_ROOT = "--repo-root"
ASSET_ROOT = "--asset-root"
INPUT = "--input"
OUTPUT = "--output"
INPUT_LIST = "--input-list"


class SkinDependencyLauncherTests(unittest.TestCase):
    def setUp(self):
        self.root = Path("repo with spaces")
        self.roots = [self.root / "impl" / "assets", self.root / "project" / "assets"]
        self.tools = tuple(Path(name) for name in ("dependency", "builder", "gatherer"))
        self.workspace = self.root / "working files"
        self.arguments = [ASSET_ROOT, *(str(root) for root in self.roots), OUTPUT, str(self.root / "volume")]

    def commands(self, extra=()):
        options = PIPELINE.parse_arguments([*self.arguments, *extra])
        return PIPELINE.pipeline_commands(
            options, self.root, self.roots, self.root / "volume", self.root / "cache",
            self.root / "built", "tests", self.tools, self.workspace,
        )

    def test_opt_in_forwards_repository_and_every_asset_root_in_order(self):
        commands = self.commands([FLAG])
        self.assertEqual(
            [INPUT_LIST, str(self.workspace / "inputs.list"), OUTPUT, str(self.workspace / "dependencies.list"),
             FLAG, REPO_ROOT, str(self.root), ASSET_ROOT, str(self.roots[0]), ASSET_ROOT, str(self.roots[1])],
            commands[0][1],
        )
        self.assertEqual(self.commands()[1:], commands[1:])

    def test_selected_skin_keeps_the_input_manifest_boundary(self):
        selected = self.roots[1] / "skin.nwb"
        commands = self.commands([FLAG, INPUT, str(selected)])
        self.assertEqual(str(self.workspace / "inputs.list"), commands[0][1][1])
        self.assertEqual(str(self.workspace / "dependencies.list"), commands[1][1][1])
        self.assertNotIn(str(selected), commands[0][1])


if __name__ == "__main__":
    unittest.main()
