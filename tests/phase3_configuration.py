#!/usr/bin/env python3
"""Consistency checks across the supplied LinuxCNC INI/HAL and sensor profile."""
import configparser
import pathlib
root=pathlib.Path(__file__).resolve().parents[1]
folder=root/'examples/phase3'
ini=configparser.ConfigParser(strict=False)
ini.read(folder/'phase3.ini')
assert ini['EMCMOT']['SERVO_PERIOD']=='1000000'
assert ini['HAL']['HALFILE']=='phase3.hal'
assert (folder/ini['EMCIO']['TOOL_TABLE']).exists()
assert (folder/ini['RS274NGC']['PARAMETER_FILE']).name=='phase3.var'
commands=[line.split('#')[0].strip().split() for line in (folder/'virtual-io.conf').read_text().splitlines()]
limits={(a[2],a[3]):a for a in commands if a[:2]==['limits','set']}
assert len(limits)==6 and ['probe','off'] in commands
assert not any(a[:2]==['probe','plane'] for a in commands), 'probe must be enabled after the Z home search'
hal=(folder/'phase3.hal').read_text()
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
print('PASS: 1-ms servo, 400 steps/mm, mill travel, six switches, X/Y min homing, Z max homing, raw/home offset relationship, HAL nets, probe disabled before homing')
