#!/usr/bin/env python3
"""Phase-3 IO diagnostics at 80x24, without scrolling or replacing the command line."""
import fcntl, os, pathlib, pty, select, socket, struct, subprocess, sys, termios, time
binary=str(pathlib.Path(sys.argv[1]).resolve())
master,slave=pty.openpty()
fcntl.ioctl(slave,termios.TIOCSWINSZ,struct.pack('HHHH',24,80,0,0))
original=termios.tcgetattr(slave)
p=subprocess.Popen([binary,'--bind','127.0.0.1','--port','0'],stdin=slave,stdout=slave,stderr=slave)
def wait_for(text):
    data=b'';deadline=time.monotonic()+4
    while time.monotonic()<deadline:
        if select.select([master],[],[],.05)[0]: data+=os.read(master,65536)
        if text in data:
            assert b'\n' not in data and b'Terminal too small' not in data,data
            assert b'Command >' in data and b'Samples:' in data and b'Virtual inputs:' in data,data
            return
        assert p.poll() is None,data
    raise AssertionError((text,data))
def command(text,expected):
    while select.select([master],[],[],0)[0]:os.read(master,65536)
    os.write(master,(text+'\n').encode());wait_for(expected)
try:
    wait_for(b'Virtual inputs:')
    command('input 22 on',b'Input 22 manual=ON effective=ON')
    command('input 127 on',b'Input 127 manual=ON effective=ON')
    command('input show',b'Manual [0..3] hex: 00400000 00000000 00000000 80000000')
    command('limits set X min 0 2 22',b'X-:ON')
    command('limits show',b'Z-Max: disabled')
    command('probe plane Z 0 28',b'Probe:ON')
    command('probe show',b'Probe ON: point in half-space Z <= 0 steps; input=28')
    command('help',b'Aliases:')
    command('help io',b'limits set X min')
    command('input clear',b'Manual overrides: 0')
    os.write(master,b'quit\n');p.wait(timeout=3)
    assert p.returncode==0 and termios.tcgetattr(slave)==original
    print('PASS: 80x24 fixed IO/sensor display, all show/help commands, manual OR sensors, terminal restoration')
finally:
    if p.poll() is None:p.kill();p.wait()
    os.close(master);os.close(slave)
