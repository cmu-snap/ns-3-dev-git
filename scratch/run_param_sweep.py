from concurrent.futures import ThreadPoolExecutor
import itertools
import json
import math
import os
import random
import shutil
import subprocess
import sys
import threading
import time


# Global variables for tracking progress
num_experiments: int = 0
num_successes: int = 0
num_failures: int = 0
failures_path: str = ""
progress_path: str = ""


# Constants for calculations
BASE_TO_MICRO: int = pow(10, 6)
BASE_TO_MILLI: int = pow(10, 3)
MEGA_TO_BASE: int = pow(10, 6)
NUM_BITS_PER_BYTE: int = 8
NUM_HOPS: int = 3


def getSetParams(experiment_config: dict[str, int], param: str) -> list[int]:
    return [
        experiment_config[f"MIN_{param}"],
        experiment_config[f"MAX_{param}"],
        experiment_config[f"NUM_{param}"],
    ]


def getLinearSet(experiment_config: dict[str, int], param: str) -> set[int]:
    start, end, num = getSetParams(experiment_config, param)
    step: int = math.ceil((end + 1 - start) / num)

    return set(range(start, end + 1, step))


def getExponentialSet(experiment_config: dict[str, int], param: str) -> set[int]:
    start, end, num = getSetParams(experiment_config, param)
    curr: float = 1.0 * start
    factor: float = math.log(math.ceil(end / curr)) / math.log(num)
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


def getLinkCapacityPps(linkBandwidthMbps: int, segmentSizeBytes: int) -> int:
    linkBandwidthBps: float = linkBandwidthMbps * MEGA_TO_BASE / NUM_BITS_PER_BYTE

    return int(linkBandwidthBps / segmentSizeBytes)


def getRttS(delayPerLinkUs: int) -> float:
    return float(NUM_HOPS * 2 * delayPerLinkUs / BASE_TO_MICRO)


class Params:
    def __init__(
        self,
        bytesPerSender: int,
        cca: str,
        delAckCount: int,
        delAckTimeoutMs: int,
        jitterUs: int,
        largeLinkBandwidthMbps: int,
        largeQueueThresholdPackets: int,
        largeQueueSizePackets: int,
        numSenders: int,
        segmentSizeBytes: int,
        smallLinkBandwidthMbps: int,
        smallQueueThresholdPackets: int,
        smallQueueSizePackets: int,
        trial: int,
        outputDirectory: str,
    ):
        self.time = int(time.time())
        self.bytesPerSender = bytesPerSender
        self.cca = cca
        self.delAckCount = delAckCount
        self.delAckTimeoutMs = delAckTimeoutMs
        self.jitterUs = jitterUs
        self.largeLinkBandwidthMbps = largeLinkBandwidthMbps
        self.largeQueueThresholdPackets = largeQueueThresholdPackets
        self.largeQueueSizePackets = largeQueueSizePackets
        self.numSenders = numSenders
        self.segmentSizeBytes = segmentSizeBytes
        self.smallLinkBandwidthMbps = smallLinkBandwidthMbps
        self.smallQueueThresholdPackets = smallQueueThresholdPackets
        self.smallQueueSizePackets = smallQueueSizePackets
        self.trial = trial
        self.outputDirectory = outputDirectory

        self.commandLineOptions = {
            "bytesPerSender": self.bytesPerSender,
            "cca": self.cca,
            "delAckCount": self.delAckCount,
            "delAckTimeoutMs": self.delAckTimeoutMs,
            "jitterUs": self.jitterUs,
            "largeLinkBandwidthMbps": self.largeLinkBandwidthMbps,
            "largeQueueMaxThresholdPackets": self.largeQueueThresholdPackets,
            "largeQueueMinThresholdPackets": self.largeQueueThresholdPackets,
            "largeQueueSizePackets": self.largeQueueSizePackets,
            "numSenders": self.numSenders,
            "segmentSizeBytes": self.segmentSizeBytes,
            "smallLinkBandwidthMbps": self.smallLinkBandwidthMbps,
            "smallQueueMaxThresholdPackets": self.smallQueueThresholdPackets,
            "smallQueueMinThresholdPackets": self.smallQueueThresholdPackets,
            "smallQueueSizePackets": self.smallQueueSizePackets,
            "traceDirectory": self.getTraceDirectory(),
            "outputDirectory": self.outputDirectory,
        }

        self.cwd_path: str = os.getcwd()
        self.trace_path: str = os.path.join(
            self.cwd_path, self.outputDirectory, self.getTraceDirectory()
        )
        self.log_path: str = os.path.join(self.trace_path, "log")
        self.pcap_path: str = os.path.join(self.trace_path, "pcap")

    def getTraceDirectory(self) -> str:
        elems: list[str] = [
            "traces",
            str(self.time),
            str(self.bytesPerSender),
            str(self.cca),
            str(self.delAckCount),
            str(self.delAckTimeoutMs),
            str(self.jitterUs),
            str(self.largeLinkBandwidthMbps),
            str(self.largeQueueThresholdPackets),
            str(self.largeQueueThresholdPackets),
            str(self.largeQueueSizePackets),
            str(self.numSenders),
            str(self.segmentSizeBytes),
            str(self.smallLinkBandwidthMbps),
            str(self.smallQueueThresholdPackets),
            str(self.smallQueueThresholdPackets),
            str(self.smallQueueSizePackets),
            str(self.trial),
        ]

        return "_".join(elems) + "/"

    def createDirectories(self):
        if not os.path.exists(self.trace_path):
            os.makedirs(self.trace_path)

        if not os.path.exists(self.log_path):
            os.makedirs(self.log_path)

        if not os.path.exists(self.pcap_path):
            os.makedirs(self.pcap_path)

    def deleteDirectories(self):
        if os.path.exists(self.trace_path):
            shutil.rmtree(self.trace_path)

    def writeConfig(self):
        with open(
            file=self.trace_path + "config.json", mode="w", encoding="ascii"
        ) as config_file:
            config_dict = {}

            for k, v in self.commandLineOptions.items():
                if "Directory" not in k:
                    config_dict[k] = v

            config_file.write(json.dumps(config_dict, indent=4))

    def getCommandLineOption(self, option: str) -> str:
        return f"--{option}={self.commandLineOptions[option]}"

    def getRunCommand(self) -> str:
        run_command_args: list[str] = ["build/scratch/ns3-dev-incast-default"]

        for option in self.commandLineOptions:
            run_command_args.append(self.getCommandLineOption(option))

        return " ".join(run_command_args)

    def run(self) -> int:
        with open(
            file=self.trace_path + "stderr.txt", mode="w", encoding="ascii"
        ) as stderr_file:
            with open(
                file=self.trace_path + "stdout.txt", mode="w", encoding="ascii"
            ) as stdout_file:
                return subprocess.run(
                    args=self.getRunCommand(),
                    stdout=stdout_file,
                    stderr=stderr_file,
                    shell=True,
                    check=False,
                ).returncode


def main():
    # Read the parameter configurations from a file
    if len(sys.argv) != 2:
        print("Usage: python scratch/run_param_sweep.py <experiment_config_file>")
        exit(1)

    with open(file=sys.argv[1], mode="r", encoding="ascii") as experiment_config_file:
        experiment_config = json.load(experiment_config_file)

    # Create sets for all parameter values
    burstDurationMs_set: set[int] = getLinearSet(
        experiment_config,
        "BURST_DURATION_MS",
    )
    delayPerLinkUs_set: set[int] = getLinearSet(
        experiment_config,
        "DELAY_PER_LINK_US",
    )
    delAckCount_set: set[int] = getLinearSet(
        experiment_config,
        "DEL_ACK_COUNT",
    )
    delAckTimeoutMs_set: set[int] = getLinearSet(
        experiment_config,
        "DEL_ACK_TIMEOUT_MS",
    )
    largeLinkBandwidthMbps_set: set[int] = getLinearSet(
        experiment_config,
        "LARGE_LINK_BANDWIDTH_MBPS",
    )
    numSenders_set: set[int] = getLinearSet(
        experiment_config,
        "NUM_SENDERS",
    )
    queueSizeFactor_set: set[int] = getExponentialSet(
        experiment_config,
        "QUEUE_SIZE_FACTOR",
    )
    segmentSizeBytes_set: set[int] = getLinearSet(
        experiment_config,
        "SEGMENT_SIZE_BYTES",
    )
    smallLinkBandwidthMbps_set: set[int] = getLinearSet(
        experiment_config,
        "SMALL_LINK_BANDWIDTH_MBPS",
    )

    # Combine all parameters
    params_tuples = list(
        itertools.product(
            burstDurationMs_set,
            delAckCount_set,
            delAckTimeoutMs_set,
            delayPerLinkUs_set,
            largeLinkBandwidthMbps_set,
            numSenders_set,
            queueSizeFactor_set,
            segmentSizeBytes_set,
            smallLinkBandwidthMbps_set,
        )
    )

    num_samples = min(len(params_tuples), experiment_config["NUM_SAMPLES"])
    params_sample = random.sample(list(params_tuples), num_samples)
    params_set = set()

    for (
        burstDurationMs,
        delAckCount,
        delAckTimeoutMs,
        delayPerLinkUs,
        largeLinkBandwidthMbps,
        numSenders,
        queueSizeFactor,
        segmentSizeBytes,
        smallLinkBandwidthMbps,
    ) in params_sample:
        cca: str = experiment_config["CCA"]
        jitterUs: int = getJitterUs(delayPerLinkUs)
        bytesPerSender: int = getBytesPerSender(
            burstDurationMs,
            numSenders,
            smallLinkBandwidthMbps,
        )

        largeLinkCapacityPps: int = getLinkCapacityPps(
            largeLinkBandwidthMbps,
            segmentSizeBytes,
        )
        smallLinkCapacityPps: int = getLinkCapacityPps(
            smallLinkBandwidthMbps,
            segmentSizeBytes,
        )
        rttS: float = getRttS(delayPerLinkUs)

        largeQueueThresholdPackets: int = math.ceil(largeLinkCapacityPps * rttS / 7)
        largeQueueSizePackets: int = int(queueSizeFactor * smallLinkCapacityPps * rttS)

        smallQueueThresholdPackets: int = math.ceil(smallLinkCapacityPps * rttS / 7)
        smallQueueSizePackets: int = int(queueSizeFactor * smallLinkCapacityPps * rttS)

        for trial in range(1, experiment_config["NUM_TRIALS"] + 1):
            params_set.add(
                Params(
                    bytesPerSender,
                    cca,
                    delAckCount,
                    delAckTimeoutMs,
                    jitterUs,
                    largeLinkBandwidthMbps,
                    largeQueueThresholdPackets,
                    largeQueueSizePackets,
                    numSenders,
                    segmentSizeBytes,
                    smallLinkBandwidthMbps,
                    smallQueueThresholdPackets,
                    smallQueueSizePackets,
                    trial,
                    experiment_config["OUTPUT_DIRECTORY"],
                )
            )

    # Build NS3
    subprocess.call("./ns3 clean", shell=True)
    subprocess.call("./ns3 configure", shell=True)
    subprocess.call("./ns3 build scratch/incast.cc", shell=True)

    # Track progress
    run_lock = threading.RLock()
    cwd_path: str = os.getcwd()

    global num_experiments, num_successes, num_failures
    num_experiments = len(params_set)

    global failures_path, progress_path
    failures_path = os.path.join(
        cwd_path,
        experiment_config["OUTPUT_DIRECTORY"],
        "failures.txt",
    )
    progress_path = os.path.join(
        cwd_path,
        experiment_config["OUTPUT_DIRECTORY"],
        "progress.txt",
    )

    # Run an experiment
    def run(params: Params):
        global failures_path
        global progress_path
        global num_experiments
        global num_successes
        global num_failures

        params.createDirectories()
        params.writeConfig()
        returncode: int = params.run()

        with run_lock:
            if returncode != 0:
                num_failures += 1
                with open(
                    file=failures_path, mode="w", encoding="ascii"
                ) as failures_file:
                    failures_file.write(params.getRunCommand())
                    failures_file.write("\n")
            else:
                num_successes += 1

            with open(file=progress_path, mode="w", encoding="ascii") as progress_file:
                progress_file.write(f"num_successes: {num_successes}\n")
                progress_file.write(f"num_failures: {num_failures}\n")
                progress_file.write(f"num_experiments: {num_experiments}")

    # Run all experiments in parallel
    with ThreadPoolExecutor(experiment_config["NUM_PROCESSES"]) as executor:
        executor.map(run, params_set)


if __name__ == "__main__":
    main()
