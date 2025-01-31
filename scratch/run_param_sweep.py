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


def getAllSet(experiment_config: dict[str, int], param: str) -> set[int]:
    all_key: str = f"ALL_{param}"

    if all_key in experiment_config:
        return set(experiment_config[all_key])

    return set()


def getSetParams(experiment_config: dict[str, int], param: str) -> list[int]:
    min_key: str = f"MIN_{param}"
    max_key: str = f"MAX_{param}"
    num_key: str = f"NUM_{param}"

    if (
        min_key in experiment_config
        and max_key in experiment_config
        and num_key in experiment_config
    ):
        return [
            experiment_config[min_key],
            experiment_config[max_key],
            experiment_config[num_key],
        ]

    return []


def getLinearSet(experiment_config: dict[str, int], param: str) -> set[int]:
    all_set: set[int] = getAllSet(experiment_config, param)

    if len(all_set) > 0:
        return all_set

    set_params: list[int] = getSetParams(experiment_config, param)

    if len(set_params) == 0:
        return {None}

    start, end, num = set_params
    step: int = math.ceil((end + 1 - start) / num)

    return set(range(start, end + 1, step))


def getExponentialSet(experiment_config: dict[str, int], param: str) -> set[int]:
    all_set: set[int] = getAllSet(experiment_config, param)

    if len(all_set) > 0:
        return all_set

    set_params: list[int] = getSetParams(experiment_config, param)

    if len(set_params) == 0:
        return {None}

    start, end, num = set_params
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
        dctcpShiftG: int,
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
        self.dctcpShiftG = dctcpShiftG
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
            "dctcpShiftG": self.dctcpShiftG,
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
            str(self.dctcpShiftG),
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

    # def writeConfig(self):
    #     with open(
    #         file=self.trace_path + "config.json", mode="w", encoding="ascii"
    #     ) as config_file:
    #         config_dict = {}

    #         for k, v in self.commandLineOptions.items():
    #             if "Directory" not in k:
    #                 config_dict[k] = v

    #         config_file.write(json.dumps(config_dict, indent=4))

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
    dctcpShiftGExp_set: set[int] = getLinearSet(
        experiment_config,
        "DCTCP_SHIFT_G_EXP",
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
    queueSizePackets_set: set[int] = getLinearSet(
        experiment_config,
        "QUEUE_SIZE_PACKETS",
    )
    queueThresholdFactor_set: set[int] = getExponentialSet(
        experiment_config,
        "QUEUE_THRESHOLD_FACTOR",
    )
    queueThresholdPackets_set: set[int] = getLinearSet(
        experiment_config,
        "QUEUE_THRESHOLD_PACKETS",
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
            dctcpShiftGExp_set,
            delAckCount_set,
            delAckTimeoutMs_set,
            delayPerLinkUs_set,
            largeLinkBandwidthMbps_set,
            numSenders_set,
            queueSizeFactor_set,
            queueSizePackets_set,
            queueThresholdFactor_set,
            queueThresholdPackets_set,
            segmentSizeBytes_set,
            smallLinkBandwidthMbps_set,
        )
    )

    num_samples = min(len(params_tuples), experiment_config["NUM_SAMPLES"])
    params_sample = random.sample(list(params_tuples), num_samples)
    params_set = set()

    for (
        burstDurationMs,
        dctcpShiftGExp,
        delAckCount,
        delAckTimeoutMs,
        delayPerLinkUs,
        largeLinkBandwidthMbps,
        numSenders,
        queueSizeFactor,
        queueSizePackets,
        queueThresholdFactor,
        queueThresholdPackets,
        segmentSizeBytes,
        smallLinkBandwidthMbps,
    ) in params_sample:
        cca: str = experiment_config["CCA"]
        dctcpShiftG: int = 1.0 / (2.0**dctcpShiftGExp)
        jitterUs: int = getJitterUs(delayPerLinkUs)
        bytesPerSender: int = getBytesPerSender(
            burstDurationMs,
            numSenders,
            smallLinkBandwidthMbps,
        )

        if smallLinkBandwidthMbps >= largeLinkBandwidthMbps:
            continue

        if queueSizeFactor is None:
            largeQueueThresholdPackets: int = queueThresholdPackets
            largeQueueSizePackets: int = queueSizePackets

            smallQueueThresholdPackets: int = queueThresholdPackets
            smallQueueSizePackets: int = queueSizePackets
        else:
            largeLinkCapacityPps: int = getLinkCapacityPps(
                largeLinkBandwidthMbps,
                segmentSizeBytes,
            )
            smallLinkCapacityPps: int = getLinkCapacityPps(
                smallLinkBandwidthMbps,
                segmentSizeBytes,
            )
            rttS: float = getRttS(delayPerLinkUs)

            largeQueueThresholdPackets: int = math.ceil(
                largeLinkCapacityPps * rttS / 7.0
            )
            largeQueueSizePackets: int = int(
                queueSizeFactor * smallLinkCapacityPps * rttS
            )

            smallQueueThresholdPackets: int = math.ceil(
                smallLinkCapacityPps * rttS / 7.0
            )
            smallQueueSizePackets: int = int(
                queueSizeFactor * smallLinkCapacityPps * rttS
            )

        if queueThresholdFactor is not None:
            largeQueueThresholdPackets: int = math.ceil(
                float(largeQueueSizePackets) / queueThresholdFactor
            )
            smallQueueThresholdPackets: int = math.ceil(
                float(smallQueueSizePackets) / queueThresholdFactor
            )

        for trial in range(1, experiment_config["NUM_TRIALS"] + 1):
            params_set.add(
                Params(
                    bytesPerSender,
                    cca,
                    dctcpShiftG,
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
    # subprocess.call("./ns3 clean", shell=True)
    # subprocess.call("./ns3 configure", shell=True)
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
        # params.writeConfig()
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

            outputDirectory: str = experiment_config["OUTPUT_DIRECTORY"]
            traceDirectory: str = params.getTraceDirectory()
            outputTraceDirectory: str = f"{outputDirectory}{traceDirectory}"
            outputTarName: str = outputTraceDirectory[:-1]

            subprocess.run(
                args=f"tar -czf {outputTarName}.tar.gz {outputTraceDirectory}",
                shell=True,
            )
            subprocess.run(args=f"rm -rf {outputTraceDirectory}", shell=True)

            with open(file=progress_path, mode="w", encoding="ascii") as progress_file:
                progress_file.write(f"num_successes: {num_successes}\n")
                progress_file.write(f"num_failures: {num_failures}\n")
                progress_file.write(f"num_experiments: {num_experiments}")

    # Run all experiments in parallel
    with ThreadPoolExecutor(experiment_config["NUM_PROCESSES"]) as executor:
        executor.map(run, params_set)


if __name__ == "__main__":
    main()
