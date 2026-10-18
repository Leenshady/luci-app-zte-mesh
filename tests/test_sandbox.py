#!/usr/bin/env python3
"""Launcher negative tests. Run inside the capability-free build container."""
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HELPER = Path(sys.argv.pop(1)).resolve()


class SandboxTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="zte-mesh-db.", dir="/tmp")
        self.root = Path(self.directory.name) / "root"
        self.root.mkdir()

    def tearDown(self):
        self.directory.cleanup()

    def run_helper(self, *arguments):
        return subprocess.run([str(HELPER), *map(str, arguments)], capture_output=True, text=True, timeout=5)

    def arguments(self, root=None, executable="/bin/db-engine-probe", library="/lib"):
        return [root or self.root, "/lib/ld-linux.so.3", "--library-path", library, executable]

    def rejected(self, result):
        self.assertEqual(result.returncode, 64, result.stderr)
        self.assertNotIn("private_ipc=1", result.stderr)
        self.assertEqual(result.stdout, "")

    def test_missing_arguments(self):
        self.rejected(self.run_helper())

    def test_wrong_root_prefix(self):
        self.rejected(self.run_helper(*self.arguments(root="/etc")))

    def test_no_symlink_escape(self):
        with tempfile.TemporaryDirectory(prefix="not-a-mesh-root.", dir="/tmp") as elsewhere:
            link = Path(self.directory.name) / "escape"
            link.symlink_to(elsewhere)
            self.rejected(self.run_helper(*self.arguments(root=link)))

    def test_cannot_start_multiapd(self):
        self.rejected(self.run_helper(*self.arguments(executable="/bin/multiapd")))

    def test_cannot_start_cspd(self):
        self.rejected(self.run_helper(*self.arguments(executable="/bin/cspd")))

    def test_cannot_replace_library_path(self):
        self.rejected(self.run_helper(*self.arguments(library="/host/lib")))

    def test_cannot_add_arguments(self):
        self.rejected(self.run_helper(*self.arguments(), "--extra"))

    def test_no_shell_evaluation(self):
        self.rejected(self.run_helper(*self.arguments(executable="/bin/db-engine-probe; reboot")))

    def test_bridge_unknown_option(self):
        self.rejected(self.run_helper(*self.arguments(), "--client"))

    def test_bridge_no_credential_arguments(self):
        self.rejected(self.run_helper(*self.arguments(), "--bridge-test", "--key=forbidden"))

    def test_bridge_regular_stdin_rejected(self):
        with tempfile.TemporaryFile() as stream:
            result = subprocess.run([str(HELPER), *map(str, self.arguments()), "--bridge-test"],
                                    stdin=stream, capture_output=True, text=True, timeout=5)
        self.rejected(result)

    def test_bridge_regular_stdout_rejected(self):
        with tempfile.TemporaryFile() as stream:
            result = subprocess.run([str(HELPER), *map(str, self.arguments()), "--bridge-test"],
                                    input="", stdout=stream, stderr=subprocess.PIPE, text=True, timeout=5)
        self.assertEqual(result.returncode, 64, result.stderr)
        self.assertIn("bridge-requires", result.stderr)

    def test_bridge_pipes_namespace_failure_is_fatal(self):
        result = subprocess.run([str(HELPER), *map(str, self.arguments()), "--bridge-test"],
                                input="", capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 80, result.stderr)
        self.assertIn("unshare-ipc-net-mount", result.stderr)
        self.assertNotIn("private_ipc=1", result.stderr)

    def test_rollback_pipes_namespace_failure_is_fatal(self):
        result = subprocess.run([str(HELPER), *map(str, self.arguments()), "--bridge-rollback-test"],
                                input="", capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 80, result.stderr)
        self.assertIn("unshare-ipc-net-mount", result.stderr)

    def test_namespace_denied_is_fatal(self):
        # Docker --cap-drop ALL guarantees no CAP_SYS_ADMIN. Correct arguments
        # must fail at namespace creation, not run the program on the host.
        result = self.run_helper(*self.arguments())
        self.assertEqual(result.returncode, 80, result.stderr)
        self.assertIn("error=unshare-ipc-net-mount", result.stderr)
        self.assertNotIn("private_ipc=1", result.stderr)
        self.assertEqual(result.stdout, "")


if __name__ == "__main__":
    unittest.main(verbosity=2)
