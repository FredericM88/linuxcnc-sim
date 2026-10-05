#!/usr/bin/env python3
"""Configuration contract through the real executable, including startup and runtime overrides."""
import pathlib
import re
import subprocess
import sys
import tempfile

binary = str(pathlib.Path(sys.argv[1]).resolve())
case = sys.argv[2]
root = pathlib.Path(__file__).resolve().parents[1]

with tempfile.TemporaryDirectory(prefix='cnc-config-') as temporary:
    directory = pathlib.Path(temporary)
    ini = directory / 'simulator.ini'
    io = directory / 'sensors.conf'
    io.write_text('input 22 on\n')

    def run(content=None, args=(), commands=None):
        if content is not None:
            ini.write_text(content)
        argv = [binary]
        if content is not None:
            argv += ['--config', str(ini)]
        argv += list(args)
        if commands is None:
            argv += ['--print-config']
        result = subprocess.run(argv, input=commands, cwd=directory,
                                text=True, capture_output=True, timeout=20)
        return result

    def good(content=None, args=(), commands=None):
        result = run(content, args, commands)
        assert result.returncode == 0, (result.args, result.stdout, result.stderr)
        if commands is None:
            assert 'Listening:' not in result.stdout and 'Stopped.' not in result.stdout
        return result.stdout

    def bad(content, expected, args=()):
        result = run(content, args)
        assert result.returncode == 1 and not result.stdout, result
        for text in [str(ini), *expected]:
            assert text in result.stderr, (text, result.stderr)

    def field(output, section, key):
        block = output.split(f'[{section}]\n', 1)[1].split('\n[', 1)[0]
        return re.search(r'^' + re.escape(key) + r' = (.*)$', block, re.M)[1]

    full = (root / 'examples/mill/simulator.ini').read_text().replace('ENABLED = true', 'ENABLED = false')
    full = full.replace('../phase3/virtual-io.conf', str(root / 'examples/phase3/virtual-io.conf'))
    if case == 'defaults':
        out = good()
        for section, key, value in [
            ('SIMULATOR', 'UNITS', 'mm'), ('SIMULATOR', 'STATS_INTERVAL_MS', '0'),
            ('SIMULATOR', 'Interactive terminal', 'true'), ('SIMULATOR', 'Verbose', 'false'),
            ('SIMULATOR', 'Display labels', 'mm,mm,mm,unit'),
            ('NETWORK', 'BIND_ADDRESS', '192.168.50.2'), ('NETWORK', 'PORT', '8888'),
            ('MATERIAL', 'BATCH_MS', '20'), ('MATERIAL', 'WORKERS', '1'),
            ('MATERIAL', 'ENABLED', 'false'), ('RENDER', 'ENABLED', 'false'),
            ('WORKPIECE', 'SIZE', '50,50,10'), ('WORKPIECE', 'POSITION', '0,0,0'),
            ('WORKPIECE', 'ORIGIN', '0,0,10'), ('TOOL', 'DIAMETER', '6'),
            ('TOOL', 'CUTTING_LENGTH', '20'), ('SPINDLE', 'MAX_RPM', '24000'),
            ('SPINDLE', 'ENCODER_COUNTS_PER_REV', '1024'), ('VIRTUAL_IO', 'CONFIG', '(none)')]:
            assert field(out, section, key) == value, out
        assert float(field(out, 'WORKPIECE', 'VOXEL_SIZE')) == .1
        assert 1 <= int(field(out, 'MESH', 'WORKERS')) <= 4
        for axis in 'XYZA':
            assert field(out, 'AXIS_' + axis, 'STEPS_PER_UNIT').startswith('1000 ')
    elif case == 'full':
        out = good(full)
        assert field(out, 'WORKPIECE', 'POSITION') == '15,10,-10'
        assert field(out, 'WORKPIECE', 'ORIGIN') == '15,10,10'
        assert field(out, 'WORKPIECE', 'Machine min (before legacy rotation)') == '0,0,-20'
        assert field(out, 'MESH', 'WORKERS') == '4'
        assert 'mm,mm,mm,deg' in out
    elif case == 'partial':
        out = good('[MATERIAL]\nBATCH_MS = 10\n')
        assert field(out, 'MATERIAL', 'BATCH_MS') == '10'
        assert field(out, 'WORKPIECE', 'SIZE') == '50,50,10'
        assert field(out, 'NETWORK', 'PORT') == '8888'
    elif case == 'precedence':
        assert field(good(), 'MATERIAL', 'BATCH_MS') == '20'
        text = '[MATERIAL]\nBATCH_MS = 10\n'
        assert field(good(text), 'MATERIAL', 'BATCH_MS') == '10'
        for args in [('--material-batch-ms', '50'), ('--material-batch-ms', '0')]:
            assert field(good(text, args), 'MATERIAL', 'BATCH_MS') == args[1]
        ini.write_text(text)
        result = run(args=('--material-batch-ms', '50', '--config', str(ini)))
        assert result.returncode == 0 and field(result.stdout, 'MATERIAL', 'BATCH_MS') == '50'
    elif case == 'legacy':
        out = good(args=('--bind', '127.0.0.1', '--port', '0', '--no-stats', '--verbose',
                         '--steps-per-unit', '-100,200,300,400', '--units', 'x,y,z,a',
                         '--stock-size', '10,20,3', '--stock-origin', '7,8,9',
                         '--voxel-size', '.5', '--stock-rotation', '0,0,45',
                         '--mesh-workers', '2', '--material-workers', '1', '--material-batch-ms', '50',
                         '--io-config', 'sensors.conf'))
        assert field(out, 'WORKPIECE', 'POSITION') == '7,8,9'
        assert field(out, 'WORKPIECE', 'ORIGIN') == '0,0,0'
        assert field(out, 'WORKPIECE', 'Legacy rotation degrees') == '0,0,45'
        assert field(out, 'AXIS_X', 'STEPS_PER_UNIT') == '-100 (steps/mm)'
        assert 'Display labels = x,y,z,a' in out and 'Interactive terminal = false' in out
        assert field(out, 'VIRTUAL_IO', 'CONFIG') == 'sensors.conf'
        assert 'STATS_INTERVAL_MS = 1000' in good(args=('--stats',))
        assert 'STATS_INTERVAL_MS = 500' in good(args=('--stats-ms', '500'))
    elif case == 'axes':
        text = ''.join(f'[AXIS_{a}]\nSTEPS_PER_UNIT = {n}\n' for a, n in zip('XYZA', (11,22,33,44)))
        for out, values in [(good(text), (11,22,33,44)),
                            (good(text, ('--steps-per-unit', '100,200,300,400')), (100,200,300,400))]:
            for a, n in zip('XYZA', values):
                assert field(out, 'AXIS_' + a, 'STEPS_PER_UNIT').startswith(str(n) + ' ')
    elif case == 'axis-units':
        out = good('[AXIS_A]\nTYPE = rotary\nSTEPS_PER_UNIT = 20\n')
        assert field(out, 'AXIS_A', 'STEPS_PER_UNIT') == '20 (steps/degree)'
        assert field(out, 'AXIS_X', 'STEPS_PER_UNIT') == '1000 (steps/mm)'
        out = good('[AXIS_A]\nTYPE = linear\n')
        assert field(out, 'AXIS_A', 'STEPS_PER_UNIT') == '1000 (steps/mm)'
        assert 'Display labels = mm,mm,mm,mm' in out
    elif case in ('origin-symbolic', 'origin-numeric', 'origin-mixed'):
        origin, expected = {'origin-symbolic': ('center,center,max', '15,10,10'),
                            'origin-numeric': ('2.5,3,4', '2.5,3,4'),
                            'origin-mixed': ('min,5,max', '0,5,10')}[case]
        # ORIGIN precedes SIZE in the file: section order is not an execution order.
        out = good(f'[WORKPIECE]\nORIGIN = {origin}\nSIZE = 30,20,10\nPOSITION = 15,10,-10\n')
        assert field(out, 'WORKPIECE', 'ORIGIN') == expected
    elif case == 'relative-io':
        nested = directory / 'nested'
        nested.mkdir()
        ini = nested / 'simulator.ini'
        out = good('[VIRTUAL_IO]\nCONFIG = ../sensors.conf\n')
        assert field(out, 'VIRTUAL_IO', 'CONFIG') == str(io)
        # CLI replaces the INI file, keeps cwd semantics, and repeated CLI files append.
        (directory / 'more.conf').write_text('input 26 on\n')
        out = good('[VIRTUAL_IO]\nCONFIG = missing.conf\n',
                   ('--io-config', 'sensors.conf', '--io-config', 'more.conf'))
        assert 'CONFIG = sensors.conf\nCONFIG = more.conf' in out
    elif case == 'unknown-section':
        for section in ('TYPO', 'TOOLSETTER', 'PROBE'):
            bad(f'[{section}]\n', [':1', f'[{section}]', 'unknown section'])
    elif case == 'unknown-key':
        bad('[MATERIAL]\nBATC_MS = 20\n', [':2', '[MATERIAL]', 'BATC_MS', 'unknown key'])
    elif case == 'duplicates':
        bad('[MATERIAL]\nBATCH_MS = 20\nBATCH_MS = 30\n', [':3', 'BATCH_MS', 'duplicate key'])
        bad('[MATERIAL]\n[MATERIAL]\n', [':2', 'duplicate section'])
    elif case == 'numeric':
        for value in ('abc', '20oops', '1.5', '-1', '4294967296'):
            bad(f'[MATERIAL]\nBATCH_MS = {value}\n', [':2', 'BATCH_MS'])
        for value in ('nan', 'inf', '1.0oops', '1e999'):
            bad(f'[TOOL]\nDIAMETER = {value}\n', [':2', 'DIAMETER'])
    elif case == 'enums':
        for section, key, value in [('AXIS_X', 'TYPE', 'spherical'), ('TOOL', 'TYPE', 'ball-end'),
                                    ('SIMULATOR', 'UNITS', 'inch'), ('RENDER', 'ENABLED', 'yes')]:
            bad(f'[{section}]\n{key} = {value}\n', [':2', f'[{section}]', key])
    elif case == 'vectors':
        assert field(good('[WORKPIECE]\nSIZE = 30 , 20 , 10\n'), 'WORKPIECE', 'SIZE') == '30,20,10'
        for key in ('SIZE', 'POSITION', 'ORIGIN'):
            for value in ('1,2', '1,2,3,4', '1,,3'):
                bad(f'[WORKPIECE]\n{key} = {value}\n', [':2', key])
    elif case == 'port':
        for value in ('-1', '65536', '1.5'):
            bad(f'[NETWORK]\nPORT = {value}\n', [':2', 'PORT'])
        for value in ('0', '65535'):
            assert field(good(f'[NETWORK]\nPORT = {value}\n'), 'NETWORK', 'PORT') == value
        bad('[NETWORK]\nBIND_ADDRESS = localhost\n', [':2', 'BIND_ADDRESS', 'IPv4'])
    elif case == 'scales':
        for axis in 'XYZA':
            for value in ('0', '-1', 'nan'):
                bad(f'[AXIS_{axis}]\nSTEPS_PER_UNIT = {value}\n', [':2', f'AXIS_{axis}', 'STEPS_PER_UNIT'])
    elif case == 'voxel':
        for value in ('0', '-.1', 'nan', '0.000000001'):
            bad(f'[WORKPIECE]\nVOXEL_SIZE = {value}\n', [':2', 'VOXEL_SIZE'])
    elif case == 'size':
        for value in ('0,20,10', '20,-1,10', 'nan,20,10', '1e20,20,10'):
            bad(f'[WORKPIECE]\nSIZE = {value}\n', [':2', 'SIZE'])
    elif case == 'batch':
        for value in ('1001', '-1'):
            bad(f'[MATERIAL]\nBATCH_MS = {value}\n', [':2', 'BATCH_MS'])
        for value in ('0', '1000'):
            assert field(good(f'[MATERIAL]\nBATCH_MS = {value}\n'), 'MATERIAL', 'BATCH_MS') == value
    elif case == 'material-workers':
        for value in ('0', '2', '-1'):
            bad(f'[MATERIAL]\nWORKERS = {value}\n', [':2', '[MATERIAL]', 'WORKERS'])
    elif case == 'mesh-workers':
        for value in ('33', '-1'):
            bad(f'[MESH]\nWORKERS = {value}\n', [':2', '[MESH]', 'WORKERS'])
        for value in ('1', '32'):
            assert field(good(f'[MESH]\nWORKERS = {value}\n'), 'MESH', 'WORKERS') == value
        assert 1 <= int(field(good('[MESH]\nWORKERS = 0\n'), 'MESH', 'WORKERS')) <= 4
    elif case == 'tool':
        for key in ('DIAMETER', 'CUTTING_LENGTH'):
            for value in ('0', '-1', '1e13'):
                bad(f'[TOOL]\n{key} = {value}\n', [':2', '[TOOL]', key])
    elif case == 'spindle':
        out = good('[SPINDLE]\nMAX_RPM = 18000.5\nENCODER_COUNTS_PER_REV = 4096\n')
        assert field(out, 'SPINDLE', 'MAX_RPM') == '18000.5'
        assert field(out, 'SPINDLE', 'ENCODER_COUNTS_PER_REV') == '4096'
        for value in ('0', '-1', 'nan', 'inf'):
            bad(f'[SPINDLE]\nMAX_RPM = {value}\n', [':2', '[SPINDLE]', 'MAX_RPM'])
        for value in ('0', '-1', '1.5', '4294967296'):
            bad(f'[SPINDLE]\nENCODER_COUNTS_PER_REV = {value}\n',
                [':2', '[SPINDLE]', 'ENCODER_COUNTS_PER_REV'])
    elif case == 'missing-io':
        bad('[VIRTUAL_IO]\nCONFIG = missing.conf\n', [':2', 'CONFIG', 'missing.conf'])
        bad('[VIRTUAL_IO]\nCONFIG = .\n', [':2', 'CONFIG', 'file'])
        io.write_text('invalid sensor command\n')
        bad('[VIRTUAL_IO]\nCONFIG = sensors.conf\n', [':2', 'CONFIG', str(io) + ':1'])
    elif case == 'diagnostics':
        bad('[WORKPIECE]\nSIZE = 30,20,10\nORIGIN = min,typo,max\n', [':3', '[WORKPIECE]', 'ORIGIN'])
        bad('[WORKPIECE]\nORIGIN = 0,0,11\n', [':2', 'ORIGIN', '[0, size]'])
        bad('[NETWORK\n', [':1', 'malformed section'])
        bad('PORT = 8888\n', [':1', 'PORT', 'unknown key'])
    elif case == 'stats':
        for value in ('1', '99', '3600001'):
            bad(f'[SIMULATOR]\nSTATS_INTERVAL_MS = {value}\n', [':2', 'STATS_INTERVAL_MS'])
        for value in ('100', '3600000'):
            assert 'Interactive terminal = false' in good(f'[SIMULATOR]\nSTATS_INTERVAL_MS = {value}\n')
    elif case == 'placement-overrides':
        out = good(full, ('--stock-origin', '7,8,9', '--stock-size', '1,2,3'))
        assert field(out, 'WORKPIECE', 'Machine min (before legacy rotation)') == '7,8,9'
        assert field(out, 'WORKPIECE', 'SIZE') == '1,2,3'
        # Symbols resolve once at the INI layer; changing size retains numerical origin.
        out = good(full, ('--stock-size', '60,40,20'))
        assert field(out, 'WORKPIECE', 'ORIGIN') == '15,10,10'
    elif case == 'no-config-runtime':
        out = good(args=('--bind', '127.0.0.1', '--port', '0', '--no-stats'),
                   commands='workpiece show\ntool show\nquit\n')
        final = out.split('Stopped. Final statistics:')[1]
        assert 'Material removal: OFF' in final and 'Material batch interval: 20 ms' in final
        assert 'Tool: flat-end diameter 6.000 cutting length 20.000 mm' in final
        assert 'X  0 steps  0.0000 mm' in final and 'A  0 steps  0.0000 unit' in final
        assert 'Size: X 50.000 Y 50.000 Z 10.000 mm' in out
    elif case == 'render':
        result = run(args=('--config', str(root / 'examples/mill/simulator.ini')))
        if sys.argv[3] == 'ON':
            assert result.returncode == 0 and field(result.stdout, 'RENDER', 'ENABLED') == 'true', result
            assert field(result.stdout, 'VIRTUAL_IO', 'CONFIG') == str(root / 'examples/phase3/virtual-io.conf')
        else:
            assert result.returncode == 1 and '--render unavailable' in result.stderr, result
    elif case == 'startup-runtime':
        content = '''[NETWORK]
BIND_ADDRESS = 127.0.0.1
PORT = 0
[WORKPIECE]
SIZE = 2,2,2
POSITION = 0,0,0
ORIGIN = center,center,min
VOXEL_SIZE = 0.1
[TOOL]
DIAMETER = 2
CUTTING_LENGTH = 1
[MATERIAL]
ENABLED = true
[VIRTUAL_IO]
CONFIG = sensors.conf
'''
        out = good(content, ('--no-stats',), 'workpiece show\ninput show\nquit\n')
        final = out.split('Stopped. Final statistics:')[1]
        assert 'Material removal: ON' in final
        assert 'Tool: flat-end diameter 2.000 cutting length 1.000 mm' in final
        xy = sum((-1+(x+.5)*.1)**2 + (-1+(y+.5)*.1)**2 <= 1+1e-9 for x in range(20) for y in range(20))
        assert f'removed: {xy*10}' in final, final
        assert 'Position (G53): X 0.000 Y 0.000 Z 0.000 mm' in out
        assert 'Origin offset from workpiece min: X 1.000 Y 1.000 Z 0.000 mm' in out
        assert 'Manual [0..3] hex: 00400000' in out
        out = good(content, ('--no-stats',),
                   'material off\ntool flat-end 1 2\nworkpiece position 5 6 7\ninput clear\nquit\n')
        final = out.split('Stopped. Final statistics:')[1]
        assert 'Material removal: OFF' in final and 'diameter 1.000 cutting length 2.000' in final
        assert ini.read_text() == content
    else:
        raise AssertionError('unknown case: ' + case)
print('PASS: configuration ' + case)
