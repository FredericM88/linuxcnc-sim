#!/usr/bin/env python3
"""Consistency checks across the supplied LinuxCNC INI/HAL and sensor profile."""
import configparser
import pathlib
root=pathlib.Path(__file__).resolve().parents[1]
folder=root/'examples/phase3'
ini=configparser.ConfigParser(strict=False)
ini.read(folder/'phase3.ini')
ini210=configparser.ConfigParser(strict=False)
ini210.read(folder/'phase3-2.10.ini')
assert ini['EMCMOT']['SERVO_PERIOD']=='1000000'
assert ini['HAL']['HALFILE']=='phase3.hal'
assert ini210['HAL']['HALFILE']=='phase3-2.10.hal'
assert ini['TRAJ']['SPINDLES']=='1'
assert ini['SPINDLE_0']['MAX_FORWARD_VELOCITY']=='24000'
assert ini['SPINDLE_0']['MAX_REVERSE_VELOCITY']=='24000'
for section in ini.sections():
    if section not in ('EMC','HAL'):
        assert dict(ini[section])==dict(ini210[section]), f'2.10 variant drifted in [{section}]'
assert (folder/ini['EMCIO']['TOOL_TABLE']).exists()
assert (folder/ini['RS274NGC']['PARAMETER_FILE']).name=='phase3.var'
commands=[line.split('#')[0].strip().split() for line in (folder/'virtual-io.conf').read_text().splitlines()]
limits={(a[2],a[3]):a for a in commands if a[:2]==['limits','set']}
assert len(limits)==6 and ['probe','off'] in commands
assert not any(a[:2]==['probe','plane'] for a in commands), 'probe must be enabled after the Z home search'
hal29=(folder/'phase3.hal').read_text()
hal210=(folder/'phase3-2.10.hal').read_text()
hal_common=(folder/'phase3-common.hal').read_text()
hal=hal29+'\n'+hal_common
assert 'loadrt conv_float_u32 names=spindle-speed-converter' in hal29
assert 'loadrt conv_real_uint names=spindle-speed-converter' in hal210
assert 'source phase3-common.hal' in hal29 and 'source phase3-common.hal' in hal210
expected={
    'X': {'joint':0, 'bit':22, 'soft':(0,600), 'physical':(-400,240400),
          'home_side':'min', 'offset':-1, 'velocity':(-2,-0.2)},
    'Y': {'joint':1, 'bit':26, 'soft':(0,400), 'physical':(-400,160400),
          'home_side':'min', 'offset':-1, 'velocity':(-2,-0.2)},
    'Z': {'joint':2, 'bit':27, 'soft':(-200,0), 'physical':(-80400,400),
          'home_side':'max', 'offset':1, 'velocity':(2,0.2)},
}
for axis,expect in expected.items():
    joint,bit=expect['joint'],expect['bit']
    values=ini[f'JOINT_{joint}']
    axis_values=ini[f'AXIS_{axis}']
    scale=float(values['INPUT_SCALE'])
    assert scale==400
    assert (float(axis_values['MIN_LIMIT']),float(axis_values['MAX_LIMIT']))==expect['soft']
    assert (float(values['MIN_LIMIT']),float(values['MAX_LIMIT']))==expect['soft']
    assert float(values['HOME'])==0 and float(values['HOME_OFFSET'])==expect['offset']
    assert (float(values['HOME_SEARCH_VEL']),float(values['HOME_LATCH_VEL']))==expect['velocity']
    assert float(values['HOME_FINAL_VEL'])==5
    assert values['HOME_IGNORE_LIMITS']=='YES' and values['HOME_USE_INDEX']=='NO'
    assert int(values['HOME_SEQUENCE'])==joint
    minimum=limits[axis,'min'];maximum=limits[axis,'max']
    assert int(minimum[6])==int(maximum[6])==bit
    assert (int(minimum[4]),int(maximum[4]))==expect['physical']
    assert int(minimum[5])==int(maximum[5])==40
    soft_min,soft_max=expect['soft']
    assert int(minimum[4])/scale==soft_min-1
    assert int(maximum[4])/scale==soft_max+1
    home_switch=minimum if expect['home_side']=='min' else maximum
    assert int(home_switch[4])==float(values['HOME_OFFSET'])*scale
    # At the ideal switch edge, applying HOME_OFFSET then moving to HOME returns
    # the simulator's raw step coordinate to zero on every axis.
    final_raw=int(home_switch[4])+int((float(values['HOME'])-float(values['HOME_OFFSET']))*scale)
    assert final_raw==0
    expected_net=f'net {axis.lower()}-switch stepgen-ninja.0.input.gp{bit} => joint.{joint}.home-sw-in joint.{joint}.neg-lim-sw-in joint.{joint}.pos-lim-sw-in'
    assert expected_net in hal
assert 'net probe-contact stepgen-ninja.0.input.gp28 => motion.probe-input' in hal
spindle_lines=[
    'loadrt [EMCMOT]EMCMOT servo_period_nsec=[EMCMOT]SERVO_PERIOD num_joints=[KINS]JOINTS num_spindles=[TRAJ]SPINDLES',
    'loadrt stepgen-ninja ip_address="192.168.50.2:8888"',
    'setp stepgen-ninja.0.pwm.0.frequency 10000',
    'setp stepgen-ninja.0.pwm.0.max-scale 24000',
    'setp stepgen-ninja.0.encoder.0.scale 1024',
    'net spindle-enable spindle.0.on => stepgen-ninja.0.output.gp8',
    'net spindle-reverse spindle.0.reverse => stepgen-ninja.0.output.gp12',
    'net spindle-speed-abs spindle.0.speed-out-abs => spindle-speed-converter.in',
    'net spindle-pwm-duty spindle-speed-converter.out => stepgen-ninja.0.pwm.0.duty',
    'net spindle-revs stepgen-ninja.0.encoder.0.position => spindle.0.revs',
    'net spindle-speed-rps stepgen-ninja.0.encoder.0.velocity-rps => spindle.0.speed-in near.0.in2',
    'net spindle-index-enable spindle.0.index-enable <=> stepgen-ninja.0.encoder.0.index-enable',
]
for line in spindle_lines:
    assert line in hal
function_order=[
    'addf stepgen-ninja.0.watchdog-process servo-thread',
    'addf stepgen-ninja.0.process-send servo-thread',
    'addf stepgen-ninja.0.process-recv servo-thread',
]
locations=[hal.index(line) for line in function_order]
assert locations == sorted(locations)
print('PASS: 1-ms servo, 400 steps/mm, mill travel, six switches, X/Y min homing, Z max homing, raw/home offset relationship, HAL nets, probe disabled before homing, spindle command/conversion/feedback wiring')
