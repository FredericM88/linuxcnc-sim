#!/usr/bin/env python3
"""Generate/check the frozen G17 G2 R5, F300, 1-ms, 400-step/mm test path.

The committed integer sequence, not platform libm at test runtime, is authoritative.
This is constant-feed endpoint sampling, not a LinuxCNC planner/servo emulation.
"""
import argparse
import hashlib
import math
import pathlib


def generate():
    radius, feed, cycle, scale = 5.0, 300.0 / 60.0, 0.001, 400
    duration = math.pi * radius / feed
    intervals = math.ceil(duration / cycle)
    rows = ["CNC_CURVED_STEPS_V1", "# tick X_steps Y_steps Z_steps; start at tick 0"]
    minimum_rounding_margin = math.inf
    for tick in range(intervals + 1):
        theta = -min(feed * tick * cycle / radius, math.pi)
        xy = ((10 + radius * math.cos(theta)) * scale,
              (4 + radius * math.sin(theta)) * scale)
        # Explicit nearest integer, halves away from zero (no ties-to-even).
        steps = [math.floor(v + 0.5) if v >= 0 else math.ceil(v - 0.5) for v in xy]
        minimum_rounding_margin = min(minimum_rounding_margin,
                                      *(abs(v - (math.floor(v) + 0.5)) for v in xy))
        if tick == 0:
            steps = [6000, 1600]
        elif tick == intervals:
            steps = [2000, 1600]
        rows.append(f"{tick} {steps[0]} {steps[1]} -4800")
    assert minimum_rounding_margin > 1e-8, "quantization too close to a half-step"
    return ("\n".join(rows) + "\n").encode("ascii"), minimum_rounding_margin


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--write", type=pathlib.Path, help="write a NEW fixture; never overwrite")
    args = parser.parse_args()
    data, margin = generate()
    if args.write:
        with args.write.open("xb") as stream:
            stream.write(data)
    else:
        fixture = pathlib.Path(__file__).parent / "fixtures/curved-material/arc.steps"
        assert fixture.read_bytes() == data, "frozen step sequence differs from generator"
    print(f"arc.steps sha256={hashlib.sha256(data).hexdigest()} "
          f"rounding_margin_steps={margin:.12g}")


if __name__ == "__main__":
    main()
