#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=network-common.sh
source "$SCRIPT_DIR/network-common.sh"
require_root "$@"
[[ $# -eq 0 ]] || die 'This script takes no arguments.'

if [[ -e $CNC_MARKER || -L $CNC_MARKER ]]; then
    check_marker
    check_owned_links
    host_exists && ns_exists && sim_exists || die 'Incomplete owned setup. Run teardown-veth.sh, then setup-veth.sh.'
    printf 'Reusing owned namespace %s and veth pair.\n' "$CNC_NS"
else
    if host_exists || ns_exists || ip link show dev "$CNC_SIM_LINK" >/dev/null 2>&1; then
        die 'A requested interface/namespace already exists without our ownership marker; nothing changed.'
    fi
    [[ -z $(ip -4 route show 192.168.50.0/24) ]] || die '192.168.50.0/24 already has a route; nothing changed.'
    created_namespace=0
    created_link=0
    rollback() {
        local result=$?
        if (( result != 0 )); then
            printf 'Setup failed; rolling back resources created by this invocation.\n' >&2
            if (( created_link )); then ip link delete "$CNC_HOST_LINK" || true; fi
            if (( created_namespace )); then ip netns delete "$CNC_NS" || true; fi
            rm -f -- "$CNC_MARKER"
        fi
    }
    umask 077
    (set -o noclobber; printf '%s\n' "$CNC_OWNER" > "$CNC_MARKER") || die 'Cannot create ownership marker.'
    trap rollback EXIT
    ip netns add "$CNC_NS"
    created_namespace=1
    ip link add "$CNC_HOST_LINK" type veth peer name "$CNC_SIM_LINK"
    created_link=1
    ip link set dev "$CNC_HOST_LINK" alias "$CNC_OWNER"
    ip link set dev "$CNC_SIM_LINK" alias "$CNC_OWNER"
    ip link set dev "$CNC_SIM_LINK" netns "$CNC_NS"
fi

ip address replace 192.168.50.1/24 dev "$CNC_HOST_LINK"
ip -n "$CNC_NS" address replace 192.168.50.2/24 dev "$CNC_SIM_LINK"
ip link set dev "$CNC_HOST_LINK" up
ip -n "$CNC_NS" link set dev "$CNC_SIM_LINK" up
ip -n "$CNC_NS" link set dev lo up
printf 'Ready: %s 192.168.50.1/24 <-> %s/%s 192.168.50.2/24; UDP 8888.\n' "$CNC_HOST_LINK" "$CNC_NS" "$CNC_SIM_LINK"
