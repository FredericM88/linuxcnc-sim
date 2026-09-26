#!/usr/bin/env python3
"""Real process: manual/automatic inputs in same-cycle UDP responses and recorder."""
import csv
import pathlib
import re
import socket
import struct
import subprocess
import sys
import tempfile
import time
from udp_integration import packet, TABLE
binary = str(pathlib.Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory() as directory, tempfile.TemporaryFile(mode='w+') as log, socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as sock:
    root = pathlib.Path(directory)
    config = root/'io.conf'
    config.write_text('''# All numbers are steps, intentionally small for exact boundaries.
limits set X min -10 2 22
limits set X max 10 2 22
limits set Y min -10 2 26
limits set Y max 10 2 26
limits set Z min -30 2 27
limits set Z max 30 2 27
probe plane Z -20 28
''')
    p=subprocess.Popen([binary,'--bind','127.0.0.1','--port','0','--no-stats','--steps-per-unit','400,400,400,400','--io-config',str(config)],stdin=subprocess.PIPE,stdout=log,stderr=log,text=True,cwd=root)
    offset=0
    def wait(pattern):
        global offset
        deadline=time.monotonic()+4
        while time.monotonic()<deadline:
            log.seek(offset); text=log.read(); found=re.search(pattern,text)
            if found: offset+=found.end(); return found
            assert p.poll() is None,text
            time.sleep(.005)
        raise AssertionError((pattern,text))
    def command(line,pattern):
        p.stdin.write(line+'\n'); p.stdin.flush(); return wait(pattern)
    identifier=0
    position=[0,0,0,0]
    expected_rows=[(position.copy(),0)]
    recording=False
    def send(delta=(0,0,0,0),invalid=False):
        global identifier
        data=bytearray(packet(identifier))
        for i,d in enumerate(delta):
            word=0 if not d else (int(d>0)<<31)|(19494<<10)|(abs(d)-1)
            struct.pack_into('<I',data,4*i,word)
        data[-1]=sum(TABLE[b] for b in data[:-1])&255
        if invalid: data[-1]^=1
        sock.sendto(data,target)
        if invalid:
            sock.settimeout(.03)
            try: sock.recv(100); raise AssertionError('invalid request answered')
            except socket.timeout: pass
            sock.settimeout(1); return
        response,peer=sock.recvfrom(100)
        assert len(response)==61 and peer==target and response[59]==identifier
        assert response[-1]==sum(TABLE[b] for b in response[:-1])&255
        assert response[:24]==bytes(24) and response[36]==0 and response[57:59]==bytes(2)
        identifier=(identifier+1)&255
        for i,d in enumerate(delta): position[i]+=d
        if recording and any(delta): expected_rows.append((position.copy(),sum(1<<i for i,d in enumerate(delta) if d)))
        return struct.unpack_from('<4I',response,37)
    try:
        target=('127.0.0.1',int(wait(r'Listening: 127.0.0.1:(\d+)')[1])); sock.settimeout(1)
        command('record begin','Recording started'); recording=True
        for n in (0,31,32,63,64,95,96,127,22,26,27,28):
            command(f'input {n} on',f'Input {n} manual=ON effective=ON')
            words=[0]*4;words[n//32]=1<<(n%32)
            assert send()==tuple(words),(n,words)
            command(f'input {n} off',f'Input {n} manual=OFF effective=OFF')
            assert send()==(0,0,0,0)
        command('input 0 on','effective=ON');command('input 127 on','effective=ON')
        assert send()==(1,0,0,0x80000000)
        command('input show','Inputs \\[0..3\\] hex: 00000001 00000000 00000000 80000000')
        command('input clear','All manual overrides cleared')
        for _ in range(100): assert send()==(0,0,0,0)
        command('status',r'Samples: 1  Changed samples: 0')
        for line in ('input -1 on','input 128 off','input 2 maybe','input 0 on extra','limits set X min 0 -1 22','limits set X min 9223372036854775807 1 22','probe plane A 0 28'):
            command(line,'Error:')
            assert send()==(0,0,0,0)
        send((-10,0,-20,0),invalid=True)
        assert send()==(0,0,0,0)
        assert send((-9,0,0,0))[0]==0
        assert send((-1,0,0,0))[0]==1<<22
        command('input 22 off','manual=OFF effective=ON')
        assert send((1,0,0,0))[0]==1<<22
        assert send((1,0,0,0))[0]==0
        assert send((0,10,-20,0))[0]==((1<<26)|(1<<28))
        command('probe show','contact steps=')
        assert send((0,-1,1,0))[0]==1<<26
        assert send((0,-1,0,0))[0]==0
        assert send((-2,-18,-11,0))[0]==((1<<22)|(1<<26)|(1<<27)|(1<<28))
        command('limits show','Z-Max: OFF trigger=30 hyst=2 steps input=27')
        # Change mapping while active: remove stale old bit, keep other automatic sources.
        command('limits set X min -10 2 127','Sensor configuration updated')
        assert send()==((1<<26)|(1<<27)|(1<<28),0,0,1<<31)
        command('probe off','Sensor configuration updated')
        assert send()==((1<<26)|(1<<27),0,0,1<<31)
        command('input 26 on','manual=ON effective=ON')
        assert send((20,20,60,0))==((1<<22)|(1<<26)|(1<<27),0,0,0)
        command('input clear','All manual overrides cleared')
        assert send((0,0,0,1))[0]==((1<<22)|(1<<26)|(1<<27))
        command('record stop','Recording stopped'); recording=False
        send((-10,-10,-30,0))
        command('record save io.csv',r'Saved \d+ samples: io.csv')
        rows=list(csv.DictReader(line for line in (root/'io.csv').read_text().splitlines() if not line.startswith('#')))
        assert len(rows)==len(expected_rows),(len(rows),len(expected_rows))
        for i,(row,(steps,mask)) in enumerate(zip(rows,expected_rows)):
            assert int(row['sequence'])==i
            assert [int(row[k]) for k in ('x_steps','y_steps','z_steps','a_steps')]==steps
            assert int(row['changed_axes'])==mask
        command('quit','Stopped. Final statistics:');p.wait(timeout=3)
        assert p.returncode==0
        log.seek(0);output=log.read();assert 'Packet-ID gaps: 0' in output and 'send errors: 0' in output
        print('PASS: command -> 128-bit wire inputs, simultaneous sources, bounds, hysteresis, probe, same-packet position/inputs, invalid packets, recorder unchanged')
    finally:
        if p.poll() is None:p.kill();p.wait()
        p.stdin.close()
    config.write_text('limits set X min 9223372036854775807 1 22\n')
    result=subprocess.run([binary,'--bind','127.0.0.1','--port','0','--io-config',str(config)],capture_output=True,text=True,timeout=3)
    assert result.returncode!=0 and 'io.conf:1:' in result.stderr
