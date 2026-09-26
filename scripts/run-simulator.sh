#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
PROJECT_DIR=$(cd -- "$SCRIPT_DIR/.." && pwd)
# shellcheck source=network-common.sh
source "$SCRIPT_DIR/network-common.sh"
require_root "$@"
check_marker
check_owned_links
ns_exists && host_exists && sim_exists || die 'Run setup-veth.sh first.'
binary=${CNC_SIM_BINARY:-"$PROJECT_DIR/build/cnc-sim"}
[[ -x $binary ]] || die "Build $binary first."
printf 'Starting %s in namespace %s.\n' "$binary" "$CNC_NS"
exec ip netns exec "$CNC_NS" "$binary" "$@"
