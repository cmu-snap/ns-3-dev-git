#!/usr/bin/env -S bash -x
#
# Run the incast simulation with Meta's datacenter parameters.

set -eou pipefail

out_dir="$1"
dir_name="$2"
rm -rfv "${out_dir:?}/${dir_name:?}"
mkdir -p "$out_dir/$dir_name/"{log,pcap}

burstDurationMs=15
lineRateGbps=12.5
numSenders=100
metaQueueSizeBytes=1800000
metaQueueThresholdBytes=120000
bytesPerPacket=1500
# Convert burst duration to bytes per sender.
bytesPerSender="$(python -c "import math; print(math.ceil(($burstDurationMs / 1e3) * ($lineRateGbps * 1e9 / 8) / $numSenders))")"

queueSizePackets="$(python -c "import math; print(math.ceil($metaQueueSizeBytes / $bytesPerPacket))")"
thresholdPackets="$(python -c "import math; print(math.ceil($metaQueueThresholdBytes / $bytesPerPacket))")"

ns3_dir="$(realpath "$(dirname "$0")/..")"

"$ns3_dir/ns3" run "scratch/incast \
    --outputDirectory=$out_dir \
    --traceDirectory=$dir_name \
    --numSenders=$numSenders \
    --bytesPerSender=$bytesPerSender \
    --numBursts=3 \
    --delayPerLinkUs=5 \
    --jitterUs=20 \
    --smallLinkBandwidthMbps=12500 \
    --largeLinkBandwidthMbps=100000 \
    --cca=TcpDctcp \
    --smallQueueSizePackets=$queueSizePackets \
    --largeQueueSizePackets=$queueSizePackets \
    --smallQueueMinThresholdPackets=$thresholdPackets \
    --smallQueueMaxThresholdPackets=$thresholdPackets \
    --largeQueueMinThresholdPackets=$thresholdPackets \
    --largeQueueMaxThresholdPackets=$thresholdPackets"
