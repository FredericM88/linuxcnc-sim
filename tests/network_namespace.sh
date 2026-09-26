#!/usr/bin/env bash
# No host-network changes: all administrative operations run in an unprivileged
# user namespace, with private network/mount namespaces and a temporary /run.
set -euo pipefail
PROJECT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
if [[ ${1-} != --inside ]]; then
    for tool in unshare ip mount python3; do
        if ! command -v "$tool" >/dev/null; then
            printf 'SKIP: %s unavailable\n' "$tool"
            exit 77
        fi
    done
    if ! unshare --user --map-root-user --net --mount true 2>/dev/null; then
        printf 'SKIP: unprivileged user/network/mount namespaces unavailable\n'
        exit 77
    fi
    exec unshare --user --map-root-user --net --mount bash "$0" --inside "${1:-$PROJECT_DIR/build/cnc-sim}"
fi

export CNC_SIM_BINARY="${2:-$PROJECT_DIR/build/cnc-sim}"
mount --make-rprivate /
mount -t tmpfs tmpfs /run
log=$(mktemp)
sim_pid=
cleanup() {
    if [[ -n $sim_pid ]]; then kill "$sim_pid" 2>/dev/null || true; wait "$sim_pid" 2>/dev/null || true; fi
    rm -f -- "$log"
}
trap cleanup EXIT
"$PROJECT_DIR/scripts/setup-veth.sh"
"$PROJECT_DIR/scripts/setup-veth.sh" # Idempotence without deleting interfaces.
ip -4 address show dev veth-lcnc | grep -q '192.168.50.1/24'
ip -n cnc-sim-ns -4 address show dev veth-sim | grep -q '192.168.50.2/24'
ip -n cnc-sim-ns link show dev lo | grep -q UP
"$PROJECT_DIR/scripts/run-simulator.sh" --steps-per-unit 400,400,400,400 --no-stats > "$log" 2>&1 &
sim_pid=$!
for ((i=0; i<100; i++)); do
    if grep -q 'Listening: 192.168.50.2:8888' "$log"; then break; fi
    if ! kill -0 "$sim_pid" 2>/dev/null; then cat "$log"; exit 1; fi
    sleep 0.01
done
grep -q 'Listening: 192.168.50.2:8888' "$log"
if "$PROJECT_DIR/scripts/teardown-veth.sh"; then
    printf 'ERROR: teardown removed a live namespace\n' >&2
    exit 1
fi
PYTHONPATH="$PROJECT_DIR/tests" python3 - <<'PY'
import socket
import time
from udp_integration import exchange
with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
    # Same source and destination port, exactly like the unchanged HAL driver.
    sock.bind(('0.0.0.0', 8888))
    sock.settimeout(1)
    start = time.monotonic()
    for i in range(1000):
        exchange(sock, ('192.168.50.2', 8888), i & 255, 4)
        time.sleep(max(0, start + (i + 1) * .001 - time.monotonic()))
print('PASS: veth / separate namespaces / UDP 8888 on both sides / 4000 steps')
PY
kill -TERM "$sim_pid"
wait "$sim_pid"
sim_pid=
grep -q 'Connected peer: 192.168.50.1:8888' "$log"
grep -q 'X  4000 steps  10.0000 mm' "$log"
grep -q 'Packet-ID gaps: 0' "$log"
"$PROJECT_DIR/scripts/teardown-veth.sh"
"$PROJECT_DIR/scripts/teardown-veth.sh"
! ip link show dev veth-lcnc >/dev/null 2>&1
! ip netns list | grep -q cnc-sim-ns
[[ ! -e /run/cnc-sim-veth.state ]]

# Neither setup nor teardown may destroy resources that they did not create.
ip netns add cnc-sim-ns
if "$PROJECT_DIR/scripts/setup-veth.sh"; then exit 1; fi
if "$PROJECT_DIR/scripts/teardown-veth.sh"; then exit 1; fi
ip netns list | grep -q cnc-sim-ns
ip netns delete cnc-sim-ns
printf 'PASS: network lifecycle, repeat setup/teardown, live-process and ownership checks\n'
