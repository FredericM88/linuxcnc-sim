#!/usr/bin/env bash
# Live spindle path: HAL pins -> unmodified Stepper-Ninja driver -> UDP ->
# cnc-sim -> UDP encoder feedback -> Stepper-Ninja driver -> HAL pins.
#
# Administrative network operations and HAL runtime state are confined to
# unprivileged user, network, mount, IPC and PID namespaces. The existing veth
# lifecycle scripts still own and validate every network object created by the
# test.
set -euo pipefail

PROJECT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
DEFAULT_RIP="$HOME/dev/linuxcnc-2.10-source"

skip() {
    printf 'SKIP: %s\n' "$*"
    exit 77
}

if [[ ${1-} != --inside ]]; then
    binary=${1:-"$PROJECT_DIR/build/cnc-sim"}
    rip=${LINUXCNC_RIP:-$DEFAULT_RIP}
    runtime_uid=$(id -u)
    runtime_gid=$(id -g)
    if (( runtime_uid == 0 )); then runtime_uid=65534; fi
    if (( runtime_gid == 0 )); then runtime_gid=65534; fi

    [[ -x $binary ]] || skip "cnc-sim binary is not executable: $binary"
    [[ -x $rip/scripts/rip-environment ]] || skip "LinuxCNC 2.10 RIP not found at $rip"
    [[ -x $rip/bin/halcmd && -x $rip/scripts/halrun && -x $rip/scripts/realtime ]] ||
        skip "LinuxCNC RIP at $rip is not built"
    [[ -f $rip/rtlib/stepgen-ninja.so ]] ||
        skip "stepgen-ninja.so is not installed in $rip/rtlib"

    version=$("$rip/scripts/rip-environment" sh -c 'printf "%s\n" "$LINUXCNCVERSION"')
    [[ $version == 2.10* ]] || skip "LinuxCNC RIP is $version, not 2.10"

    for tool in timeout unshare ip mount awk grep mktemp; do
        command -v "$tool" >/dev/null || skip "$tool unavailable"
    done
    if ! unshare --user --map-root-user --net --mount --ipc --pid --fork \
        --kill-child=TERM --mount-proc true 2>/dev/null; then
        skip "unprivileged user/network/mount/IPC/PID namespaces unavailable"
    fi

    set +e
    timeout --signal=TERM --kill-after=5s 45s \
        unshare --user --map-root-user --net --mount --ipc --pid --fork \
        --kill-child=TERM --mount-proc \
        bash "$0" --inside "$binary" "$rip" "$runtime_uid" "$runtime_gid"
    result=$?
    set -e
    if (( result == 124 || result == 137 )); then
        printf 'FAIL: live spindle smoke test exceeded its 45-second timeout\n' >&2
        exit 1
    fi
    exit "$result"
fi

binary=$2
rip=$3
runtime_uid=$4
runtime_gid=$5
rip_environment="$rip/scripts/rip-environment"
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/cnc-sim-spindle-live.XXXXXX")
sim_log="$work_dir/cnc-sim.log"
hal_log="$work_dir/halrun.log"
hal_cmd_log="$work_dir/halcmd.log"
hal_fifo="$work_dir/hal-input"
sim_pid=
halrun_pid=
hal_input_fd=
network_started=false
cleanup_done=false

# Network setup needs namespace-root privileges, while LinuxCNC's uspace
# rtapi_app deliberately rejects uid 0. A nested user namespace maps the same
# underlying caller to its original non-root uid without leaving this test's
# network, mount, IPC or PID namespaces.
linuxcnc_cmd() {
    unshare --user --map-user="$runtime_uid" --map-group="$runtime_gid" --keep-caps \
        "$rip_environment" "$@"
}

wait_for_exit() {
    local pid=$1
    for ((attempt=0; attempt<100; ++attempt)); do
        kill -0 "$pid" 2>/dev/null || return 0
        sleep 0.02
    done
    return 1
}

cleanup_resources() {
    local force=${1:-false}
    local cleanup_result=0
    $cleanup_done && return 0

    set +e
    if [[ -n $hal_input_fd ]]; then
        printf 'exit\n' >&"$hal_input_fd"
        exec {hal_input_fd}>&-
        hal_input_fd=
    fi
    if [[ -n $halrun_pid ]]; then
        if ! wait_for_exit "$halrun_pid"; then
            kill -TERM "$halrun_pid" 2>/dev/null
            wait_for_exit "$halrun_pid" || kill -KILL "$halrun_pid" 2>/dev/null
        fi
        wait "$halrun_pid" 2>/dev/null
        halrun_pid=
    fi
    if $force && linuxcnc_cmd realtime status >/dev/null 2>&1; then
        linuxcnc_cmd halrun -U >>"$hal_log" 2>&1
    fi
    if linuxcnc_cmd realtime status >/dev/null 2>&1; then
        printf 'FAIL: LinuxCNC realtime still active after cleanup\n' >&2
        cleanup_result=1
    fi

    if [[ -n $sim_pid ]]; then
        kill -TERM "$sim_pid" 2>/dev/null
        if ! wait_for_exit "$sim_pid"; then
            kill -KILL "$sim_pid" 2>/dev/null
            cleanup_result=1
        fi
        wait "$sim_pid" 2>/dev/null
        sim_pid=
    fi
    if $network_started; then
        "$PROJECT_DIR/scripts/teardown-veth.sh" >>"$sim_log" 2>&1 || cleanup_result=1
        network_started=false
    fi
    cleanup_done=true
    set -e
    return "$cleanup_result"
}

on_exit() {
    local result=$?
    trap - EXIT INT TERM HUP
    if (( result != 0 )); then
        printf 'FAIL: live spindle smoke test stopped with status %d\n' "$result" >&2
        [[ ! -s $hal_log ]] || { printf '%s\n' '--- halrun log ---' >&2; tail -80 "$hal_log" >&2; }
        [[ ! -s $hal_cmd_log ]] || { printf '%s\n' '--- halcmd log ---' >&2; tail -80 "$hal_cmd_log" >&2; }
        [[ ! -s $sim_log ]] || { printf '%s\n' '--- cnc-sim log ---' >&2; tail -80 "$sim_log" >&2; }
    fi
    cleanup_resources true || result=1
    if [[ -n $work_dir && -d $work_dir ]]; then rm -rf -- "$work_dir"; fi
    exit "$result"
}
trap on_exit EXIT INT TERM HUP

fail() {
    printf 'FAIL: %s\n' "$*" >&2
    return 1
}

hal_cmd() {
    linuxcnc_cmd halcmd "$@" 2>>"$hal_cmd_log"
}

hal_send() {
    printf '%s\n' "$*" >&"$hal_input_fd"
}

hal_get() {
    local value
    value=$(hal_cmd getp "$1") || return 1
    printf '%s' "$value"
}

number_in_range() {
    awk -v actual="$1" -v low="$2" -v high="$3" \
        'BEGIN { exit !(actual == actual && actual >= low && actual <= high) }'
}

wait_pin_value() {
    local pin=$1 expected=$2
    local actual=
    for ((attempt=0; attempt<150; ++attempt)); do
        actual=$(hal_get "$pin" 2>/dev/null || true)
        if [[ ${actual^^} == "${expected^^}" ]]; then
            observed_value=$actual
            return 0
        fi
        sleep 0.02
    done
    fail "$pin did not become $expected (last value: ${actual:-unavailable})"
}

wait_pin_range() {
    local pin=$1 low=$2 high=$3 description=$4
    local actual=
    for ((attempt=0; attempt<150; ++attempt)); do
        actual=$(hal_get "$pin" 2>/dev/null || true)
        if [[ -n $actual ]] && number_in_range "$actual" "$low" "$high"; then
            observed_value=$actual
            return 0
        fi
        sleep 0.02
    done
    fail "$description: $pin was ${actual:-unavailable}, expected [$low, $high]"
}

set_spindle() {
    local enabled=$1 reverse=$2 duty=$3
    hal_cmd setp stepgen-ninja.0.output.gp8 "$enabled" >>"$hal_cmd_log"
    hal_cmd setp stepgen-ninja.0.output.gp12 "$reverse" >>"$hal_cmd_log"
    hal_cmd setp stepgen-ninja.0.pwm.0.duty "$duty" >>"$hal_cmd_log"
}

read_state() {
    state_raw=$(hal_get stepgen-ninja.0.encoder.0.raw-count)
    state_position=$(hal_get stepgen-ninja.0.encoder.0.position)
    state_rpm=$(hal_get stepgen-ninja.0.encoder.0.velocity-rpm)
}

report_state() {
    local label=$1
    read_state
    printf 'OBS: %-22s raw-count=%s position=%s velocity-rpm=%s\n' \
        "$label" "$state_raw" "$state_position" "$state_rpm"
}

assert_motion() {
    local label=$1 direction=$2
    local first_raw first_position second_raw second_position delta_raw delta_position
    first_raw=$(hal_get stepgen-ninja.0.encoder.0.raw-count)
    first_position=$(hal_get stepgen-ninja.0.encoder.0.position)
    sleep 0.12
    second_raw=$(hal_get stepgen-ninja.0.encoder.0.raw-count)
    second_position=$(hal_get stepgen-ninja.0.encoder.0.position)
    delta_raw=$((second_raw - first_raw))
    delta_position=$(awk -v after="$second_position" -v before="$first_position" 'BEGIN { print after - before }')
    if [[ $direction == forward ]]; then
        (( delta_raw > 1000 )) || fail "$label raw-count did not increase: delta=$delta_raw"
        number_in_range "$delta_position" 1 1000000 || fail "$label position did not increase: delta=$delta_position"
    else
        (( delta_raw < -1000 )) || fail "$label raw-count did not decrease: delta=$delta_raw"
        number_in_range "$delta_position" -1000000 -1 || fail "$label position did not decrease: delta=$delta_position"
    fi
}

assert_stopped() {
    local label=$1 first_raw second_raw first_position second_position
    first_raw=$(hal_get stepgen-ninja.0.encoder.0.raw-count)
    first_position=$(hal_get stepgen-ninja.0.encoder.0.position)
    sleep 0.15
    second_raw=$(hal_get stepgen-ninja.0.encoder.0.raw-count)
    second_position=$(hal_get stepgen-ninja.0.encoder.0.position)
    [[ $second_raw == "$first_raw" ]] || fail "$label accumulated counts while stopped: $first_raw -> $second_raw"
    number_in_range "$second_position" "$first_position" "$first_position" ||
        fail "$label position changed while stopped: $first_position -> $second_position"
}

# Isolate LinuxCNC runtime files as well as network state from the host.
mount --make-rprivate /
mount -t tmpfs -o mode=0755,nosuid,nodev tmpfs /run
mount -t tmpfs -o mode=1777,nosuid,nodev tmpfs /dev/shm

"$PROJECT_DIR/scripts/setup-veth.sh" >"$sim_log" 2>&1
network_started=true
export CNC_SIM_BINARY=$binary
"$PROJECT_DIR/scripts/run-simulator.sh" --no-stats >>"$sim_log" 2>&1 &
sim_pid=$!
for ((attempt=0; attempt<200; ++attempt)); do
    grep -q 'Listening: 192.168.50.2:8888' "$sim_log" && break
    kill -0 "$sim_pid" 2>/dev/null || fail "cnc-sim exited during startup"
    sleep 0.01
done
grep -q 'Listening: 192.168.50.2:8888' "$sim_log" || fail "cnc-sim did not become ready"

mkfifo "$hal_fifo"
linuxcnc_cmd halrun -I <"$hal_fifo" >"$hal_log" 2>&1 &
halrun_pid=$!
exec {hal_input_fd}>"$hal_fifo"

hal_send 'loadrt threads name1=spindle-smoke-thread period1=1000000 fp1=1'
hal_send 'loadrt stepgen-ninja ip_address="192.168.50.2:8888"'

function_table=
for ((attempt=0; attempt<150; ++attempt)); do
    function_table=$(hal_cmd show funct 2>/dev/null || true)
    if grep -Fq 'stepgen-ninja.0.watchdog-process' <<<"$function_table" &&
       grep -Fq 'stepgen-ninja.0.process-send' <<<"$function_table" &&
       grep -Fq 'stepgen-ninja.0.process-recv' <<<"$function_table"; then
        break
    fi
    kill -0 "$halrun_pid" 2>/dev/null || fail "halrun exited while loading stepgen-ninja"
    sleep 0.02
done
for function_name in \
    stepgen-ninja.0.watchdog-process \
    stepgen-ninja.0.process-send \
    stepgen-ninja.0.process-recv; do
    grep -Fq "$function_name" <<<"$function_table" || fail "missing realtime function $function_name"
done
printf 'FUNCTIONS: discovered watchdog-process, process-send, process-recv\n'

# The order is deliberate: establish watchdog state, transmit this cycle's
# commands, then consume the response produced by that request.
hal_send 'addf stepgen-ninja.0.watchdog-process spindle-smoke-thread'
hal_send 'addf stepgen-ninja.0.process-send spindle-smoke-thread'
hal_send 'addf stepgen-ninja.0.process-recv spindle-smoke-thread'
hal_send 'setp stepgen-ninja.0.io-ready-in true'
hal_send 'setp stepgen-ninja.0.output.gp8 false'
hal_send 'setp stepgen-ninja.0.output.gp12 false'
hal_send 'setp stepgen-ninja.0.pwm.0.enable true'
hal_send 'setp stepgen-ninja.0.pwm.0.frequency 10000'
hal_send 'setp stepgen-ninja.0.pwm.0.max-scale 24000'
hal_send 'setp stepgen-ninja.0.pwm.0.min-limit 0'
hal_send 'setp stepgen-ninja.0.pwm.0.duty 0'
hal_send 'setp stepgen-ninja.0.encoder.0.scale 1024'
hal_send 'setp stepgen-ninja.0.encoder.0.index-enable false'
hal_send 'start'

wait_pin_value stepgen-ninja.0.connected TRUE
wait_pin_value stepgen-ninja.0.io-ready-out TRUE
[[ $(hal_get stepgen-ninja.0.pwm.0.frequency) == 10000 ]] || fail "PWM frequency is not 10000 Hz"
[[ $(hal_get stepgen-ninja.0.pwm.0.max-scale) == 24000 ]] || fail "PWM max-scale is not 24000"
[[ $(hal_get stepgen-ninja.0.encoder.0.scale) == 1024 ]] || fail "encoder scale is not 1024 CPR"
[[ $(hal_get stepgen-ninja.0.pwm.0.enable) == TRUE ]] || fail "PWM channel 0 is not enabled"

thread_table=$(hal_cmd show thread)
watchdog_order=$(awk '/stepgen-ninja\.0\.watchdog-process/ { print NR; exit }' <<<"$thread_table")
send_order=$(awk '/stepgen-ninja\.0\.process-send/ { print NR; exit }' <<<"$thread_table")
recv_order=$(awk '/stepgen-ninja\.0\.process-recv/ { print NR; exit }' <<<"$thread_table")
[[ -n $watchdog_order && -n $send_order && -n $recv_order ]] || fail "thread function list is incomplete"
(( watchdog_order < send_order && send_order < recv_order )) || fail "realtime functions are in the wrong order"
printf 'THREAD: 1 ms order watchdog-process -> process-send -> process-recv\n'

sleep 0.10
wait_pin_range stepgen-ninja.0.encoder.0.velocity-rpm -100 100 'initial stopped RPM'
assert_stopped 'initial stopped/disabled'
report_state 'stopped/disabled'

set_spindle true false 6000
wait_pin_range stepgen-ninja.0.encoder.0.velocity-rpm 5000 7000 'forward 6000 RPM'
assert_motion 'forward 6000 RPM' forward
report_state 'forward 6000 rpm'

set_spindle true false 12000
wait_pin_range stepgen-ninja.0.encoder.0.velocity-rpm 10500 13500 'forward 12000 RPM'
assert_motion 'forward 12000 RPM' forward
report_state 'forward 12000 rpm'

set_spindle true true 6000
wait_pin_range stepgen-ninja.0.encoder.0.velocity-rpm -7000 -5000 'reverse 6000 RPM'
assert_motion 'reverse 6000 RPM' reverse
report_state 'reverse 6000 rpm'

set_spindle true false 0
wait_pin_range stepgen-ninja.0.encoder.0.velocity-rpm -100 100 'enabled zero-PWM RPM'
[[ $(hal_get stepgen-ninja.0.output.gp8) == TRUE ]] || fail "zero-PWM state lost spindle enable"
[[ $(hal_get stepgen-ninja.0.pwm.0.duty) == 0 ]] || fail "zero-PWM state has nonzero duty"
assert_stopped 'enabled zero PWM'
report_state 'enabled / zero PWM'

set_spindle false false 0
wait_pin_range stepgen-ninja.0.encoder.0.velocity-rpm -100 100 'disabled RPM'
assert_stopped 'stopped/disabled again'
report_state 'stopped/disabled again'

# Arm while stationary so TRUE is observable deterministically, then rotate at
# 60 RPM. The next index is at most one second away and the driver must clear
# index-enable after receiving interrupt_data bit 0.
pre_index_raw=$(hal_get stepgen-ninja.0.encoder.0.raw-count)
(( pre_index_raw > 2048 || pre_index_raw < -2048 )) || fail "insufficient pre-index count: $pre_index_raw"
hal_cmd setp stepgen-ninja.0.encoder.0.index-enable true >>"$hal_cmd_log"
wait_pin_value stepgen-ninja.0.encoder.0.index-enable TRUE
set_spindle true false 60
wait_pin_value stepgen-ninja.0.encoder.0.index-enable FALSE
index_raw=$(hal_get stepgen-ninja.0.encoder.0.raw-count)
index_position=$(hal_get stepgen-ninja.0.encoder.0.position)
(( index_raw >= -512 && index_raw <= 512 )) || fail "index reset raw-count is too far from zero: $index_raw"
number_in_range "$index_position" -0.5 0.5 || fail "index reset position is too far from zero: $index_position"
printf 'OBS: %-22s before=%s raw-count=%s position=%s index-enable=FALSE\n' \
    'index handshake' "$pre_index_raw" "$index_raw" "$index_position"

set_spindle false false 0
wait_pin_range stepgen-ninja.0.encoder.0.velocity-rpm -100 100 'final stopped RPM'
assert_stopped 'final stopped/disabled'
report_state 'final stopped'

cleanup_resources true || fail "cleanup did not remove all live resources"
! ip link show dev veth-lcnc >/dev/null 2>&1 || fail "veth-lcnc remains after cleanup"
! ip netns list | grep -q '^cnc-sim-ns\b' || fail "cnc-sim-ns remains after cleanup"
[[ ! -e /run/cnc-sim-veth.state ]] || fail "network ownership marker remains after cleanup"
grep -q 'Connected peer: 192.168.50.1:8888' "$sim_log" || fail "simulator never reported the HAL peer"

printf 'PASS: live HAL/Stepper-Ninja/UDP/VirtualSpindle/encoder path and cleanup\n'
