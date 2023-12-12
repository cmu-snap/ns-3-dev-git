#!/usr/bin/env -S bash -x
#
# Run all incast simulation parameter sweeps.

set -oux pipefail

# Enforce that there is a single argument.
if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <output directory>"
    exit 1
fi
out_dir="$1"
dur_ms=15

# Build once, instead of in each instance.
scratch_dir="$(realpath "$(dirname "$0")")"
ns3_dir="$scratch_dir/.."
"$ns3_dir/ns3" configure --build-profile=default
"$ns3_dir/ns3" build "scratch/incast"

connss=(50 100 150 200 500 1000)
rwnds=(2048 3072 4096 5120 6144 7168 8192 9216 10240 11264 12288 13312 14336)
for conns in "${connss[@]}"; do
    parallel --line-buffer "$scratch_dir/incast.sh" "$out_dir/sweep" "$conns" "static" {} "$dur_ms" yes ::: "${rwnds[@]}"
done

connss=(50 100 150 200 250 300 350 400 450 500 1000)
parallel --line-buffer "$scratch_dir/incast.sh" "$out_dir/sweep" {} "none" "0" "$dur_ms" yes ::: "${connss[@]}"

connss=(500 1000)
parallel --line-buffer "$scratch_dir/incast.sh" "$out_dir/sweep" {} "scheduled" "65536" "$dur_ms" yes ::: "${connss[@]}"