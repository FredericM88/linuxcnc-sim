#!/usr/bin/env python3
"""Original UDP + continuous cutting + commands, optionally with live OpenGL."""
import pathlib
import re
import socket
import subprocess
import sys
import tempfile
import time
from udp_integration import exchange

binary = str(pathlib.Path(sys.argv[1]).resolve())
render = sys.argv[2:] == ["--render"]
with tempfile.TemporaryFile(mode="w+") as log:
    process = subprocess.Popen([binary, "--bind", "127.0.0.1", "--port", "0", "--no-stats",
                                "--steps-per-unit", "400,400,400,400", "--stock-size", "30,10,4",
                                "--stock-origin", "0,-5,-2", *(["--render"] if render else [])],
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
        command("tool flat-end 2 3")
        command("material on")
        wait_for("material on: queued")
        latencies = []
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.settimeout(2)
            start = time.monotonic()
            for i in range(1000):
                latencies.append(exchange(sock, target, i & 255, 4))
                time.sleep(max(0, start + (i+1)*.001-time.monotonic()))
        command("material off")
        command("material show")
        command("tool show")
        for invalid in ["tool flat-end nan 3", "tool flat-end 2 -1", "material on extra"]:
            command(invalid)
        command("quit")
        assert process.wait(timeout=30) == 0
        log.seek(0)
        text = log.read()
        final = text.split("Stopped. Final statistics:")[-1]
        assert "accepted: 1000" in final and "TX: 1000" in final and "invalid: 0" in final, text
        assert "Packet-ID gaps: 0" in final and "send errors: 0" in final, text
        assert "Material removal: OFF | queue 0" in final and "Sweeps processed: 1001" in final, text
        assert "Dirty mesh chunks: 0" in final and "Worker lag: 0.000" in final, text
        assert "X  4000 steps  10.0000 mm" in final and text.count("Error:") == 3, text
        # Independent capsule cross-section centre count; z = [0,2] gives 20 layers.
        xy = sum(((x+.5)*.1 - min(10, (x+.5)*.1))**2 + (-5+(y+.5)*.1)**2 <= 1+1e-10
                 for x in range(300) for y in range(100))
        removed = int(re.search(r"Voxels tested: \d+ \| removed: (\d+)", final)[1])
        assert removed == xy*20, (removed, xy*20, text)
        ordered = sorted(latencies)
        maximum = int(re.search(r"queue 0 / max (\d+)", final)[1])
        print(f"PASS: {'graphics' if render else 'headless'} material UDP/CLI: 1000 packets, "
              f"1001 sweeps, {removed} removed voxels, max queue {maximum}; "
              f"RTT median={ordered[500]*1000:.3f} p99={ordered[990]*1000:.3f} "
              f"max={max(latencies)*1000:.3f} ms (diagnostic, not realtime guarantee)")
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
