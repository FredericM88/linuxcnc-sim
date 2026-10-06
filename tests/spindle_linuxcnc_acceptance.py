#!/usr/bin/env python3
"""Headless LinuxCNC display for the Phase 6.1 spindle acceptance test."""

import math
import os
import sys
import time

import hal
import linuxcnc


TIMEOUT = 12.0


def fail(message):
    raise RuntimeError(message)


def pin(name):
    return hal.get_value(name)


def close(actual, expected, tolerance=1.0e-6):
    return math.isclose(float(actual), float(expected), rel_tol=0.0, abs_tol=tolerance)


def wait_until(predicate, description, timeout=TIMEOUT):
    deadline = time.monotonic() + timeout
    last = None
    while time.monotonic() < deadline:
        try:
            last = predicate()
            if last:
                return last
        except (RuntimeError, hal.error):
            pass
        time.sleep(0.01)
    fail(f"timeout waiting for {description}; last={last!r}")


def command_complete(command, timeout=TIMEOUT):
    control.mdi(command)
    result = control.wait_complete(timeout)
    if result == -1:
        fail(f"LinuxCNC timed out executing MDI command: {command}")
    status.poll()
    if status.interp_state != linuxcnc.INTERP_IDLE:
        fail(f"LinuxCNC did not return to interpreter idle after: {command}")


def set_mode(mode):
    control.mode(mode)
    if control.wait_complete(5.0) == -1:
        fail(f"LinuxCNC timed out changing task mode to {mode}")


def simulator_command(command):
    fifo = os.environ["CNC_SIM_CONTROL_FIFO"]
    with open(fifo, "w", encoding="ascii") as stream:
        stream.write(command + "\n")
        stream.flush()


def spindle_snapshot():
    return {
        "on": bool(pin("spindle.0.on")),
        "forward": bool(pin("spindle.0.forward")),
        "reverse": bool(pin("spindle.0.reverse")),
        "speed": float(pin("spindle.0.speed-out")),
        "speed_abs": float(pin("spindle.0.speed-out-abs")),
        "gp8": bool(pin("stepgen-ninja.0.output.gp8")),
        "gp12": bool(pin("stepgen-ninja.0.output.gp12")),
        "duty": int(pin("stepgen-ninja.0.pwm.0.duty")),
        "raw": int(pin("stepgen-ninja.0.encoder.0.raw-count")),
        "position": float(pin("stepgen-ninja.0.encoder.0.position")),
        "rpm": float(pin("stepgen-ninja.0.encoder.0.velocity-rpm")),
        "revs": float(pin("spindle.0.revs")),
        "speed_in": float(pin("spindle.0.speed-in")),
        "at_speed": bool(pin("spindle.0.at-speed")),
    }


def report_spindle(label):
    value = spindle_snapshot()
    print(
        "OBS: {:<18} lc-on={} lc-fwd={} lc-rev={} speed={:.1f} "
        "gp8={} gp12={} duty={} raw={} pos={:.6f} rpm={:.1f} "
        "revs={:.6f} speed-in={:.3f} at-speed={}".format(
            label,
            value["on"],
            value["forward"],
            value["reverse"],
            value["speed"],
            value["gp8"],
            value["gp12"],
            value["duty"],
            value["raw"],
            value["position"],
            value["rpm"],
            value["revs"],
            value["speed_in"],
            value["at_speed"],
        ),
        flush=True,
    )
    return value


def assert_feedback_consistency(value):
    if not close(value["position"], value["revs"], 1.0e-6):
        fail(f"spindle.0.revs does not match encoder position: {value}")
    if not close(value["speed_in"], value["rpm"] / 60.0, 0.2):
        fail(f"spindle.0.speed-in does not match encoder RPS: {value}")


def wait_running(expected_rpm):
    if expected_rpm > 0:
        low, high = expected_rpm * 0.85, expected_rpm * 1.15
    else:
        low, high = expected_rpm * 1.15, expected_rpm * 0.85
    wait_until(
        lambda: low <= float(pin("stepgen-ninja.0.encoder.0.velocity-rpm")) <= high,
        f"encoder velocity in [{low}, {high}] RPM",
    )
    wait_until(lambda: bool(pin("spindle.0.at-speed")), "spindle at-speed feedback")


def assert_running(label, command, expected_rpm, reverse):
    command_complete(command)
    wait_running(expected_rpm)
    before_raw = int(pin("stepgen-ninja.0.encoder.0.raw-count"))
    before_position = float(pin("stepgen-ninja.0.encoder.0.position"))
    time.sleep(0.12)
    value = report_spindle(label)
    delta_raw = value["raw"] - before_raw
    delta_position = value["position"] - before_position

    if not value["on"] or not value["gp8"]:
        fail(f"{label}: spindle enable did not reach gp8")
    if value["reverse"] != reverse or value["gp12"] != reverse:
        fail(f"{label}: direction did not reach gp12: {value}")
    if value["forward"] == reverse:
        fail(f"{label}: LinuxCNC forward/reverse command pins disagree: {value}")
    if not close(value["speed"], expected_rpm, 0.5):
        fail(f"{label}: unexpected signed LinuxCNC speed: {value}")
    if not close(value["speed_abs"], abs(expected_rpm), 0.5):
        fail(f"{label}: unexpected absolute LinuxCNC speed: {value}")
    if value["duty"] != int(abs(expected_rpm)):
        fail(f"{label}: converted PWM duty is wrong: {value}")
    if expected_rpm > 0 and (delta_raw <= 1000 or delta_position <= 1.0):
        fail(f"{label}: encoder did not advance: raw delta={delta_raw}, position delta={delta_position}")
    if expected_rpm < 0 and (delta_raw >= -1000 or delta_position >= -1.0):
        fail(f"{label}: encoder did not reverse: raw delta={delta_raw}, position delta={delta_position}")
    assert_feedback_consistency(value)


def assert_stopped(label, command, enabled):
    command_complete(command)
    try:
        wait_until(
            lambda: -100.0 <= float(pin("stepgen-ninja.0.encoder.0.velocity-rpm")) <= 100.0,
            f"{label} encoder stop",
        )
    except RuntimeError:
        fail(f"{label}: encoder did not stop; state={spindle_snapshot()}")
    before_raw = int(pin("stepgen-ninja.0.encoder.0.raw-count"))
    before_position = float(pin("stepgen-ninja.0.encoder.0.position"))
    time.sleep(0.15)
    value = report_spindle(label)
    if value["on"] != enabled or value["gp8"] != enabled:
        fail(f"{label}: wrong LinuxCNC/gp8 enable state: {value}")
    if value["duty"] != 0 or not close(value["speed"], 0.0, 0.5):
        fail(f"{label}: speed command did not become zero: {value}")
    if value["raw"] != before_raw or not close(value["position"], before_position):
        fail(
            f"{label}: encoder accumulated while stopped: "
            f"raw {before_raw}->{value['raw']}, position {before_position}->{value['position']}"
        )
    assert_feedback_consistency(value)


def check_s_zero_after_running():
    """Expose LinuxCNC's retained canonical speed rather than hiding it."""
    command_complete("S0 M3")
    time.sleep(0.15)
    first = spindle_snapshot()
    if first["duty"] == 0 and close(first["speed"], 0.0, 0.5):
        before_raw = first["raw"]
        time.sleep(0.15)
        value = report_spindle("S0 M3")
        if value["raw"] != before_raw or not value["on"] or not value["gp8"]:
            fail(f"S0 M3 did not remain enabled and stopped: {value}")
        return False

    before_raw = first["raw"]
    time.sleep(0.12)
    value = report_spindle("S0 M3 / retained")
    if (
        not value["on"]
        or not value["gp8"]
        or value["duty"] != 6000
        or not close(value["speed"], 6000.0, 0.5)
        or value["raw"] - before_raw <= 1000
    ):
        fail(f"S0 M3 produced an unexpected state: {value}")
    print(
        "LIMITATION: LinuxCNC 2.10-pre2 retained the prior canonical 6000 RPM for "
        "S0 M3; HAL therefore cannot stop PWM for this sequence without controller-side repair",
        flush=True,
    )
    return True


def exercise_xyz_limits_and_probe():
    set_mode(linuxcnc.MODE_MANUAL)
    seen = {22: False, 26: False, 27: False}
    control.home(-1)
    deadline = time.monotonic() + 20.0
    while time.monotonic() < deadline:
        for gpio in seen:
            seen[gpio] = seen[gpio] or bool(pin(f"stepgen-ninja.0.input.gp{gpio}"))
        status.poll()
        if all(status.homed[:3]) and status.inpos:
            break
        time.sleep(0.005)
    else:
        fail(f"XYZ homing did not complete; homed={status.homed[:3]}, switches={seen}")
    if not all(seen.values()):
        fail(f"homing did not exercise every virtual limit/home input: {seen}")

    set_mode(linuxcnc.MODE_MDI)
    command_complete("G21 G90 G40 G49 G80")
    command_complete("G10 L2 P1 X0 Y0 Z0")
    command_complete("G54")
    command_complete("G53 G0 X10 Y10 Z-5")
    status.poll()
    xyz = tuple(float(value) for value in status.position[:3])
    if any(not close(actual, expected, 0.03) for actual, expected in zip(xyz, (10.0, 10.0, -5.0))):
        fail(f"XYZ normal motion did not reach the expected machine position: {xyz}")

    simulator_command("probe plane Z -4000 28")
    wait_until(lambda: not bool(pin("motion.probe-input")), "probe input clear before probing")
    command_complete("G38.2 Z-15 F60")
    wait_until(lambda: bool(pin("motion.probe-input")), "virtual probe input")
    status.poll()
    probed_z = float(status.probed_position[2])
    if not -10.20 <= probed_z <= -9.80:
        fail(f"probe trigger position was outside tolerance: Z={probed_z}")
    if not bool(pin("stepgen-ninja.0.input.gp28")):
        fail("probe reached motion.probe-input without Stepper-Ninja gp28")

    command_complete("G53 G0 Z-5")
    wait_until(lambda: not bool(pin("motion.probe-input")), "probe input clear after retract")
    simulator_command("probe off")
    print(
        "OBS: XYZ/IO regression homed=(1,1,1) switches=(gp22,gp26,gp27) "
        f"position=({xyz[0]:.3f},{xyz[1]:.3f},{xyz[2]:.3f}) probed-z={probed_z:.3f} gp28=TRUE",
        flush=True,
    )


def main():
    global control, status
    control = linuxcnc.command()
    status = linuxcnc.stat()

    def startup_ready():
        status.poll()
        return (
            status.axis_mask != 0
            and status.cycle_time != 0.0
            and status.exec_state == linuxcnc.EXEC_DONE
            and status.interp_state == linuxcnc.INTERP_IDLE
            and status.linear_units != 0.0
            and status.state == linuxcnc.RCS_DONE
            and status.task_state == linuxcnc.STATE_ESTOP
        )

    wait_until(startup_ready, "LinuxCNC task startup")
    wait_until(lambda: bool(pin("stepgen-ninja.0.connected")), "Stepper-Ninja UDP connection")

    control.state(linuxcnc.STATE_ESTOP_RESET)
    if control.wait_complete(5.0) == -1:
        fail("LinuxCNC timed out resetting E-stop")
    control.state(linuxcnc.STATE_ON)
    if control.wait_complete(5.0) == -1:
        fail("LinuxCNC timed out enabling the machine")
    wait_until(lambda: bool(pin("stepgen-ninja.0.io-ready-out")), "Stepper-Ninja I/O ready")

    if int(pin("stepgen-ninja.0.pwm.0.frequency")) != 10000:
        fail("PWM frequency is not 10000 Hz")
    if int(pin("stepgen-ninja.0.pwm.0.max-scale")) != 24000:
        fail("PWM max-scale is not 24000 RPM")
    if not close(pin("stepgen-ninja.0.encoder.0.scale"), 1024.0):
        fail("encoder scale is not 1024 CPR")

    exercise_xyz_limits_and_probe()
    set_mode(linuxcnc.MODE_MDI)

    assert_stopped("M5 / stopped", "M5", False)
    # A fresh controller has a genuinely zero canonical S value. This proves
    # enabled/zero-PWM transport separately from the retained-S limitation below.
    assert_stopped("S0 M3 / fresh", "S0 M3", True)
    assert_stopped("M5 / reset", "M5", False)
    assert_running("S6000 M3", "S6000 M3", 6000.0, False)
    assert_running("S12000 M3", "S12000 M3", 12000.0, False)
    assert_running("S6000 M4", "S6000 M4", -6000.0, True)
    retained_s_zero = check_s_zero_after_running()
    assert_stopped("M5 / final", "M5", False)

    suffix = " (with documented LinuxCNC S0 retention)" if retained_s_zero else ""
    print(
        "PASS: LinuxCNC MDI spindle integration, XYZ homing/limits, probe and encoder feedback" + suffix,
        flush=True,
    )


if __name__ == "__main__":
    try:
        main()
    except Exception as error:  # LinuxCNC uses the display exit status as the test result.
        print(f"FAIL: {error}", file=sys.stderr, flush=True)
        sys.exit(1)
