#!/usr/bin/env python3
"""Verify all vendored upstream files against the pinned SHA-256 manifest."""
import hashlib
import pathlib
import re

root = pathlib.Path(__file__).resolve().parents[1]
manifest = root / "third_party/stepper-ninja/SHA256SUMS"
count = 0
for line in manifest.read_text().splitlines():
    digest, relative = line.split("  ", 1)
    vendored = manifest.parent / relative
    assert hashlib.sha256(vendored.read_bytes()).hexdigest() == digest, relative
    count += 1
assert count == 10
print(f"PASS: {count} unchanged original files match pinned SHA-256 manifest")
