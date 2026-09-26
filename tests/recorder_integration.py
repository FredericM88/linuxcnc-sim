#!/usr/bin/env python3
"""Commands and real UDP packets, with independent wire and CSV checks."""
import csv
import pathlib
import re
import socket
import struct
import subprocess
import sys
import tempfile
import time
from udp_integration import packet, TABLE, exchange

BINARY = str(pathlib.Path(sys.argv[1]).resolve())


def read_csv(path):
    text = path.read_text()
    assert text.startswith('# cnc-sim motion record\n# steps_per_unit=400,400,400,400\n# axis_units=mm,mm,mm,unit\n')
    rows = list(csv.DictReader(line for line in text.splitlines() if not line.startswith('#')))
    values = [[int(row[key]) for key in ('sequence', 'time_us', 'x_steps', 'y_steps', 'z_steps', 'a_steps', 'changed_axes')] for row in rows]
    assert [r[0] for r in values] == list(range(len(values)))
    assert [r[1] for r in values] == sorted(r[1] for r in values)
    return values


with tempfile.TemporaryDirectory() as directory, tempfile.TemporaryFile(mode='w+') as log, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
    work = pathlib.Path(directory)
    p = subprocess.Popen([BINARY, '--bind', '127.0.0.1', '--port', '0', '--no-stats',
                          '--steps-per-unit', '400,400,400,400'], stdin=subprocess.PIPE, stdout=log, stderr=log, cwd=work)
    offset = 0

    def wait_for(pattern):
        global offset
        end = time.monotonic() + 4
        while time.monotonic() < end:
            log.seek(offset)
            text = log.read()
            match = re.search(pattern, text)
            if match:
                offset += match.end()
                return match
            assert p.poll() is None, text
            time.sleep(0.005)
        raise AssertionError(f'timeout waiting for {pattern}: {text}')

    def command(line, result):
        p.stdin.write((line + '\n').encode())
        p.stdin.flush()
        return wait_for(result)

    identifier = 0

    def send(delta=(0, 0, 0, 0), valid=True):
        global identifier
        data = bytearray(packet(identifier))
        for axis, step in enumerate(delta):
            word = 0 if not step else (int(step > 0) << 31) | (19494 << 10) | (abs(step) - 1)
            struct.pack_into('<I', data, axis * 4, word)
        data[-1] = sum(TABLE[x] for x in data[:-1]) & 255
        if not valid:
            data[-1] ^= 1
        sock.sendto(data, target)
        if valid:
            reply, peer = sock.recvfrom(100)
            assert len(reply) == 61 and reply[59] == identifier and peer == target
            assert reply[-1] == sum(TABLE[x] for x in reply[:-1]) & 255
            identifier = (identifier + 1) & 255
        else:
            sock.settimeout(0.03)
            try:
                sock.recvfrom(100)
                raise AssertionError('invalid packet answered')
            except socket.timeout:
                pass
            sock.settimeout(1)

    try:
        target = ('127.0.0.1', int(wait_for(r'Listening: 127.0.0.1:(\d+)')[1]))
        sock.settimeout(1)
        command('record stop', 'already stopped')
        command('record save empty.csv', 'no samples')
        command('nonsense', 'unknown command')
        command('help', 'Aliases:')
        command('\nstatus', 'Status updated')  # Empty line is a no-op, then a synchronization command.
        send((100, 200, 300, 400))
        command('begin record', 'Recording started')
        command('record begin', 'Already recording')
        for _ in range(100): send()
        command('status', r'Samples: 1  Changed samples: 0')
        send((9, 9, 0, 0), valid=False)
        # Partially typed command must never stop UDP.
        p.stdin.write(b'record ')
        p.stdin.flush()
        send((3, 0, 0, 0))
        send((-2, 5, 0, 0))
        for _ in range(100): send()
        command('stop', 'Recording stopped')
        send((20, 10, 0, 0))
        command('save record first recording.csv', r'Saved 3 samples: first recording.csv')
        rows = read_csv(work / 'first recording.csv')
        assert rows[0] == [0, 0, 100, 200, 300, 400, 0], rows
        assert rows[1][2:] == [103, 200, 300, 400, 1], rows
        assert rows[2][2:] == [101, 205, 300, 400, 3], rows
        command('record save first recording.csv', 'File exists')
        assert read_csv(work / 'first recording.csv') == rows
        (work / 'link.csv').symlink_to(work / 'first recording.csv')
        command('record save link.csv', 'File exists')
        command('record save missing/fail.csv', 'No such file')
        command('record save ' + 'x' * 300, 'File name too long')
        command('record save ' + 'x' * 9000, 'unknown command')
        assert not (work / 'empty.csv').exists()
        command('clear record', 'Recording cleared')
        command('record begin', 'Recording started')
        # Cross several immutable blocks; snapshot while still recording.
        for _ in range(2200): send((1, -1, 0, 0))
        command('record save active.csv', 'Saving snapshot')
        for _ in range(1000): send((1, -1, 0, 0))
        wait_for('Saved 2201 samples: active.csv')
        active = read_csv(work / 'active.csv')
        assert len(active) == 2201 and active[-1][2:4] == [2321, -1985], active[-1]
        command('stop record', 'Recording stopped')
        command('record save complete.csv', 'Saved 3201 samples: complete.csv')
        complete = read_csv(work / 'complete.csv')
        assert complete[-1][2:] == [3321, -2985, 300, 400, 3]
        assert all(row[6] == 3 for row in complete[1:])
        command('record begin', 'Recording started')
        command('record clear', 'Recording cleared')
        send((1, 0, 0, 0))
        command('status', 'Samples: 0  Changed samples: 0')
        command('quit', 'Stopped. Final statistics:')
        p.wait(timeout=3)
        assert p.returncode == 0
        log.seek(0)
        output = log.read()
        assert 'Packet-ID gaps: 0' in output and 'send errors: 0' in output
        print('PASS: commands/aliases, start, idle, invalid, X/XY, stop, clear, repeat, active snapshot, exact CSV, filename errors')
    finally:
        if p.poll() is None:
            p.kill()
            p.wait()
        p.stdin.close()

# Deliberately stop consuming stdout. A blocked terminal write must not block UDP.
p = subprocess.Popen([BINARY, '--bind', '127.0.0.1', '--port', '0', '--no-stats'],
                     stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
try:
    while True:
        line = p.stdout.readline()
        match = re.search(rb'Listening: 127.0.0.1:(\d+)', line)
        if match: break
        assert line
    target = ('127.0.0.1', int(match[1]))
    p.stdin.write(b'help\n' * 200)
    p.stdin.flush()
    time.sleep(0.1)
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.settimeout(1)
        start = time.monotonic()
        worst = 0
        for i in range(1000):
            worst = max(worst, exchange(sock, target, i & 255, 1))
            time.sleep(max(0, start + (i + 1) * .001 - time.monotonic()))
    print(f'PASS: blocked console output, 1000 packets nominal 1 kHz; worst RTT {worst * 1000:.3f} ms')
finally:
    p.stdout.close()  # Unblocks write with EPIPE; SIGPIPE is deliberately ignored.
    p.terminate()
    try:
        p.wait(timeout=3)
    except subprocess.TimeoutExpired:
        p.kill()
        p.wait()
    p.stdin.close()
assert p.returncode == 0, p.returncode
