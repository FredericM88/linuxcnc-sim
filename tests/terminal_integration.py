#!/usr/bin/env python3
"""Real PTY: fixed screen, editing, resize, quit, terminal-generated SIGINT."""
import fcntl
import os
import pathlib
import pty
import re
import select
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import termios
import time
from udp_integration import exchange

binary = str(pathlib.Path(sys.argv[1]).resolve())
for ending in ('quit', 'ctrl-c', 'sigterm'):
    with tempfile.TemporaryDirectory() as directory:
        master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 24, 100, 0, 0))
        original = termios.tcgetattr(slave)
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reservation:
            reservation.bind(('127.0.0.1', 0))
            port = reservation.getsockname()[1]

        def setup():
            os.setsid()
            fcntl.ioctl(0, termios.TIOCSCTTY, 0)

        p = subprocess.Popen([binary, '--bind', '127.0.0.1', '--port', str(port),
                              '--steps-per-unit', '400,400,400,400'], stdin=slave, stdout=slave, stderr=slave,
                             preexec_fn=setup, cwd=directory)
        data = bytearray()

        def wait_for(value):
            deadline = time.monotonic() + 4
            while time.monotonic() < deadline:
                if value in data: return
                if select.select([master], [], [], .05)[0]:
                    data.extend(os.read(master, 65536))
                assert p.poll() is None, bytes(data)
            raise AssertionError((value, bytes(data)))

        def send(command, response):
            data.clear()
            os.write(master, command)
            wait_for(response)

        try:
            wait_for(b'Command >')
            assert b'\x1b[?1049h' in data and b'\x1b[2;1H' in data
            assert not termios.tcgetattr(slave)[3] & termios.ECHO
            assert b'\n' not in data, 'fixed rendering must not scroll'
            send(b'\n', b'Command >')
            send(b'unknown\n', b'unknown command')
            send(b'help\n', b'Aliases:')
            send(b'record stop\n', b'already stopped')
            send(b'record save empty.csv\n', b'no samples')
            send(b'record begin\n', b'Recording started')
            send(b'begin record\n', b'Already recording')
            # A half-entered command and escape key sequences do not affect UDP.
            os.write(master, b'record stox\x7fp\x1b[D\x1b[C')
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
                sock.settimeout(1)
                for i in range(200): exchange(sock, ('127.0.0.1', port), i & 255, 1)
            send(b'\n', b'Recording stopped')
            send(b'record save motion.csv\n', b'Saved 201 samples')
            send(b'record save motion.csv\n', b'File exists')
            fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 12, 40, 0, 0))
            data.clear()
            wait_for(b'Terminal too small')
            fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 24, 100, 0, 0))
            send(b'discard this\x15status\n', b'Samples: 201')
            assert b'\n' not in data, 'commands must not add scrolling output'
            if ending == 'quit': os.write(master, b'quit\n')
            elif ending == 'ctrl-c': os.write(master, b'\x03')
            else: p.send_signal(signal.SIGTERM)
            # Drain until the process has restored and exited.
            deadline = time.monotonic() + 3
            while p.poll() is None and time.monotonic() < deadline:
                if select.select([master], [], [], .05)[0]: data.extend(os.read(master, 65536))
            p.wait(timeout=1)
            while select.select([master], [], [], 0)[0]: data.extend(os.read(master, 65536))
            assert p.returncode == 0, bytes(data)
            assert termios.tcgetattr(slave) == original, ending
            assert b'\x1b[?1049l' in data and b'Stopped. Final statistics:' in data
            print(f'PASS: PTY fixed layout, editing, aliases, UDP during input, resize, {ending}, restored termios')
        finally:
            if p.poll() is None:
                p.kill()
                p.wait()
            os.close(master)
            os.close(slave)
