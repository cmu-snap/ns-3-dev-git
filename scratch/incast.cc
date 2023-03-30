/*
 * Copyright (c) 2023 Carnegie Mellon University
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

// Derived from: https://code.nsnam.org/adrian/ns-3-incast
//
// Run with:
//  $ ./ns3 run "scratch/incast --bytesPerSender=100000 --numBursts=5
//       --numSenders=100 --smallLinkBandwidthMbps=12500
//       --largeLinkBandwidthMbps=100000"

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/incast-aggregator.h"
#include "ns3/incast-sender.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-layout-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/traffic-control-module.h"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>

/*
 * Incast Topology
 *
 *    Left(i)            Left()             Right()          Right(i)
 * [aggregator] --1-- [ToR switch] ==2== [ToR switch] --1-- [senders]
 *
 * 1: small link
 * 2: large link
 */

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("IncastSim");

std::ofstream incastQueueDepthOut;
std::ofstream uplinkQueueDepthOut;
std::ofstream incastQueueMarkOut;
std::ofstream uplinkQueueMarkOut;
std::ofstream incastQueueDropOut;
std::ofstream uplinkQueueDropOut;

void
LogQueueDepth(std::ofstream *out, uint32_t oldDepth, uint32_t newDepth) {
  (*out) << std::fixed << std::setprecision(9) << Simulator::Now().GetSeconds()
         << " " << newDepth << std::endl;
}

void
LogIncastQueueDepth(uint32_t oldDepth, uint32_t newDepth) {
  LogQueueDepth(&incastQueueDepthOut, oldDepth, newDepth);
}

void
LogUplinkQueueDepth(uint32_t oldDepth, uint32_t newDepth) {
  LogQueueDepth(&uplinkQueueDepthOut, oldDepth, newDepth);
}

void
LogQueueMark(std::ofstream *out) {
  (*out) << std::fixed << std::setprecision(9) << Simulator::Now().GetSeconds()
         << std::endl;
}

void
LogIncastQueueMark(Ptr<const QueueDiscItem>, const char *) {
  LogQueueMark(&incastQueueMarkOut);
}

void
LogUplinkQueueMark(Ptr<const QueueDiscItem>, const char *) {
  LogQueueMark(&uplinkQueueMarkOut);
}

void
LogQueueDrop(std::ofstream *out, int type) {
  (*out) << std::fixed << std::setprecision(9) << Simulator::Now().GetSeconds()
         << " " << type << std::endl;
}

void
LogIncastQueueDrop(Ptr<const QueueDiscItem>) {
  LogQueueDrop(&incastQueueDropOut, 0);
}

void
LogIncastQueueDropBeforeEnqueue(Ptr<const QueueDiscItem>, const char *) {
  LogQueueDrop(&incastQueueDropOut, 1);
}

void
LogIncastQueueDropAfterDequeue(Ptr<const QueueDiscItem>, const char *) {
  LogQueueDrop(&incastQueueDropOut, 2);
}

void
LogUplinkQueueDrop(Ptr<const QueueDiscItem>) {
  LogQueueDrop(&uplinkQueueDropOut, 0);
}

void
LogUplinkQueueDropBeforeEnqueue(Ptr<const QueueDiscItem>, const char *) {
  LogQueueDrop(&uplinkQueueDropOut, 1);
}

void
LogUplinkQueueDropAfterDequeue(Ptr<const QueueDiscItem>, const char *) {
  LogQueueDrop(&uplinkQueueDropOut, 2);
}

int
main(int argc, char *argv[]) {
  // Define log configurations
  LogLevel logConfigInfo =
      (LogLevel)(LOG_PREFIX_LEVEL | LOG_PREFIX_TIME | LOG_PREFIX_NODE | LOG_LEVEL_INFO);
  LogComponentEnable("IncastSim", logConfigInfo);
  LogComponentEnable("IncastAggregator", logConfigInfo);
  LogComponentEnable("IncastSender", logConfigInfo);
  //   LogLevel logConfigWarn =
  //       (LogLevel)(LOG_PREFIX_LEVEL | LOG_PREFIX_TIME | LOG_PREFIX_NODE |
  //       LOG_LEVEL_WARN);
  //   LogComponentEnable("TcpSocketBase", logConfigWarn);

  // Parameters for the simulation
  std::string tcpTypeId = "TcpCubic";
  uint32_t numBursts = 5;
  uint32_t numSenders = 10;
  uint32_t bytesPerSender = 500000;
  float delayPerLinkUs = 5;
  uint32_t jitterUs = 100;
  uint32_t segmentSizeBytes = 1448;

  // Parameters for the small links (ToR to node)
  uint32_t smallLinkBandwidthMbps = 12500;
  uint32_t smallQueueSizePackets = 2666;
  uint32_t smallQueueMinThresholdPackets = 60;
  uint32_t smallQueueMaxThresholdPackets = 60;

  // Parameters for the large links (ToR to ToR)
  uint32_t largeLinkBandwidthMbps = 100000;
  uint32_t largeQueueSizePackets = 2666;
  uint32_t largeQueueMinThresholdPackets = 150;
  uint32_t largeQueueMaxThresholdPackets = 150;

  // Parameters for RWND tuning
  std::string rwndStrategy = "none";
  uint32_t staticRwndBytes = 65535;

  // Configurations for tracing
  std::string outputDirectory = "scratch/traces/";
  std::string traceDirectory = "trace_directory/";
  bool enableSenderPcap = false;

  // Define command line arguments
  CommandLine cmd;
  cmd.AddValue(
      "outputDirectory",
      "Directory for all log and pcap traces",
      outputDirectory);
  cmd.AddValue(
      "traceDirectory",
      "Sub-directory for this experiment's log and pcap traces",
      traceDirectory);
  cmd.AddValue(
      "cca",
      "Congestion control algorithm (e.g., TcpCubic, TcpDctcp, etc.)",
      tcpTypeId);
  cmd.AddValue("numBursts", "Number of bursts to simulate", numBursts);
  cmd.AddValue("numSenders", "Number of incast senders", numSenders);
  cmd.AddValue(
      "bytesPerSender",
      "Number of bytes for each sender to send for each burst",
      bytesPerSender);
  cmd.AddValue(
      "jitterUs",
      "Maximum random jitter when sending requests (in microseconds)",
      jitterUs);
  cmd.AddValue(
      "segmentSizeBytes", "TCP segment size (in bytes)", segmentSizeBytes);
  cmd.AddValue(
      "smallLinkBandwidthMbps",
      "Small link bandwidth (in Mbps)",
      smallLinkBandwidthMbps);
  cmd.AddValue(
      "largeLinkBandwidthMbps",
      "Large link bandwidth (in Mbps)",
      largeLinkBandwidthMbps);
  cmd.AddValue(
      "delayPerLinkUs",
      "Delay on each link (in microseconds). The RTT is 6 times this value.",
      delayPerLinkUs);
  cmd.AddValue(
      "smallQueueSizePackets",
      "Maximum number of packets accepted by queues on the small link",
      smallQueueSizePackets);
  cmd.AddValue(
      "smallQueueMinThresholdPackets",
      "Minimum average length threshold for the small queue (in packets/bytes)",
      smallQueueMinThresholdPackets);
  cmd.AddValue(
      "smallQueueMaxThresholdPackets",
      "Maximum average length threshold for the small queue (in packets/bytes)",
      smallQueueMaxThresholdPackets);
  cmd.AddValue(
      "largeQueueSizePackets",
      "Maximum number of packets accepted by queues on the large link",
      largeQueueSizePackets);
  cmd.AddValue(
      "largeQueueMinThresholdPackets",
      "Minimum average length threshold for the large queue (in packets/bytes)",
      largeQueueMinThresholdPackets);
  cmd.AddValue(
      "largeQueueMaxThresholdPackets",
      "Maximum average length threshold for the large queue (in packets/bytes)",
      largeQueueMaxThresholdPackets);
  cmd.AddValue(
      "rwndStrategy",
      "RWND tuning strategy to use [none, static, bdp+connections]",
      rwndStrategy);
  cmd.AddValue(
      "staticRwndBytes",
      "If --rwndStrategy=static, then use this static RWND value",
      staticRwndBytes);
  cmd.AddValue(
      "enableSenderPcap",
      "Enable pcap traces for the senders",
      enableSenderPcap);
  cmd.Parse(argc, argv);

  // Check if the large link will be overwhelmed
  uint32_t totalIncastMbps = smallLinkBandwidthMbps * numSenders;

  if (totalIncastMbps > largeLinkBandwidthMbps) {
    NS_LOG_WARN(
        "Total incast bandwidth (" << totalIncastMbps
                                   << "Mbps) exceeds large link bandwidth ("
                                   << largeLinkBandwidthMbps << "Mbps)");
  }

  // Convert numeric values to NS3 values
  std::ostringstream delayPerLinkUsString;
  delayPerLinkUsString << delayPerLinkUs << "us";
  StringValue delayPerLinkUsStringValue =
      StringValue(delayPerLinkUsString.str());

  std::ostringstream smallLinkBandwidthMbpsString;
  smallLinkBandwidthMbpsString << smallLinkBandwidthMbps << "Mbps";
  StringValue smallLinkBandwidthMbpsStringValue =
      StringValue(smallLinkBandwidthMbpsString.str());

  std::ostringstream largeLinkBandwidthMbpsString;
  largeLinkBandwidthMbpsString << largeLinkBandwidthMbps << "Mbps";
  StringValue largeLinkBandwidthMbpsStringValue =
      StringValue(largeLinkBandwidthMbpsString.str());

  std::ostringstream smallQueueSizePacketsString;
  smallQueueSizePacketsString << smallQueueSizePackets << "p";
  QueueSizeValue smallQueueSizePacketsValue =
      QueueSizeValue(QueueSize(smallQueueSizePacketsString.str()));

  std::ostringstream largeQueueSizePacketsString;
  largeQueueSizePacketsString << largeQueueSizePackets << "p";
  QueueSizeValue largeQueueSizePacketsValue =
      QueueSizeValue(QueueSize(largeQueueSizePacketsString.str()));

  NS_LOG_INFO("Building incast topology...");

  // Create links
  PointToPointHelper smallLinkHelper;
  smallLinkHelper.SetDeviceAttribute(

      "DataRate", smallLinkBandwidthMbpsStringValue);
  smallLinkHelper.SetChannelAttribute("Delay", delayPerLinkUsStringValue);

  PointToPointHelper largeLinkHelper;
  largeLinkHelper.SetDeviceAttribute(

      "DataRate", largeLinkBandwidthMbpsStringValue);
  largeLinkHelper.SetChannelAttribute("Delay", delayPerLinkUsStringValue);

  // Create a dumbbell topology
  PointToPointDumbbellHelper dumbbellHelper(
      1, smallLinkHelper, numSenders, smallLinkHelper, largeLinkHelper);

  // Print global node IDs
  std::ostringstream leftNodeIds;
  leftNodeIds << "Left nodes (aggregator): ";
  for (uint32_t i = 0; i < dumbbellHelper.LeftCount(); ++i) {
    leftNodeIds << dumbbellHelper.GetLeft(i)->GetId() << " ";
  }

  std::ostringstream rightNodeIds;
  rightNodeIds << "Right nodes (senders): ";
  for (uint32_t i = 0; i < dumbbellHelper.RightCount(); ++i) {
    rightNodeIds << dumbbellHelper.GetRight(i)->GetId() << " ";
  }

  NS_LOG_INFO(
      "Node IDs:" << std::endl
                  << "\tLeft router (at the aggregator): "
                  << dumbbellHelper.GetLeft()->GetId() << std::endl
                  << "\tRight router (at the senders): "
                  << dumbbellHelper.GetRight()->GetId() << std::endl
                  << "\t" << leftNodeIds.str() << std::endl
                  << "\t" << rightNodeIds.str());

  NS_LOG_INFO("Installing the TCP stack on all nodes...");

  // Install the TCP stack on all nodes
  InternetStackHelper stackHelper;
  dumbbellHelper.InstallStack(stackHelper);

  NS_LOG_INFO("Configuring TCP parameters...");

  // Set global TCP parameters
  Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(pow(10, 9)));
  Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(pow(10, 9)));
  Config::SetDefault("ns3::TcpSocket::InitialCwnd", UintegerValue(10));
  Config::SetDefault("ns3::TcpSocket::DelAckCount", UintegerValue(1));
  Config::SetDefault("ns3::TcpSocket::TcpNoDelay", BooleanValue(true));
  //   Config::SetDefault("ns3::TcpSocket::DelAckCount", UintegerValue(2));
  Config::SetDefault(
      "ns3::TcpSocket::DelAckTimeout", TimeValue(MilliSeconds(0)));
  //   Config::SetDefault(
  //       "ns3::TcpSocket::DelAckTimeout", TimeValue(MilliSeconds(5)));
  // TODO: Try 9k
  Config::SetDefault(
      "ns3::TcpSocket::SegmentSize", UintegerValue(segmentSizeBytes));
  Config::SetDefault(
      "ns3::TcpSocketBase::MinRto", TimeValue(MilliSeconds(200)));
  Config::SetDefault("ns3::TcpSocketBase::Timestamp", BooleanValue(true));
  Config::SetDefault("ns3::TcpSocketBase::WindowScaling", BooleanValue(true));

  if (tcpTypeId == "TcpDctcp") {
    // TODO: For non-DCTCP, try with and without
    Config::SetDefault("ns3::TcpSocketBase::UseEcn", StringValue("On"));
  }

  // Important: Must set up queues before configuring IP addresses.
  NS_LOG_INFO("Creating queues...");

  // Set default parameters for RED queue disc
  Config::SetDefault("ns3::RedQueueDisc::UseEcn", BooleanValue(true));
  Config::SetDefault("ns3::RedQueueDisc::UseHardDrop", BooleanValue(false));
  Config::SetDefault("ns3::RedQueueDisc::MeanPktSize", UintegerValue(1500));
  // DCTCP tracks instantaneous queue length only; so set QW = 1
  Config::SetDefault("ns3::RedQueueDisc::QW", DoubleValue(1));

  // Configure different queues for the small and large links
  TrafficControlHelper smallLinkQueueHelper;
  smallLinkQueueHelper.SetRootQueueDisc(
      "ns3::RedQueueDisc",
      "LinkBandwidth",
      smallLinkBandwidthMbpsStringValue,
      "LinkDelay",
      delayPerLinkUsStringValue,
      "MaxSize",
      smallQueueSizePacketsValue,
      "MinTh",
      DoubleValue(smallQueueMinThresholdPackets),
      "MaxTh",
      DoubleValue(smallQueueMaxThresholdPackets),
      "LInterm",
      DoubleValue(100));

  TrafficControlHelper largeLinkQueueHelper;
  largeLinkQueueHelper.SetRootQueueDisc(
      "ns3::RedQueueDisc",
      "LinkBandwidth",
      largeLinkBandwidthMbpsStringValue,
      "LinkDelay",
      delayPerLinkUsStringValue,
      "MaxSize",
      largeQueueSizePacketsValue,
      "MinTh",
      DoubleValue(largeQueueMinThresholdPackets),
      "MaxTh",
      DoubleValue(largeQueueMaxThresholdPackets),
      "LInterm",
      DoubleValue(100));

  // Install small queues on all the NetDevices connected to small links.
  QueueDiscContainer leftQueues =
      smallLinkQueueHelper.Install(dumbbellHelper.GetLeftDevices());
  QueueDiscContainer leftRouterQueues =
      smallLinkQueueHelper.Install(dumbbellHelper.GetLeftRouterDevices());
  QueueDiscContainer rightQueues =
      smallLinkQueueHelper.Install(dumbbellHelper.GetRightDevices());
  QueueDiscContainer rightRouterQueues =
      smallLinkQueueHelper.Install(dumbbellHelper.GetRightRouterDevices());

  // Get the queue from the left switch to the aggregator.
  Ptr<QueueDisc> incastQueue = leftRouterQueues.Get(0);

  // Install large queues on all the NetDevices connected to large links.
  QueueDiscContainer switchQueues =
      largeLinkQueueHelper.Install(dumbbellHelper.GetRouterDevices());

  // Get the queue from the right switch to the left switch.
  Ptr<QueueDisc> uplinkQueue = switchQueues.Get(1);

  NS_LOG_INFO("Assigning IP addresses...");

  // Assign IP Addresses
  dumbbellHelper.AssignIpv4Addresses(
      Ipv4AddressHelper("10.0.0.0", "255.255.255.0"),
      Ipv4AddressHelper("11.0.0.0", "255.255.255.0"),
      Ipv4AddressHelper("12.0.0.0", "255.255.255.0"));

  NS_LOG_INFO("Configuring static global routing...");

  // Build the global routing table
  Ipv4GlobalRoutingHelper::PopulateRoutingTables();

  NS_LOG_INFO("Creating applications...");

  // Global record which burst is currently running.
  uint32_t currentBurstCount = 0;
  // Global record of senders, which maps sender node ID to a pair of SenderApp
  // and sender IP address.
  std::unordered_map<uint32_t, std::pair<Ptr<IncastSender>, Ipv4Address>>
      senders;
  // Global record of flow start and end times, which is a vector of bursts,
  // where each entry is a maps from sender node ID to (start time, end time)
  // pair.
  std::vector<std::unordered_map<uint32_t, std::pair<Time, Time>>> flowTimes;

  // Create the aggregator application
  Ptr<IncastAggregator> aggregatorApp = CreateObject<IncastAggregator>();
  aggregatorApp->SetCurrentBurstCount(&currentBurstCount);
  aggregatorApp->SetFlowTimesRecord(&flowTimes);
  aggregatorApp->SetStartTime(Seconds(1.0));
  aggregatorApp->SetAttribute("OutputDirectory", StringValue(outputDirectory));
  aggregatorApp->SetAttribute("TraceDirectory", StringValue(traceDirectory));
  aggregatorApp->SetAttribute("NumBursts", UintegerValue(numBursts));
  aggregatorApp->SetAttribute("BytesPerSender", UintegerValue(bytesPerSender));
  aggregatorApp->SetAttribute("RequestJitterUs", UintegerValue(jitterUs));
  aggregatorApp->SetAttribute(
      "CCA", TypeIdValue(TypeId::LookupByName("ns3::" + tcpTypeId)));
  aggregatorApp->SetAttribute("RwndStrategy", StringValue(rwndStrategy));
  aggregatorApp->SetAttribute(
      "StaticRwndBytes", UintegerValue(staticRwndBytes));
  aggregatorApp->SetAttribute(
      "BandwidthMbps", UintegerValue(smallLinkBandwidthMbps));
  aggregatorApp->SetAttribute(
      "PhysicalRTT", TimeValue(MicroSeconds(6 * delayPerLinkUs)));
  dumbbellHelper.GetLeft(0)->AddApplication(aggregatorApp);

  // Create the sender applications

  for (size_t i = 0; i < dumbbellHelper.RightCount(); ++i) {
    Ptr<IncastSender> senderApp = CreateObject<IncastSender>();
    senders[dumbbellHelper.GetRight(i)->GetId()] = {
        senderApp, dumbbellHelper.GetRightIpv4Address(i)};

    senderApp->SetCurrentBurstCount(&currentBurstCount);
    senderApp->SetFlowTimesRecord(&flowTimes);
    senderApp->SetAttribute("OutputDirectory", StringValue(outputDirectory));
    senderApp->SetAttribute("TraceDirectory", StringValue(traceDirectory));
    senderApp->SetAttribute(
        "Aggregator", Ipv4AddressValue(dumbbellHelper.GetLeftIpv4Address(0)));
    senderApp->SetAttribute("ResponseJitterUs", UintegerValue(jitterUs));
    senderApp->SetAttribute(
        "CCA", TypeIdValue(TypeId::LookupByName("ns3::" + tcpTypeId)));
    senderApp->SetStartTime(Seconds(1.0));

    dumbbellHelper.GetRight(i)->AddApplication(senderApp);
  }
  aggregatorApp->SetSenders(&senders);

  NS_LOG_INFO("Enabling tracing...");

  // Use nanosecond timestamps for PCAP traces
  Config::SetDefault("ns3::PcapFileWrapper::NanosecMode", BooleanValue(true));

  // Enable tracing at the aggregator
  largeLinkHelper.EnablePcap(
      outputDirectory + traceDirectory + "/pcap/incast",
      dumbbellHelper.GetLeft(0)->GetId(),
      0);

  // Enable tracing at each sender
  if (enableSenderPcap) {
    for (uint32_t i = 0; i < dumbbellHelper.RightCount(); ++i) {
      largeLinkHelper.EnablePcap(
          outputDirectory + traceDirectory + "/pcap/incast",
          dumbbellHelper.GetRight(i)->GetId(),
          0);
    }
  }

  // Compute the data per burst
  double totalBytesPerBurst = bytesPerSender * numSenders;
  double totalBytes = totalBytesPerBurst * numBursts;
  uint32_t megaToBase = pow(10, 6);

  NS_LOG_INFO(
      "Data per burst: " << totalBytesPerBurst / megaToBase
                         << " MB, Total data: " << totalBytes / megaToBase
                         << " MB");

  NS_LOG_INFO("Running simulation...");

  // Trace the queues
  //
  // Depth
  incastQueueDepthOut.open(
      outputDirectory + traceDirectory + "/log/incast_queue_depth.log",
      std::ios::out);
  incastQueueDepthOut << "# Time (s) qlen (pkts)" << std::endl;
  incastQueue->TraceConnectWithoutContext(
      "PacketsInQueue", MakeCallback(&LogIncastQueueDepth));
  uplinkQueueDepthOut.open(
      outputDirectory + traceDirectory + "/log/uplink_queue_depth.log",
      std::ios::out);
  uplinkQueueDepthOut << "# Time (s) qlen (pkts)" << std::endl;
  uplinkQueue->TraceConnectWithoutContext(
      "PacketsInQueue", MakeCallback(&LogUplinkQueueDepth));
  // Marks
  incastQueueMarkOut.open(
      outputDirectory + traceDirectory + "/log/incast_queue_mark.log",
      std::ios::out);
  incastQueueMarkOut << "# Time (s)" << std::endl;
  incastQueue->TraceConnectWithoutContext(
      "Mark", MakeCallback(&LogIncastQueueMark));
  uplinkQueueMarkOut.open(
      outputDirectory + traceDirectory + "/log/uplink_queue_mark.log",
      std::ios::out);
  uplinkQueueMarkOut << "# Time (s)" << std::endl;
  uplinkQueue->TraceConnectWithoutContext(
      "Mark", MakeCallback(&LogUplinkQueueMark));
  // Drops
  incastQueueDropOut.open(
      outputDirectory + traceDirectory + "/log/incast_queue_drop.log",
      std::ios::out);
  incastQueueDropOut << "# Time (s) Drop type" << std::endl;
  incastQueue->TraceConnectWithoutContext(
      "Drop", MakeCallback(&LogIncastQueueDrop));
  incastQueue->TraceConnectWithoutContext(
      "DropBeforeEnqueue", MakeCallback(&LogIncastQueueDropBeforeEnqueue));
  incastQueue->TraceConnectWithoutContext(
      "DropAfterDequeue", MakeCallback(&LogIncastQueueDropAfterDequeue));
  uplinkQueueDropOut.open(
      outputDirectory + traceDirectory + "/log/uplink_queue_drop.log",
      std::ios::out);
  uplinkQueueDropOut << "# Time (s) Drop type" << std::endl;
  uplinkQueue->TraceConnectWithoutContext(
      "Drop", MakeCallback(&LogUplinkQueueDrop));
  uplinkQueue->TraceConnectWithoutContext(
      "DropBeforeEnqueue", MakeCallback(&LogUplinkQueueDropBeforeEnqueue));
  uplinkQueue->TraceConnectWithoutContext(
      "DropAfterDequeue", MakeCallback(&LogUplinkQueueDropAfterDequeue));

  Simulator::Run();

  // Write application output files
  aggregatorApp->WriteLogs();
  for (const auto &p : senders) {
    p.second.first->WriteLogs();
  }

  Simulator::Destroy();

  incastQueueDepthOut.close();
  uplinkQueueDepthOut.close();
  incastQueueMarkOut.close();
  uplinkQueueMarkOut.close();
  incastQueueDropOut.close();
  uplinkQueueDropOut.close();

  NS_LOG_INFO("Finished simulation.");

  // Compute the ideal and actual burst durations
  uint32_t numBitsPerByte = 8;
  uint32_t numHops = 3;
  uint32_t baseToMilli = pow(10, 3);
  uint32_t baseToMicro = pow(10, 6);

  double burstTransmissionSec = (double)bytesPerSender * numSenders *
                                numBitsPerByte /
                                ((double)smallLinkBandwidthMbps * megaToBase);
  double firstRttSec = numHops * 2 * delayPerLinkUs / baseToMicro;
  double idealBurstDurationSec = burstTransmissionSec + firstRttSec;

  NS_LOG_INFO(
      "Ideal burst duration: " << idealBurstDurationSec * baseToMilli << "ms");

  NS_LOG_INFO("Burst durations (x ideal):");
  for (const auto &p : aggregatorApp->GetBurstTimes()) {
    Time burstDuration = p.second - p.first;
    NS_LOG_INFO(
        burstDuration.As(Time::MS)
        << " (" << burstDuration.GetSeconds() / idealBurstDurationSec << "x)");
  }

  // Serialize the flow times to a JSON file.
  nlohmann::json flowTimesJson;
  for (uint32_t i = 0; i < flowTimes.size(); ++i) {
    nlohmann::json burstJson;
    for (const auto &flow : flowTimes[i]) {
      Ipv4Address ip = senders[flow.first].second;
      std::ostringstream ipStr;
      ip.Print(ipStr);

      burstJson[ipStr.str()] = {
          {"start", flow.second.first.GetSeconds()},
          {"end", flow.second.second.GetSeconds()}};
    }
    flowTimesJson[std::to_string(i)] = burstJson;
  }
  std::ofstream burstTimesOut;
  burstTimesOut.open(
      outputDirectory + "/" + traceDirectory + "/log/flow_times.json",
      std::ios::out);
  burstTimesOut << std::setw(4) << flowTimesJson << std::endl;
  burstTimesOut.close();

  return 0;
}