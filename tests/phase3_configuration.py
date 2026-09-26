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
for joint,axis,bit in ((0,'X',22),(1,'Y',26),(2,'Z',27)):
    values=ini[f'JOINT_{joint}']
    assert float(values['INPUT_SCALE'])==400
    assert float(values['HOME_SEARCH_VEL'])<0 and float(values['HOME_LATCH_VEL'])<0
    assert values['HOME_IGNORE_LIMITS']=='YES' and values['HOME_USE_INDEX']=='NO'
    assert int(values['HOME_SEQUENCE'])==joint
    minimum=limits[axis,'min'];maximum=limits[axis,'max']
    assert int(minimum[6])==int(maximum[6])==bit
    assert int(minimum[4])==float(values['HOME_OFFSET'])*400
    assert int(minimum[5])==int(maximum[5])==40
    assert int(minimum[4])/400<float(values['MIN_LIMIT'])<float(values['HOME'])<float(values['MAX_LIMIT'])<int(maximum[4])/400
    expected=f'net {axis.lower()}-switch stepgen-ninja.0.input.gp{bit} => joint.{joint}.home-sw-in joint.{joint}.neg-lim-sw-in joint.{joint}.pos-lim-sw-in'
    assert expected in hal
assert 'net probe-contact stepgen-ninja.0.input.gp28 => motion.probe-input' in hal
print('PASS: 1-ms servo, 400 steps/mm, six switches, actual HAL nets, matching homing offsets, safe soft limits, probe disabled before homing')
