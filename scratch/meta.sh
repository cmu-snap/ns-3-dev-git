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
numBursts=5
numSenders=214 # $((5 + 1))
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
firstFlowOffsetMs=0 # 10

out_dir="$1"
dir_name="${burstDurationMs}ms-$numSenders-$numBursts-$cca-${icwnd}icwnd-${firstFlowOffsetMs}offset"
results_dir="$out_dir/$dir_name"
rm -rfv "${results_dir:?}"
mkdir -p "$results_dir/"{log,pcap}

ns3_dir="$(realpath "$(dirname "$0")/..")"

"$ns3_dir/ns3" build "scratch/incast"

"$ns3_dir"/build/scratch/ns3-dev-incast-default \
    --outputDirectory="$out_dir/" \
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
    --firstFlowOffsetMs=$firstFlowOffsetMs

echo "Results in: $results_dir"
