#!/usr/bin/env -S bash -x
#
# Run all incast simulation parameter sweeps.

set -eoux pipefail

# Enforce that there is a single argument.
if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <output directory>"
    exit 1
fi
out_dir="$1"

# Build once, instead of in each instance.
scratch_dir="$(realpath "$(dirname "$0")")"
ns3_dir="$scratch_dir/.."
"$ns3_dir/ns3" configure --build-profile=default
"$ns3_dir/ns3" build "scratch/incast"

# 15ms bursts
dur_ms=15
connss=(100 500 1000)
parallel --line-buffer "$scratch_dir/incast.sh" "$out_dir/sweep" {} "none" "0" "$dur_ms" yes ::: "${connss[@]}"

# 2ms bursts
dur_ms=2
connss=(100)
parallel --line-buffer "$scratch_dir/incast.sh" "$out_dir/sweep" {} "none" "0" "$dur_ms" yes ::: "${connss[@]}"
