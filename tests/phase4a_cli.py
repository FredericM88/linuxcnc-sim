#!/usr/bin/env python3
"""CLI/configuration and graphics startup errors work without a display."""
import os
import subprocess
import sys

binary = sys.argv[1]
base = [binary, "--bind", "127.0.0.1", "--port", "0", "--no-stats"]
for args in (["--voxel-size", "0"], ["--voxel-size", "nan"], ["--voxel-size", "-1"],
             ["--stock-size", "1,2"], ["--stock-size", "1,2,-3"], ["--stock-size", "1,2,3,4"],
             ["--stock-origin", "1,inf,3"], ["--stock-rotation", "x,0,0"],
             ["--render", "--units", "inch,mm,mm,unit"]):
    result = subprocess.run(base + args, capture_output=True, text=True, timeout=3)
    assert result.returncode != 0 and "cnc-sim:" in result.stderr, (args, result)
env = os.environ.copy()
for key in ("DISPLAY", "WAYLAND_DISPLAY", "XDG_RUNTIME_DIR"):
    env.pop(key, None)
result = subprocess.run(base + ["--render"], capture_output=True, text=True, env=env, timeout=5)
assert result.returncode != 0, result
assert "GLFW initialization failed" in result.stderr or "--render unavailable" in result.stderr, result.stderr
result = subprocess.run(base, input="quit\n", capture_output=True, text=True, env=env, timeout=5)
assert result.returncode == 0 and "Stopped." in result.stdout, result
print("PASS: scene arguments, explicit renderer failure, display-free default startup")
