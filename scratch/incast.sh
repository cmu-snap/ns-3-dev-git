#!/usr/bin/env -S bash -x
#
# Run the incast simulation.

set -eoux pipefail

# Enforce that there are four arguments.
if [ "$#" -ne 6 ]; then
    echo "Usage: $0 <output directory> <num senders> <RWND policy> <static RWND clamp> <dur_ms> <skip build>"
    exit 1
fi

skip_build="$6"
burstDurationMs="$5"
numBursts=6
# Note: Retransmits during slow start begin at 214 connections. < Is that true?
numBurstSenders="$2" # $((100 + 1))
numBackgroundSenders=0
cca="TcpDctcp"
nicRateMbps=100000
uplinkRateMbps=400000
# nicRateMbps=10000
# uplinkRateMbps=100000
delayPerLinkUs=5
jitterUs=100
queueSizeBytes=2000000
bytesPerPacket=1500
queueSizePackets="$(python -c "import math; print(math.ceil($queueSizeBytes / $bytesPerPacket))")"
# Convert burst duration to bytes per sender.
bytesPerBurstSender="$(python -c "import math; print(math.ceil(($burstDurationMs / 1e3) * ($nicRateMbps * 1e6 / 8) / $numBurstSenders))")"
icwnd=10
firstFlowOffsetMs=0
rwndStrategy="$3"
staticRwndBytes="$4"
rwndScheduleMaxConns=20
delAckCount=1
delAckTimeoutMs=0

# Pick the right ECN marking threshold.
nicRatePps="$(python -c "print($nicRateMbps * 1e6 / 8 / $bytesPerPacket)")"
rttSec="$(python -c "print($delayPerLinkUs * 6 / 1e6)")"
# recommendedThresholdPackets="$(python -c "print($nicRatePps * $rttSec / 7)")"
# thresholdPackets="$(python -c "import math; print(math.ceil($recommendedThresholdPackets))")"
# ECN marking threshold:
queueThresholdBytes=120000
thresholdPackets="$(python -c "import math; print(math.ceil($queueThresholdBytes / $bytesPerPacket))")"

# Pick the right DCTCP G parameter.
recommendedG="$(python -c "import math; print(1.386 / math.sqrt(2 * ($nicRatePps * $rttSec + $thresholdPackets)))")"
dctcpShiftGExpRaw="$(python -c "import math; print(math.log(1 / $recommendedG, 2))")"
dctcpShiftGExp="$(python -c "import math; print(math.ceil($dctcpShiftGExpRaw))")"
# DCTCP G:
# dctcpShiftGExp=4
# More reactive:
# dctcpShiftGExp=2
dctcpShiftG="$(python -c "print(1 / 2**$dctcpShiftGExp)")"

# Get git branch
ns3_dir="$(realpath "$(dirname "$0")/..")"
pushd "$ns3_dir"
git_branch="$(git branch --show-current)"
popd
if [ -z "$git_branch" ]; then
    printf "Error: Unable to determine Git branch of simulator!\n"
    exit 1
fi

out_dir="$1/$git_branch"
dir_name="${burstDurationMs}ms-$numBurstSenders-$numBackgroundSenders-$numBursts-$cca-${nicRateMbps}mbps-${queueSizeBytes}B-${icwnd}icwnd-${firstFlowOffsetMs}offset-$rwndStrategy-rwnd${staticRwndBytes}B-${rwndScheduleMaxConns}tokens-${dctcpShiftGExp}g-${thresholdPackets}ecn-${delAckCount}_${delAckTimeoutMs}da"
# We will store in-progress results in a tmpfs and move them to the final
# location later.
tmpfs="$out_dir"/tmpfs/${dir_name}_tmpfs
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
sudo mount -v -t tmpfs none "$tmpfs" -o size=5G

# Clean up previous results.
rm -rfv "${tmpfs:?}/"* "${results_dir:?}"
# mkdir -p "$tmpfs/"{logs,pcap}

mkdir -pv "$tmpfs/$dir_name/"{logs,pcap}

# Run simulation.
if [ "$skip_build" != "yes" ]; then
    # "$ns3_dir/ns3" configure --build-profile=debug
    "$ns3_dir/ns3" configure --build-profile=default
    "$ns3_dir/ns3" build "scratch/incast"
fi
# time "$ns3_dir"/build/scratch/ns3-dev-incast-debug \
time "$ns3_dir"/build/scratch/ns3-dev-incast-default \
    --outputDirectory="$tmpfs/" \
    --traceDirectory="$dir_name" \
    --numBurstSenders="$numBurstSenders" \
    --numBackgroundSenders="$numBackgroundSenders" \
    --bytesPerBurstSender="$bytesPerBurstSender" \
    --numBursts="$numBursts" \
    --delayPerLinkUs="$delayPerLinkUs" \
    --jitterUs="$jitterUs" \
    --smallLinkBandwidthMbps="$nicRateMbps" \
    --largeBurstLinkBandwidthMbps="$uplinkRateMbps" \
    --cca="$cca" \
    --smallQueueSizePackets="$queueSizePackets" \
    --largeBurstQueueSizePackets="$queueSizePackets" \
    --smallQueueMinThresholdPackets="$thresholdPackets" \
    --smallQueueMaxThresholdPackets="$thresholdPackets" \
    --largeBurstQueueMinThresholdPackets="$thresholdPackets" \
    --largeBurstQueueMaxThresholdPackets="$thresholdPackets" \
    --initialCwnd="$icwnd" \
    --firstFlowOffsetMs="$firstFlowOffsetMs" \
    --rwndStrategy="$rwndStrategy" \
    --staticRwndBytes="$staticRwndBytes" \
    --rwndScheduleMaxConns="$rwndScheduleMaxConns" \
    --dctcpShiftG="$dctcpShiftG" \
    --delAckCount="$delAckCount" \
    --delAckTimeoutMs="$delAckTimeoutMs"
#  \
# --enableSenderPcap

# Move results to results_dir.
mkdir -pv "$results_dir"
mv -f "$tmpfs/$dir_name/"* "$results_dir/"

# Clean up tmpfs.
rm -rf "${tmpfs:?}"/*
# umount sometimes claims that the mountpoint is busy, so sleep for a bit.
sleep 1
sudo umount -v "$tmpfs"
rmdir -v "$tmpfs"

echo "Results in: $results_dir"
