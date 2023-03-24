import io
import itertools
import json
import math
import multiprocessing
import os
import subprocess
import shutil
import time

NUM_PROCESSES: int = 30
OUTPUT_DIRECTORY: str = "traces/"

# Parameter sweeping boundaries
MIN_BURST_DURATION_MS: int = 5
MAX_BURST_DURATION_MS: int = 50
STEP_BURST_DURATION_MS: int = math.ceil(
    (MAX_BURST_DURATION_MS + 1 - MIN_BURST_DURATION_MS) / 10
)

MIN_DELAY_PER_LINK_US: int = 2
MAX_DELAY_PER_LINK_US: int = 25
STEP_DELAY_PER_LINK_US: int = math.ceil(
    (MAX_DELAY_PER_LINK_US + 1 - MIN_DELAY_PER_LINK_US) / 10
)

# MIN_LARGE_LINK_BANDWIDTH_MBPS: int = 5000
# MAX_LARGE_LINK_BANDWIDTH_MBPS: int = 100000
# STEP_LARGE_LINK_BANDWIDTH_MBPS: int = 1

MIN_LARGE_QUEUE_THRESHOLD_PACKETS: int = 80
MAX_LARGE_QUEUE_THRESHOLD_PACKETS: int = 80
STEP_LARGE_QUEUE_THRESHOLD_PACKETS: int = 1

MIN_LARGE_QUEUE_SIZE_PACKETS: int = 1200
MAX_LARGE_QUEUE_SIZE_PACKETS: int = 1200
STEP_LARGE_QUEUE_SIZE_PACKETS: int = 1

MIN_NUM_SENDERS: int = 100
MAX_NUM_SENDERS: int = 1000
STEP_NUM_SENDERS: int = math.ceil((MAX_NUM_SENDERS + 1 - MIN_NUM_SENDERS) / 10)

MIN_SMALL_LINK_BANDWIDTH_MBPS: int = 50000
MAX_SMALL_LINK_BANDWIDTH_MBPS: int = 800000
NUM_SMALL_LINK_BANDWIDTH_MBPS: int = 10

MIN_SMALL_QUEUE_THRESHOLD_PACKETS: int = 80
MAX_SMALL_QUEUE_THRESHOLD_PACKETS: int = 80
STEP_SMALL_QUEUE_THRESHOLD_PACKETS: int = 1

MIN_SMALL_QUEUE_SIZE_PACKETS: int = 1200
MAX_SMALL_QUEUE_SIZE_PACKETS: int = 1200
STEP_SMALL_QUEUE_SIZE_PACKETS: int = 1

# Constants for calculations
BASE_TO_MILLI: int = pow(10, 3)
MEGA_TO_BASE: int = pow(10, 6)
NUM_BITS_PER_BYTE: int = 8


def getExponentialSet(start: int, end: int, num: int) -> set[int]:
    curr: float = 1.0 * start
    factor: float = math.log(end / curr) / math.log(num)
    output: set[int] = {int(curr)}

    while curr < end:
        curr *= factor
        output.add(int(curr))

    return output


def getBytesPerSender(
    burstDurationMs: int,
    numSenders: int,
    smallLinkBandwidthMbps: int,
) -> int:
    burstDurationS: float = burstDurationMs / BASE_TO_MILLI
    smallLinkBandwidthBps: float = (
        smallLinkBandwidthMbps * MEGA_TO_BASE / NUM_BITS_PER_BYTE
    )
    totalBytes: float = burstDurationS * smallLinkBandwidthBps

    return int(totalBytes / numSenders)


def getJitterUs(delayPerLinkUs: int) -> int:
    return int(delayPerLinkUs / 2)


class Params:
    def __init__(
        self,
        bytesPerSender: int,
        jitterUs: int,
        largeLinkBandwidthMbps: int,
        largeQueueThresholdPackets: int,
        largeQueueSizePackets: int,
        numSenders: int,
        smallLinkBandwidthMbps: int,
        smallQueueThresholdPackets: int,
        smallQueueSizePackets: int,
    ):
        self.time = int(time.time())
        self.bytesPerSender = bytesPerSender
        self.jitterUs = jitterUs
        self.largeLinkBandwidthMbps = largeLinkBandwidthMbps
        self.largeQueueThresholdPackets = largeQueueThresholdPackets
        self.largeQueueSizePackets = largeQueueSizePackets
        self.numSenders = numSenders
        self.smallLinkBandwidthMbps = smallLinkBandwidthMbps
        self.smallQueueThresholdPackets = smallQueueThresholdPackets
        self.smallQueueSizePackets = smallQueueSizePackets
        self.commandLineOptions = {
            "bytesPerSender": self.bytesPerSender,
            "jitterUs": self.jitterUs,
            "largeLinkBandwidthMbps": self.largeLinkBandwidthMbps,
            "largeQueueMaxThresholdPackets": self.largeQueueThresholdPackets,
            "largeQueueMinThresholdPackets": self.largeQueueThresholdPackets,
            "largeQueueSizePackets": self.largeQueueSizePackets,
            "numSenders": self.numSenders,
            "smallLinkBandwidthMbps": self.smallLinkBandwidthMbps,
            "smallQueueMaxThresholdPackets": self.smallQueueThresholdPackets,
            "smallQueueMinThresholdPackets": self.smallQueueThresholdPackets,
            "smallQueueSizePackets": self.smallQueueSizePackets,
            "traceDirectory": self.getTraceDirectory(),
            "outputDirectory": OUTPUT_DIRECTORY,
        }

    def getTraceDirectory(self) -> str:
        elems: list[str] = [
            "traces",
            str(self.time),
            str(self.bytesPerSender),
            str(self.jitterUs),
            str(self.largeLinkBandwidthMbps),
            str(self.largeQueueThresholdPackets),
            str(self.largeQueueThresholdPackets),
            str(self.largeQueueSizePackets),
            str(self.numSenders),
            str(self.smallLinkBandwidthMbps),
            str(self.smallQueueThresholdPackets),
            str(self.smallQueueThresholdPackets),
            str(self.smallQueueSizePackets),
        ]

        return "_".join(elems) + "/"

    def createDirectories(self):
        cwd_path: str = os.getcwd()

        self.trace_path: str = os.path.join(
            cwd_path, OUTPUT_DIRECTORY, self.getTraceDirectory()
        )
        if not os.path.exists(self.trace_path):
            os.makedirs(self.trace_path)

        self.log_path: str = os.path.join(self.trace_path, "log")
        if not os.path.exists(self.log_path):
            os.makedirs(self.log_path)

        self.pcap_path: str = os.path.join(self.trace_path, "pcap")
        if not os.path.exists(self.pcap_path):
            os.makedirs(self.pcap_path)

    def deleteDirectories(self):
        if os.path.exists(self.trace_path):
            shutil.rmtree(self.trace_path)

    def writeConfig(self):
        with open(self.trace_path + "config.txt", "w") as f:
            f.write(json.dumps(self.commandLineOptions, indent=4))

    def getCommandLineOption(self, option: str) -> str:
        return f"--{option}={self.commandLineOptions[option]}"

    def getRunCommand(self) -> str:
        run_command_args: list[str] = ["build/scratch/ns3-dev-incast-default"]

        for option in self.commandLineOptions:
            run_command_args.append(self.getCommandLineOption(option))

        return " ".join(run_command_args)

    def run(self):
        with open(self.trace_path + "stderr.txt", "w") as stderr_file:
            with open(self.trace_path + "stdout.txt", "w") as stdout_file:
                if (
                    subprocess.run(
                        self.getRunCommand(),
                        stdout=stdout_file,
                        stderr=stderr_file,
                        shell=True,
                    ).returncode
                    != 0
                ):
                    self.deleteDirectories()


if __name__ == "__main__":
    # Create sets for all parameter values
    burstDurationMs_set: set[int] = set(
        range(
            MIN_BURST_DURATION_MS,
            MAX_BURST_DURATION_MS + 1,
            STEP_BURST_DURATION_MS,
        )
    )
    delayPerLinkUs_set: set[int] = set(
        range(
            MIN_DELAY_PER_LINK_US,
            MAX_DELAY_PER_LINK_US + 1,
            STEP_DELAY_PER_LINK_US,
        )
    )
    numSenders_set: set[int] = set(
        range(
            MIN_NUM_SENDERS,
            MAX_NUM_SENDERS + 1,
            STEP_NUM_SENDERS,
        )
    )
    largeQueueThresholdPackets_set: set[int] = set(
        range(
            MIN_LARGE_QUEUE_THRESHOLD_PACKETS,
            MAX_LARGE_QUEUE_THRESHOLD_PACKETS + 1,
            STEP_LARGE_QUEUE_THRESHOLD_PACKETS,
        )
    )
    largeQueueSizePackets_set: set[int] = set(
        range(
            MIN_LARGE_QUEUE_SIZE_PACKETS,
            MAX_LARGE_QUEUE_SIZE_PACKETS + 1,
            STEP_LARGE_QUEUE_SIZE_PACKETS,
        )
    )
    smallLinkBandwidthMbps_set: set[int] = getExponentialSet(
        MIN_SMALL_LINK_BANDWIDTH_MBPS,
        MAX_SMALL_LINK_BANDWIDTH_MBPS,
        NUM_SMALL_LINK_BANDWIDTH_MBPS,
    )
    smallQueueThresholdPackets_set: set[int] = set(
        range(
            MIN_SMALL_QUEUE_THRESHOLD_PACKETS,
            MAX_SMALL_QUEUE_THRESHOLD_PACKETS + 1,
            STEP_LARGE_QUEUE_SIZE_PACKETS,
        )
    )
    smallQueueSizePackets_set: set[int] = set(
        range(
            MIN_SMALL_QUEUE_SIZE_PACKETS,
            MAX_SMALL_QUEUE_SIZE_PACKETS + 1,
            STEP_SMALL_QUEUE_SIZE_PACKETS,
        )
    )

    # Combine all parameters
    params_tuples = list(
        itertools.product(
            burstDurationMs_set,
            delayPerLinkUs_set,
            largeQueueThresholdPackets_set,
            largeQueueSizePackets_set,
            numSenders_set,
            smallLinkBandwidthMbps_set,
            smallQueueThresholdPackets_set,
            smallQueueSizePackets_set,
        )
    )

    params_set = set()

    for (
        burstDurationMs,
        delayPerLinkUs,
        largeQueueThresholdPackets,
        largeQueueSizePackets,
        numSenders,
        smallLinkBandwidthMbps,
        smallQueueThresholdPackets,
        smallQueueSizePackets,
    ) in params_tuples:
        jitterUs: int = getJitterUs(delayPerLinkUs)
        largeLinkBandwidthMbps: int = 10 * smallLinkBandwidthMbps
        bytesPerSender: int = getBytesPerSender(
            burstDurationMs,
            numSenders,
            smallLinkBandwidthMbps,
        )
        params_set.add(
            Params(
                bytesPerSender,
                jitterUs,
                largeLinkBandwidthMbps,
                largeQueueThresholdPackets,
                largeQueueSizePackets,
                numSenders,
                smallLinkBandwidthMbps,
                smallQueueThresholdPackets,
                smallQueueSizePackets,
            )
        )

    # Build NS3
    subprocess.call("./ns3 clean", shell=True)
    subprocess.call("./ns3 configure", shell=True)
    subprocess.call("./ns3 build scratch/incast.cc", shell=True)

    # Run an experiment
    def run(params: Params):
        params.createDirectories()
        params.writeConfig()
        params.run()

    # Run all experiments in parallel
    with multiprocessing.Pool(NUM_PROCESSES) as p:
        p.map(run, params_set)
