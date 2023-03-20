#!/usr/bin/env -S bash -x
#
# Run the incast simulation with Meta's datacenter parameters.

set -eou pipefail

queueSizePackets="$(python -c "import math; print(math.ceil(1800000 / 1500))")"
thresholdPackets="$(python -c "import math; print(math.ceil(120000 / 1500))")"

burstDurationMs=8
lineRateGbps=12.5
numSenders=100
# Convert burst duration to bytes per sender.
bytesPerSender="$(python -c "import math; print(math.ceil(($burstDurationMs / 1e3) * ($lineRateGbps * 1e9 / 8) / $numSenders))")"

"$(realpath "$(dirname "$0")/..")/ns3" run "scratch/incast \
    --numSenders=$numSenders \
    --bytesPerSender=$bytesPerSender \
    --numBursts=3 \
    --perLinkDelayUs=5 \
    --jitterUs=20 \
    --smallBandwidthMbps=12500 \
    --largeBandwidthMbps=100000 \
    --cca=TcpDctcp \
    --smallQueueSize=$queueSizePackets \
    --largeQueueSize=$queueSizePackets \
    --smallMinThreshold=$thresholdPackets \
    --smallMaxThreshold=$thresholdPackets \
    --largeMinThreshold=$thresholdPackets \
    --largeMaxThreshold=$thresholdPackets"
