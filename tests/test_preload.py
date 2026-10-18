#!/usr/bin/env python3
"""Native fixture tests of interception, fail-closed behavior and redaction."""
import os
import subprocess
import sys
from pathlib import Path


def main():
    directory = Path(sys.argv[1])
    base = dict(os.environ)
    base.pop("ZTE_MESH_SHIM_MODE", None)
    base.pop("ZTE_MESH_SHIM_ALLOW_TRACE", None)
    base["LD_PRELOAD"] = str(directory / "libshim-host.so")

    def run(program="caller", **settings):
        return subprocess.run([str(directory / program)], env={**base, **settings}, capture_output=True, text=True, timeout=5)

    result = run()
    assert result.returncode == 78 and "call=DBShmCliInit" in result.stderr, result
    assert "MOCK_INIT_CALLED" not in result.stdout, result
    result = run(ZTE_MESH_SHIM_MODE="trace")
    assert result.returncode == 78 and "unapproved-mode" in result.stderr, result
    result = run(ZTE_MESH_SHIM_MODE="unknown", ZTE_MESH_SHIM_ALLOW_TRACE="1")
    assert result.returncode == 78, result
    result = run(ZTE_MESH_SHIM_MODE="trace", ZTE_MESH_SHIM_ALLOW_TRACE="1")
    assert result.returncode == 0 and "ABI_PASSTHROUGH_OK" in result.stdout, result
    assert "status=-45" in result.stderr and "view=TestView" in result.stderr, result
    assert "return=DBShmCliInit returned=1" in result.stderr and "status=17" not in result.stderr, result
    assert "secret-credential" not in result.stderr and "11223344" not in result.stderr, result
    result = run(program="no-backend", ZTE_MESH_SHIM_MODE="trace", ZTE_MESH_SHIM_ALLOW_TRACE="1")
    assert result.returncode == 79 and "missing-real-symbol" in result.stderr, result
    print("PASS: 5 preload scenarios; original return/errno/buffer preserved; credentials redacted")


if __name__ == "__main__":
    main()
