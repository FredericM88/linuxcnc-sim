#!/usr/bin/env python3
"""Black-box test of the executable, using only original Stepper-Ninja datagrams."""
import pathlib
import re
import socket
import struct
import subprocess
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
TABLE = [int(x, 16) for x in re.findall(r"0x([0-9a-f]{2})", (
    ROOT / "third_party/stepper-ninja/firmware/modules/inc/jump_table.h").read_text())]
assert len(TABLE) == 256


def packet(identifier, delta=0, timing=235, outputs=0, pwm_duty=0,
           pwm_frequency=0, enc_control=0):
    word = 0 if delta == 0 else ((int(delta > 0) << 31) | (19494 << 10) | (abs(delta) - 1))
    body = struct.pack("<4I2IIIHBB", word, 0, 0, 0, outputs, 0,
                       pwm_duty, pwm_frequency, timing, enc_control, identifier)
    assert len(body) == 36
    return body + bytes([sum(TABLE[x] for x in body) & 255])


def exchange(sock, target, identifier, delta=0):
    start = time.monotonic()
    sock.sendto(packet(identifier, delta), target)
    reply, peer = sock.recvfrom(65535)
    assert peer == target, (peer, target)
    assert len(reply) == 61, len(reply)
    assert reply[-1] == (sum(TABLE[x] for x in reply[:-1]) & 255)
    assert reply[59] == identifier
    assert reply[:24] == bytes(24)  # Stationary independent encoders.
    assert reply[36:53] == bytes(17)  # Index flags and all inputs.
    assert reply[57:59] == bytes(2)  # No software ring.
    return time.monotonic() - start


def spindle_exchange(sock, target, identifier, outputs, duty=20000, frequency=10000):
    sock.sendto(packet(identifier, outputs=outputs, pwm_duty=duty,
                       pwm_frequency=frequency), target)
    reply, peer = sock.recvfrom(65535)
    assert peer == target and len(reply) == 61
    assert reply[-1] == (sum(TABLE[x] for x in reply[:-1]) & 255)
    assert reply[59] == identifier
    counter = struct.unpack_from("<i", reply)[0]
    assert struct.unpack_from("<i", reply, 12)[0] == 0  # Firmware-compatible velocity field.
    return counter


def main():
    binary = str(pathlib.Path(sys.argv[1]).resolve())
    for args in (["--port", "65536"], ["--steps-per-unit", "0,1,1,1"],
                 ["--steps-per-unit", "nan,1,1,1"], ["--steps-per-unit", "1,2"],
                 ["--stats-ms", "0"], ["--bind", "invalid"], ["--port"], ["--unknown"]):
        result = subprocess.run([binary, *args], capture_output=True, timeout=3)
        assert result.returncode != 0, args
    assert subprocess.run([binary, "--help"], capture_output=True, timeout=3).returncode == 0
    with tempfile.TemporaryFile(mode="w+") as log, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        process = subprocess.Popen([binary, "--bind", "127.0.0.1", "--port", "0",
                                    "--steps-per-unit", "400,400,400,400", "--no-stats"], stdout=log, stderr=log)
        try:
            deadline = time.monotonic() + 3
            match = None
            while time.monotonic() < deadline:
                log.seek(0)
                match = re.search(r"Listening: 127.0.0.1:(\d+)", log.read())
                if match:
                    break
                assert process.poll() is None, "simulator exited before readiness"
                time.sleep(0.01)
            assert match, "simulator did not become ready"
            target = ("127.0.0.1", int(match[1]))
            sock.bind(("127.0.0.1", 0))
            sock.settimeout(1)
            for identifier, delta in [(254, 10), (255, 20), (0, -5), (1, 0), (3, 4), (3, 4), (1, 2)]:
                exchange(sock, target, identifier, delta)
            corrupt = bytearray(packet(2, 100))
            corrupt[0] ^= 1
            invalid = [packet(2)[:-1], packet(2) + b"x", packet(2) + bytes(1000),
                       b"", bytes(corrupt), packet(2, 100, timing=299)]
            sock.settimeout(0.04)
            for data in invalid:
                sock.sendto(data, target)
                try:
                    sock.recvfrom(65535)
                except socket.timeout:
                    pass
                else:
                    raise AssertionError("invalid datagram received a response")
            sock.settimeout(1)
            exchange(sock, target, 2)
            # A different real source port must receive its own answer too.
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as other:
                other.bind(("127.0.0.1", 0))
                other.settimeout(1)
                exchange(other, target, 3)
            start = time.monotonic()
            worst = 0.0
            for i in range(1000):
                worst = max(worst, exchange(sock, target, (4 + i) & 255, 4))
                time.sleep(max(0, start + (i + 1) * 0.001 - time.monotonic()))
            spindle_id = (4 + 1000) & 255
            spindle_exchange(sock, target, spindle_id, outputs=0x1)  # Enable, forward, 100% PWM.
            time.sleep(0.05)
            forward_count = spindle_exchange(sock, target, (spindle_id + 1) & 255,
                                              outputs=0x3)  # Reverse applies after response.
            assert forward_count > 0, forward_count
            time.sleep(0.02)
            reverse_count = spindle_exchange(sock, target, (spindle_id + 2) & 255,
                                              outputs=0)  # Disable applies after response.
            assert reverse_count < forward_count, (forward_count, reverse_count)
            print(f"1000 packets at nominal 1 kHz, worst observed roundtrip {worst * 1000:.3f} ms")
        finally:
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        log.seek(0)
        output = log.read()
        assert process.returncode == 0, output
        assert "X  4035 steps  10.0875 mm" in output, output
        assert "Y  0 steps" in output and "Z  0 steps" in output and "A  0 steps" in output, output
        assert "Packets RX: 1018  accepted: 1012  invalid: 6  TX: 1012  send errors: 0" in output, output
        assert "Length errors: 4  checksum errors: 1  timing errors: 1" in output, output
        assert "Packet-ID gaps: 4" in output, output
        assert "Spindle: disabled" in output, output
        print("PASS: replies, spindle forward/reverse/disable feedback, peer ports, invalid datagrams, "
              "ID wrap/gaps/duplicates, positions, CLI, shutdown")


if __name__ == "__main__":
    main()
