import tempfile
import unittest
from pathlib import Path

from pogls_dashboard.manifest import load_manifest, resolve_manifest
from pogls_dashboard.scanner import DashboardScanner


class DashboardManifestTests(unittest.TestCase):
    def test_manifest_defaults_and_yaml(self):
        with tempfile.TemporaryDirectory() as td:
            manifest_path = Path(td) / "dash.yaml"
            manifest_path.write_text(
                """
version: 1
repos:
  - name: A
    path: ./repo_a
    role: live
modules:
  temporal:
    tracked_paths:
      - x.h
links:
  - source: A:x.h
    target: A:y.h
    kind: manifest
exports:
  formats:
    - json
""",
                encoding="utf-8",
            )
            data = load_manifest(manifest_path)
            resolved = resolve_manifest(manifest_path.parent, data, [])
            self.assertEqual(resolved["repos"][0].name, "A")
            self.assertEqual(resolved["module_tracking"]["temporal"], ["x.h"])
            self.assertEqual(resolved["links"][0].source, "A:x.h")
            self.assertEqual(resolved["export_formats"], ["json"])

    def test_cross_repo_manifest_link_is_applied(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            repo_a = root / "repo_a"
            repo_b = root / "repo_b"
            repo_a.mkdir(); repo_b.mkdir()
            (repo_a / "a.h").write_text('#include "b.h"\n', encoding='utf-8')
            (repo_b / "b.h").write_text('typedef struct B { int x; } B;\n', encoding='utf-8')
            scanner = DashboardScanner(
                repos=[], module_tracking={"federation": ["a.h"]}, links=[]
            )
            # inject repo configs directly after import to keep test light
            from pogls_dashboard.models import RepoConfig, ManifestLink
            scanner = DashboardScanner(
                repos=[RepoConfig("A", str(repo_a), "live"), RepoConfig("B", str(repo_b), "durable")],
                module_tracking={"federation": ["a.h"]},
                links=[ManifestLink("A:a.h", "B:b.h", "cross_repo", "bridge")],
            )
            snap = scanner.scan()
            self.assertIn("A:a.h", snap["files"])
            self.assertIn("B:b.h", snap["files"])
            self.assertIn("cross_repo", snap["files"]["A:a.h"]["outgoing"])
            self.assertIn("B:b.h", snap["files"]["A:a.h"]["outgoing"]["cross_repo"])


if __name__ == '__main__':
    unittest.main()
