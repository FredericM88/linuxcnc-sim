#!/usr/bin/env python3
"""Check copy isolation, input hash enforcement and protected output rejection."""
from pathlib import Path
import subprocess
import sys
import tempfile

compat = Path(__file__).resolve().parents[1]
vendor = compat.parents[1] / "third_party/stepper-ninja"
source = Path(sys.argv[1]).resolve()
files = ["hal-driver/stepgen-ninja.c", "hal-driver/hal_pin_macros.h"]
files += [line.split("  ", 1)[1] for line in (vendor / "SHA256SUMS").read_text().splitlines()]


def run(upstream, output, success):
    result = subprocess.run([sys.executable, str(compat / "prepare.py"),
                             "--source", str(upstream), "--output", str(output)],
                            capture_output=True, text=True)
    assert (result.returncode == 0) == success, result.stdout + result.stderr
    return result.stderr


with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    upstream, output = root / "upstream", root / "prepared"
    for name in files:
        target = upstream / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes((source / name).read_bytes())
    run(upstream, output, True)
    assert not any(p.is_symlink() for p in output.rglob("*"))
    for name in files:
        assert (upstream / name).read_bytes() == (source / name).read_bytes()
    # Repeated builds work; direct and symlinked paths into inputs are rejected.
    run(upstream, output, True)
    assert "overlaps" in run(upstream, upstream / "generated", False)
    (root / "alias").symlink_to(upstream, target_is_directory=True)
    assert "overlaps" in run(upstream, root / "alias", False)
    for name in ("hal-driver/stepgen-ninja.c", "firmware/inc/config.h"):
        path = upstream / name
        original = path.read_bytes()
        path.write_bytes(original + b"\n")
        assert "differs from pinned" in run(upstream, output, False)
        path.write_bytes(original)
    # Every pin-value dereference in the two adapted C files is removed.
    for name in ("stepgen-ninja.c", "modules/breakoutboard_hal_0.c"):
        assert "*d->" not in (output / name).read_text()
print("PASS: isolated preparation, pinned hashes and protected paths")
