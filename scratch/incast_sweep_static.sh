#!/usr/bin/env -S bash -x
#
# Run an incast simulation parameter sweep with static RWND tuning enabled over many RWND clamp values.

set -oux pipefail

# Enforce that there is a single argument.
if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <output directory>"
    exit 1
fi
out_dir="$1"
scratch_dir="$(realpath "$(dirname "$0")")"
for rwnd in 4096 8192 16384 32768 65536; do
    "$scratch_dir/incast.sh" "$out_dir/incast_sweep_static" 200 "static" "$rwnd"
done
