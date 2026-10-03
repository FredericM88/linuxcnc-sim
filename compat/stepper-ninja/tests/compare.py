#!/usr/bin/env python3
"""Compare full pin/default/function/feedback and raw UDP packet transcripts."""
import difflib
import subprocess
import sys

baseline, adapted = (subprocess.check_output([p], text=True) for p in sys.argv[1:])
if baseline != adapted:
    sys.stderr.writelines(difflib.unified_diff(baseline.splitlines(True), adapted.splitlines(True),
                                            fromfile="upstream API 0", tofile="adapted"))
    raise SystemExit(1)
print("PASS: identical pin contracts, feedback and raw UDP packet bytes")
