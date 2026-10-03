#!/usr/bin/env python3
"""Verify pinned upstream inputs and patch only a private build-directory copy."""
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
VENDOR = HERE.parent.parent / "third_party/stepper-ninja"
EXTRA_INPUTS = {
    "hal-driver/stepgen-ninja.c": "7639e72d6a89ce3b89fc3aceb4db319dbc48651246ab187d99ff5b4e579289a1",
    "hal-driver/hal_pin_macros.h": "599fbc2f962cc740372c54a428d0f310693f01ea3156fff2fb8a55b75f4fe530",
}


def prepare(source, output):
    source, output = source.resolve(), output.resolve()
    # Never apply a patch to upstream, the vendor snapshot, or our own sources.
    for protected in (source, VENDOR.resolve(), HERE):
        if output == protected or protected in output.parents or output in protected.parents:
            raise ValueError(f"Output overlaps protected sources: {output}")
    expected = dict(EXTRA_INPUTS)
    for line in (VENDOR / "SHA256SUMS").read_text().splitlines():
        digest, relative = line.split("  ", 1)
        expected[relative] = digest
    inputs = {}
    for relative, digest in expected.items():
        data = (source / relative).read_bytes()
        if hashlib.sha256(data).hexdigest() != digest:
            raise ValueError(f"Upstream file differs from pinned eb7e5dfa revision: {relative}")
        inputs[relative] = data

    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="stepper-ninja-", dir=output.parent) as temporary:
        staged = Path(temporary)
        # Flatten the upstream header symlinks into real, independently owned files.
        for relative, data in inputs.items():
            name = relative.removeprefix("hal-driver/") if relative.startswith("hal-driver/") else Path(relative).name
            dest = staged / name
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(data)
        shutil.copyfile(HERE / "hal_compat.h", staged / "hal_compat.h")
        subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-i", str(HERE / "hal-api.patch")],
                       cwd=staged, check=True)
        # Refuse symlinks in an existing generated tree as well.
        if output.exists() and any(p.is_symlink() for p in output.rglob("*")):
            raise ValueError(f"Generated output contains a symlink: {output}")
        shutil.copytree(staged, output, dirs_exist_ok=True)
    print(f"Prepared pinned Board-0 UDP driver in {output}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True, help="separate upstream checkout root")
    parser.add_argument("--output", type=Path, required=True, help="private build-directory destination")
    args = parser.parse_args()
    try:
        prepare(args.source, args.output)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"HAL source preparation failed: {error}\n")
