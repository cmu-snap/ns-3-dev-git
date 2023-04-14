#!/usr/bin/env -S bash -x
#
# Run the incast simulation with Meta's datacenter parameters.

set -eou pipefail

# Enforce that there is a single argument.
if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <output directory>"
    exit 1
fi

burstDurationMs=15
numBursts=3
# Note: Retransmits during slow start begin at 214 connections. < Is that true?
numSenders=200 # $((200 + 1))
cca="TcpDctcp"
nicRateMbps=12500
uplinkRateMbps=100000
delayPerLinkUs=5
jitterUs=100
metaQueueSizeBytes=1800000
metaQueueThresholdBytes=120000
bytesPerPacket=1500
queueSizePackets="$(python -c "import math; print(math.ceil($metaQueueSizeBytes / $bytesPerPacket))")"
thresholdPackets="$(python -c "import math; print(math.ceil($metaQueueThresholdBytes / $bytesPerPacket))")"
# Convert burst duration to bytes per sender.
bytesPerSender="$(python -c "import math; print(math.ceil(($burstDurationMs / 1e3) * ($nicRateMbps * 1e6 / 8) / $numSenders))")"
icwnd=10
firstFlowOffsetMs=0
rwndStrategy="scheduled"
staticRwndBytes=1000000
rwndScheduleMaxConns=20

out_dir="$1"
dir_name="${burstDurationMs}ms-$numSenders-$numBursts-$cca-${icwnd}icwnd-${firstFlowOffsetMs}offset-$rwndStrategy-rwnd${staticRwndBytes}B-${rwndScheduleMaxConns}tokens"
# We will store in-progress results in a tmpfs and move them to the final
# location later.
tmpfs="$out_dir"/tmpfs
tmpfs_results_dir="$tmpfs/$dir_name"
results_dir="$out_dir/$dir_name"

# If tmpfs exists, then clean it up.
if [ -d "$tmpfs" ]; then
    rm -rf "${tmpfs:?}"/*
    # Check if $tmpfs is a mountpoint
    if mountpoint -q "$tmpfs"; then
        sudo umount -v "$tmpfs"
    fi
    rmdir -v "$tmpfs"
fi

# Prepare tmpfs.
rm -rf "$tmpfs"
mkdir -pv "$tmpfs"
sudo mount -v -t tmpfs none "$tmpfs" -o size=10G

# Clean up previous results.
rm -rfv "${tmpfs_results_dir:?}" "${results_dir:?}"
mkdir -p "$tmpfs_results_dir/"{log,pcap}

# Run simulation.
ns3_dir="$(realpath "$(dirname "$0")/..")"
"$ns3_dir/ns3" build "scratch/incast"
"$ns3_dir"/build/scratch/ns3-dev-incast-default \
    --outputDirectory="$tmpfs/" \
    --traceDirectory="$dir_name" \
    --numSenders=$numSenders \
    --bytesPerSender="$bytesPerSender" \
    --numBursts=$numBursts \
    --delayPerLinkUs=$delayPerLinkUs \
    --jitterUs=$jitterUs \
    --smallLinkBandwidthMbps=$nicRateMbps \
    --largeLinkBandwidthMbps=$uplinkRateMbps \
    --cca=$cca \
    --smallQueueSizePackets="$queueSizePackets" \
    --largeQueueSizePackets="$queueSizePackets" \
    --smallQueueMinThresholdPackets="$thresholdPackets" \
    --smallQueueMaxThresholdPackets="$thresholdPackets" \
    --largeQueueMinThresholdPackets="$thresholdPackets" \
    --largeQueueMaxThresholdPackets="$thresholdPackets" \
    --initialCwnd=$icwnd \
    --firstFlowOffsetMs=$firstFlowOffsetMs \
    --rwndStrategy=$rwndStrategy \
    --staticRwndBytes=$staticRwndBytes \
    --rwndScheduleMaxConns=$rwndScheduleMaxConns

# Move results to out_dir.
mkdir -pv "$out_dir"
mv -f "$tmpfs_results_dir" "$results_dir"

# Clean up tmpfs.
rm -rf "${tmpfs:?}"/*
# umount sometimes claims that the mountpoint is busy, so sleep for a bit.
sleep 1
sudo umount -v "$tmpfs"
rmdir -v "$tmpfs"

echo "Results in: $results_dir"
