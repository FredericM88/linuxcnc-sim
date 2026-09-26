#!/usr/bin/env bash
# Shared constants and ownership checks; no changes merely by sourcing this file.
set -euo pipefail
CNC_NS=cnc-sim-ns
CNC_HOST_LINK=veth-lcnc
CNC_SIM_LINK=veth-sim
CNC_MARKER=/run/cnc-sim-veth.state
CNC_OWNER=cnc-sim-phase1-v1

die() { printf 'cnc-sim network: %s\n' "$*" >&2; exit 1; }
require_root() {
    [[ $EUID -eq 0 ]] || die "Root required. Run: sudo $0 $*"
    command -v ip >/dev/null || die 'iproute2 (ip) is required.'
}
ns_exists() { ip netns list | awk '{print $1}' | grep -Fxq "$CNC_NS"; }
host_exists() { ip link show dev "$CNC_HOST_LINK" >/dev/null 2>&1; }
sim_exists() { ip -n "$CNC_NS" link show dev "$CNC_SIM_LINK" >/dev/null 2>&1; }
check_marker() {
    [[ -f $CNC_MARKER && ! -L $CNC_MARKER ]] || die "Missing ownership marker $CNC_MARKER; refusing to remove existing resources."
    [[ $(cat "$CNC_MARKER") == "$CNC_OWNER" ]] || die 'Unexpected ownership marker.'
}
check_owned_links() {
    if host_exists; then
        ip -d link show dev "$CNC_HOST_LINK" | grep -Fq "alias $CNC_OWNER" || die 'Host interface belongs to another setup.'
    fi
    if ns_exists && sim_exists; then
        ip -n "$CNC_NS" -d link show dev "$CNC_SIM_LINK" | grep -Fq "alias $CNC_OWNER" || die 'Simulator interface belongs to another setup.'
    fi
}
