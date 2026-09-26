#!/usr/bin/env python3
"""Verify pinned source hashes and, if available, the untouched upstream copies."""
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
    original = root / "stepper-ninja" / relative
    if original.exists():
        assert vendored.read_bytes() == original.read_bytes(), relative
    count += 1
assert count == 9
print(f"PASS: {count} unchanged original files match pinned SHA-256 manifest")
