#!/usr/bin/env python3
"""Opt-in desktop test: rendering, original UDP, recorder and terminal together."""
import csv
import pathlib
import re
import socket
import subprocess
import sys
import tempfile
import time
from udp_integration import exchange


def main():
    binary = str(pathlib.Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory() as directory, tempfile.TemporaryFile(mode="w+") as log:
        output = pathlib.Path(directory) / "motion.csv"
        process = subprocess.Popen([binary, "--render", "--bind", "127.0.0.1", "--port", "0",
                                    "--steps-per-unit", "400,400,400,400", "--no-stats"],
                                   stdin=subprocess.PIPE, stdout=log, stderr=log, text=True)

        def wait_for(pattern, seconds=15):
            deadline = time.monotonic() + seconds
            while time.monotonic() < deadline:
                log.seek(0)
                text = log.read()
                match = re.search(pattern, text)
                if match:
                    return match
                assert process.poll() is None, text
                time.sleep(.01)
            raise AssertionError((pattern, text))

        def command(text):
            process.stdin.write(text + "\n")
            process.stdin.flush()

        try:
            target = ("127.0.0.1", int(wait_for(r"Listening: 127.0.0.1:(\d+)")[1]))
            command("record begin")
            wait_for("Recording started")
            latencies = []
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
                sock.settimeout(1)
                start = time.monotonic()
                for i in range(1000):
                    if i == 100:
                        command("workpiece size 100 60 20")
                    if i == 300:
                        command("workpiece origin center center max")
                    if i == 500:
                        command("workpiece position 10 20 30")
                    if i == 700:
                        command("workpiece voxel .2")
                    if i == 900:
                        command("workpiece reset")
                    latencies.append(exchange(sock, target, i & 255, 4))
                    time.sleep(max(0, start + (i+1)*.001 - time.monotonic()))
            wait_for(r"Workpiece \(revision 6\)")
            command("record stop")
            wait_for("Recording stopped")
            command(f"record save {output}")
            wait_for("Saved 1001 samples")
            command("status")
            wait_for(r"X  4000 steps  10.0000 mm")
            command("quit")
            assert process.wait(timeout=5) == 0
            log.seek(0)
            text = log.read()
            assert "accepted: 1000" in text and "TX: 1000" in text, text
            assert "Packet-ID gaps: 0" in text and "invalid: 0" in text, text
            with output.open() as stream:
                rows = list(csv.DictReader(line for line in stream if not line.startswith("#")))
            assert len(rows) == 1001 and rows[-1]["x_steps"] == "4000"
            assert all(int(row["x_steps"]) == i*4 for i, row in enumerate(rows))
            ordered = sorted(latencies)
            print(f"PASS: 1000 original UDP exchanges during GL remeshing + recorder; "
                  f"RTT median={ordered[500]*1000:.3f} ms p99={ordered[990]*1000:.3f} ms "
                  f"max={max(ordered)*1000:.3f} ms (diagnostic, not hard realtime)")
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()


if __name__ == "__main__":
    main()
