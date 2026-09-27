#!/usr/bin/env python3
"""Runtime console transactions, legacy startup, and complete 80x24 output."""
import fcntl
import os
import pathlib
import pty
import re
import select
import struct
import subprocess
import sys
import termios
import time

binary = str(pathlib.Path(sys.argv[1]).resolve())
base = [binary, '--bind', '127.0.0.1', '--port', '0']
commands = [
    'workpiece size 100 60 20',
    'workpiece position 50 30 0',
    'workpiece origin 10 center max',
    'workpiece voxel .2',
    'workpiece show',
    'workpiece size 1 1 1',
    'workpiece origin 1 foo 2',
    'workpiece origin 1 2 nan',
    'workpiece position 1 2 inf',
    'workpiece voxel .000001',
    'workpiece show',
    'workpiece origin 10.125 15.25 19.5',
    'workpiece show',
    'workpiece reset',
    'workpiece show',
    'help', 'quit',
]
result = subprocess.run(base + ['--no-stats'], input='\n'.join(commands)+'\n',
                        capture_output=True, text=True, timeout=10)
assert result.returncode == 0, result
text = result.stdout
assert text.count('Error:') == 5, text
blocks = re.findall(r'Workpiece \(revision \d+\).*?Chunk size: 32 x 32 x 32', text, re.S)
assert len(blocks) == 10, text
assert blocks[3] == blocks[4] == blocks[5], text
assert 'Position (G53): X 50.000 Y 30.000 Z 0.000 mm' in blocks[5]
assert 'Origin offset from workpiece min: X 10.000 Y 30.000 Z 20.000 mm' in blocks[5]
for line in ('X 40.000 .. 140.000 mm', 'Y 0.000 .. 60.000 mm', 'Z -20.000 .. 0.000 mm'):
    assert line in blocks[5], blocks[5]
assert 'Voxel size: 0.2 mm' in blocks[5]
assert 'X 39.875 .. 139.875 mm' in blocks[6]
assert blocks[8] == blocks[9]
assert 'workpiece origin X Y Z' in text
assert 'X  0 steps  0.0000 mm' in text
# Old CLI placement remains the minimum-corner translation, including headless.
result = subprocess.run(base + ['--no-stats', '--stock-size', '10,20,30',
                               '--stock-origin', '7,8,9', '--stock-rotation', '0,0,45'],
                        input='workpiece show\nworkpiece origin min min max\nquit\n',
                        capture_output=True, text=True, timeout=5)
assert result.returncode == 0, result
assert 'Legacy --stock-rotation active' in result.stdout
assert 'Legacy rotation cleared: axis-aligned fresh raw stock.' in result.stdout
assert 'X 7.000 .. 17.000 mm' in result.stdout
assert 'Position (G53): X 7.000 Y 8.000 Z 9.000 mm' in result.stdout

master, slave = pty.openpty()
fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 24, 80, 0, 0))
original = termios.tcgetattr(slave)
process = subprocess.Popen(base, stdin=slave, stdout=slave, stderr=slave)
def wait_for(expected):
    data = b''
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        if select.select([master], [], [], .05)[0]:
            data += os.read(master, 65536)
        if expected in data:
            assert b'Terminal too small' not in data and b'\n' not in data, data
            assert b'Command >' in data, data
            return data
        assert process.poll() is None, data
    raise AssertionError((expected, data))
try:
    wait_for(b'Command >')
    os.write(master, b'workpiece show\n')
    data = wait_for(b'Chunk size: 32 x 32 x 32')
    for label in (b'Size:', b'Position (G53):', b'Origin offset', b'Machine bounds:', b'Voxel size:'):
        assert label in data, data
    os.write(master, b'quit\n')
    assert process.wait(timeout=3) == 0
    assert termios.tcgetattr(slave) == original
finally:
    if process.poll() is None:
        process.kill()
        process.wait()
    os.close(master)
    os.close(slave)
print('PASS: workpiece CLI, transactional errors, legacy placement, 80x24 output')
