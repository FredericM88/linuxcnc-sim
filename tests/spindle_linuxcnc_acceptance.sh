#!/usr/bin/env bash
# Full controller path: LinuxCNC MDI -> spindle HAL -> unmodified
# Stepper-Ninja driver -> UDP -> cnc-sim -> encoder feedback -> spindle HAL.
#
# Follow the B3 live-test isolation model: all runtime, network and IPC state is
# confined to disposable namespaces and every owned resource is removed.
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
    [[ -x $rip/bin/halcmd && -x $rip/scripts/linuxcnc && -x $rip/scripts/realtime ]] ||
        skip "LinuxCNC RIP at $rip is not built"
    [[ -f $rip/rtlib/stepgen-ninja.so ]] ||
        skip "stepgen-ninja.so is not installed in $rip/rtlib"

    version=$("$rip/scripts/rip-environment" sh -c 'printf "%s\n" "$LINUXCNCVERSION"')
    [[ $version == 2.10* ]] || skip "LinuxCNC RIP is $version, not 2.10"

    for tool in timeout unshare ip mount grep mktemp sed cp; do
        command -v "$tool" >/dev/null || skip "$tool unavailable"
    done
    if ! unshare --user --map-root-user --net --mount --ipc --pid --fork \
        --kill-child=TERM --mount-proc true 2>/dev/null; then
        skip "unprivileged user/network/mount/IPC/PID namespaces unavailable"
    fi

    set +e
    timeout --signal=TERM --kill-after=5s 80s \
        unshare --user --map-root-user --net --mount --ipc --pid --fork \
        --kill-child=TERM --mount-proc \
        bash "$0" --inside "$binary" "$rip" "$runtime_uid" "$runtime_gid"
    result=$?
    set -e
    if (( result == 124 || result == 137 )); then
        printf 'FAIL: LinuxCNC spindle acceptance exceeded its 80-second timeout\n' >&2
        exit 1
    fi
    exit "$result"
fi

binary=$2
rip=$3
runtime_uid=$4
runtime_gid=$5
rip_environment="$rip/scripts/rip-environment"
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/cnc-sim-spindle-acceptance.XXXXXX")
config_dir="$work_dir/config"
test_home="$work_dir/home"
sim_fifo="$work_dir/sim-input"
sim_ini="$work_dir/simulator.ini"
sim_log="$work_dir/cnc-sim.log"
linuxcnc_log="$work_dir/linuxcnc.log"
cleanup_log="$work_dir/cleanup.log"
sim_pid=
linuxcnc_pid=
sim_input_fd=
network_started=false
cleanup_done=false

# Network setup runs as namespace root. LinuxCNC uspace rtapi_app runs in a
# nested user namespace as the original, non-root caller, sharing this network.
linuxcnc_cmd() {
    if [[ ${CNC_SIM_TEST_PRIVILEGE_DROP-} == setpriv ]]; then
        setpriv --reuid="$runtime_uid" --regid="$runtime_gid" --clear-groups \
            "$rip_environment" "$@"
    else
        unshare --user --map-user="$runtime_uid" --map-group="$runtime_gid" --keep-caps \
            "$rip_environment" "$@"
    fi
}

wait_for_exit() {
    local pid=$1
    for ((attempt=0; attempt<150; ++attempt)); do
        kill -0 "$pid" 2>/dev/null || return 0
        sleep 0.02
    done
    return 1
}

cleanup_resources() {
    local cleanup_result=0
    $cleanup_done && return 0
    set +e

    if [[ -n $linuxcnc_pid ]]; then
        kill -TERM "$linuxcnc_pid" 2>/dev/null
        wait_for_exit "$linuxcnc_pid" || kill -KILL "$linuxcnc_pid" 2>/dev/null
        wait "$linuxcnc_pid" 2>/dev/null
        linuxcnc_pid=
    fi
    if linuxcnc_cmd realtime status >/dev/null 2>&1; then
        linuxcnc_cmd halrun -U >>"$cleanup_log" 2>&1
    fi
    if linuxcnc_cmd realtime status >/dev/null 2>&1; then
        printf 'FAIL: LinuxCNC realtime still active after cleanup\n' >&2
        cleanup_result=1
    fi

    if [[ -n $sim_input_fd ]]; then
        printf 'quit\n' >&"$sim_input_fd"
        exec {sim_input_fd}>&-
        sim_input_fd=
    fi
    if [[ -n $sim_pid ]]; then
        if ! wait_for_exit "$sim_pid"; then
            kill -TERM "$sim_pid" 2>/dev/null
            wait_for_exit "$sim_pid" || kill -KILL "$sim_pid" 2>/dev/null
        fi
        wait "$sim_pid" 2>/dev/null
        sim_pid=
    fi
    if $network_started; then
        "$PROJECT_DIR/scripts/teardown-veth.sh" >>"$cleanup_log" 2>&1 || cleanup_result=1
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
        printf 'FAIL: LinuxCNC spindle acceptance stopped with status %d\n' "$result" >&2
        [[ ! -s $linuxcnc_log ]] || { printf '%s\n' '--- LinuxCNC log ---' >&2; tail -120 "$linuxcnc_log" >&2; }
        [[ ! -s $sim_log ]] || { printf '%s\n' '--- cnc-sim log ---' >&2; tail -80 "$sim_log" >&2; }
        [[ ! -s $cleanup_log ]] || { printf '%s\n' '--- cleanup log ---' >&2; tail -80 "$cleanup_log" >&2; }
    fi
    cleanup_resources || result=1
    if [[ -n $work_dir && -d $work_dir ]]; then rm -rf -- "$work_dir"; fi
    exit "$result"
}
trap on_exit EXIT INT TERM HUP

fail() {
    printf 'FAIL: %s\n' "$*" >&2
    return 1
}

# Isolate LinuxCNC shared memory/runtime files and all network changes.
mount --make-rprivate /
mount -t tmpfs -o mode=0755,nosuid,nodev tmpfs /run
mount -t tmpfs -o mode=1777,nosuid,nodev tmpfs /dev/shm

mkdir -p "$config_dir" "$test_home"
cp "$PROJECT_DIR/examples/phase3/phase3-2.10.ini" "$config_dir/phase3.ini"
cp "$PROJECT_DIR/examples/phase3/phase3-2.10.hal" \
   "$PROJECT_DIR/examples/phase3/phase3-common.hal" \
   "$PROJECT_DIR/examples/phase3/phase3.tbl" \
   "$PROJECT_DIR/examples/phase3/phase3.var" "$config_dir/"
# Keep the normal AXIS example unchanged; only the disposable test copy uses
# the Python acceptance program as LinuxCNC's display process.
sed -i "s|^DISPLAY = axis$|DISPLAY = $PROJECT_DIR/tests/spindle_linuxcnc_acceptance.py|" \
    "$config_dir/phase3.ini"
if [[ ${CNC_SIM_TEST_PRIVILEGE_DROP-} == setpriv ]]; then
    # Validation inside an already isolated privileged container cannot rely on
    # a nested user namespace. Give the requested runtime user only its temp
    # configuration and home; normal test execution never enters this branch.
    chown "$runtime_uid:$runtime_gid" "$work_dir"
    chown -R "$runtime_uid:$runtime_gid" "$config_dir" "$test_home"
fi

# Reuse the accepted mill simulator profile, changing only display policy and
# making its relative virtual-I/O reference valid from this temporary copy.
cp "$PROJECT_DIR/examples/mill/simulator.ini" "$sim_ini"
sed -i \
    -e 's|^ENABLED = true$|ENABLED = false|' \
    -e "s|^CONFIG = ../phase3/virtual-io.conf$|CONFIG = $PROJECT_DIR/examples/phase3/virtual-io.conf|" \
    "$sim_ini"

"$PROJECT_DIR/scripts/setup-veth.sh" >"$sim_log" 2>&1
network_started=true
mkfifo "$sim_fifo"
if [[ ${CNC_SIM_TEST_PRIVILEGE_DROP-} == setpriv ]]; then chmod 666 "$sim_fifo"; fi
exec {sim_input_fd}<>"$sim_fifo"
export CNC_SIM_BINARY=$binary
"$PROJECT_DIR/scripts/run-simulator.sh" --config "$sim_ini" --no-stats \
    <"$sim_fifo" >>"$sim_log" 2>&1 &
sim_pid=$!
for ((attempt=0; attempt<200; ++attempt)); do
    grep -q 'Listening: 192.168.50.2:8888' "$sim_log" && break
    kill -0 "$sim_pid" 2>/dev/null || fail "cnc-sim exited during startup"
    sleep 0.01
done
grep -q 'Listening: 192.168.50.2:8888' "$sim_log" || fail "cnc-sim did not become ready"

export CNC_SIM_CONTROL_FIFO=$sim_fifo
(
    cd "$config_dir"
    linuxcnc_cmd env HOME="$test_home" CNC_SIM_CONTROL_FIFO="$sim_fifo" \
        linuxcnc -r phase3.ini
) >"$linuxcnc_log" 2>&1 &
linuxcnc_pid=$!

set +e
wait "$linuxcnc_pid"
linuxcnc_result=$?
set -e
linuxcnc_pid=
(( linuxcnc_result == 0 )) || fail "LinuxCNC exited with status $linuxcnc_result"
grep -q '^PASS: LinuxCNC MDI spindle integration' "$linuxcnc_log" ||
    fail "acceptance display did not report PASS"
grep -q 'Connected peer: 192.168.50.1:8888' "$sim_log" ||
    fail "simulator never reported the LinuxCNC peer"

grep -E '^(OBS|LIMITATION|PASS):' "$linuxcnc_log"

cleanup_resources || fail "cleanup did not remove all live resources"
! ip link show dev veth-lcnc >/dev/null 2>&1 || fail "veth-lcnc remains after cleanup"
! ip netns list | grep -q '^cnc-sim-ns\b' || fail "cnc-sim-ns remains after cleanup"
[[ ! -e /run/cnc-sim-veth.state ]] || fail "network ownership marker remains after cleanup"

printf 'PASS: normal LinuxCNC G-code spindle path and isolated cleanup\n'
