#!/usr/bin/env -S bash -x
#
# Run an incast simulation parameter sweep with RWND tuning disabled over many numbers of connections.

set -eoux pipefail

# Enforce that there is a single argument.
if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <output directory>"
    exit 1
fi
out_dir="$1"
scratch_dir="$(realpath "$(dirname "$0")")"
for conns in 50 100 150 200 250 300 350 400 450 500 1000 2000; do
    "$scratch_dir/incast.sh" "$out_dir/incast_sweep_none" "$conns" "none" "0" 15
done
