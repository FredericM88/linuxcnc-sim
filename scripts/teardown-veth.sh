#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=network-common.sh
source "$SCRIPT_DIR/network-common.sh"
require_root "$@"
[[ $# -eq 0 ]] || die 'This script takes no arguments.'

if [[ ! -e $CNC_MARKER && ! -L $CNC_MARKER ]]; then
    if host_exists || ns_exists || ip link show dev "$CNC_SIM_LINK" >/dev/null 2>&1; then
        die 'Resources exist without our ownership marker; refusing to delete them.'
    fi
    printf 'No cnc-sim network setup exists; nothing to remove.\n'
    exit 0
fi
check_marker
check_owned_links
if ns_exists; then
    pids=$(ip netns pids "$CNC_NS")
    [[ -z $pids ]] || die "Namespace still has processes ($pids). Stop the simulator first."
fi
if host_exists; then ip link delete "$CNC_HOST_LINK"; fi
if ns_exists; then ip netns delete "$CNC_NS"; fi
rm -- "$CNC_MARKER"
printf 'Removed cnc-sim namespace, veth pair, addresses and temporary ownership marker.\n'
