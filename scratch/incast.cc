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
//  $ ./ns3 run "scratch/incast --bytesPerBurstSender=100000 --numBursts=5
//       --numBurstSenders=100 --smallLinkBandwidthMbps=12500
//       --largeLinkBandwidthMbps=100000"

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
// #include "ns3/burst-sender.h"
// #include "ns3/incast-aggregator.h"
// #include "ns3/incast-sender.h"
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
 *    Left(i)            Left()             Right()             Right(i)
 * [aggregator]---1---[ToR switch]===2===[ToR switch]---1---[burst senders]
 *                         ||
 *                          ===3===[ToR switch]---1---[background senders]
 *
 * 1: small link
 * 2: large burst link
 * 3: large background link
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
  (*out) << Simulator::Now().GetSeconds() << " " << newDepth << std::endl;
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
  (*out) << Simulator::Now().GetSeconds() << std::endl;
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
  (*out) << Simulator::Now().GetSeconds() << " " << type << std::endl;
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
  LogComponentEnable("BurstSender", logConfigInfo);
  //   LogLevel logConfigWarn =
  //       (LogLevel)(LOG_PREFIX_LEVEL | LOG_PREFIX_TIME | LOG_PREFIX_NODE |
  //       LOG_LEVEL_WARN);
  //   LogComponentEnable("TcpSocketBase", logConfigWarn);

  // Parameters for the simulation
  std::string tcpTypeId = "TcpCubic";
  uint32_t numBursts = 5;
  uint32_t numBurstSenders = 10;
  uint32_t numBackgroundSenders = 5;
  uint32_t bytesPerBurstSender = 500000;
  float delayPerLinkUs = 5;
  uint32_t jitterUs = 100;

  // Parameters for TCP
  uint32_t segmentSizeBytes = 1448;
  uint32_t delAckCount = 1;
  uint32_t delAckTimeoutMs = 0;
  uint32_t initialCwndSegments = 10;
  double dctcpShiftG = 0.0625;

  // Parameters for the small links (ToR to node)
  uint32_t smallLinkBandwidthMbps = 12500;
  uint32_t smallQueueSizePackets = 1800;
  uint32_t smallQueueMinThresholdPackets = 80;
  uint32_t smallQueueMaxThresholdPackets = 80;

  // Parameters for the large burst links (ToR to ToR)
  uint32_t largeBurstLinkBandwidthMbps = 100000;
  uint32_t largeBurstQueueSizePackets = 1200;
  uint32_t largeBurstQueueMinThresholdPackets = 80;
  uint32_t largeBurstQueueMaxThresholdPackets = 80;

  // Parameters for the large background links (ToR to ToR)
  uint32_t largeBackgroundLinkBandwidthMbps = 100000;
  uint32_t largeBackgroundQueueSizePackets = 2666;
  uint32_t largeBackgroundQueueMinThresholdPackets = 150;
  uint32_t largeBackgroundQueueMaxThresholdPackets = 150;

  // Parameters for RWND tuning
  std::string rwndStrategy = "none";
  uint32_t staticRwndBytes = 65535;
  uint32_t rwndScheduleMaxConns = 20;

  // Parameters for tracing
  std::string outputDirectory = "scratch/traces/";
  std::string traceDirectory = "trace_directory/";
  bool enableSenderPcap = false;

  // Time to delay the request for the first sender in each burst.
  uint32_t firstFlowOffsetMs = 0;

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
  cmd.AddValue("numBurstSenders", "Number of burst senders", numBurstSenders);
  cmd.AddValue(
      "bytesPerBurstSender",
      "Number of bytes for each sender to send for each burst",
      bytesPerBurstSender);
  cmd.AddValue(
      "jitterUs",
      "Maximum random jitter when sending requests (in microseconds)",
      jitterUs);
  cmd.AddValue(
      "segmentSizeBytes", "TCP segment size (in bytes)", segmentSizeBytes);
  cmd.AddValue(
      "delAckCount",
      "Number of packets to wait before sending a TCP ack",
      delAckCount);
  cmd.AddValue(
      "delAckTimeoutMs", "Timeout value for TCP delayed acks", delAckTimeoutMs);
  cmd.AddValue(
      "initialCwnd",
      "The initial congestion window size (in segments)",
      initialCwndSegments);
  cmd.AddValue(
      "dctcpShiftG", "Parameter G for updating dctcp_alpha", dctcpShiftG);
  cmd.AddValue(
      "smallLinkBandwidthMbps",
      "Small link bandwidth (in Mbps)",
      smallLinkBandwidthMbps);
  cmd.AddValue(
      "largeBurstLinkBandwidthMbps",
      "Large burst link bandwidth (in Mbps)",
      largeBurstLinkBandwidthMbps);
  cmd.AddValue(
      "largeBackgroundLinkBandwidthMbps",
      "Large background link bandwidth (in Mbps)",
      largeBackgroundLinkBandwidthMbps);
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
      "largeBurstQueueSizePackets",
      "Maximum number of packets accepted by queues on the large burst link",
      largeBurstQueueSizePackets);
  cmd.AddValue(
      "largeBackgroundQueueSizePackets",
      "Maximum number of packets accepted by queues on the large background "
      "link",
      largeBackgroundQueueSizePackets);
  cmd.AddValue(
      "largeBurstQueueMinThresholdPackets",
      "Minimum average length threshold for the large burst queue (in "
      "packets/bytes)",
      largeBurstQueueMinThresholdPackets);
  cmd.AddValue(
      "largeBackgroundQueueMinThresholdPackets",
      "Minimum average length threshold for the large background queue (in "
      "packets/bytes)",
      largeBackgroundQueueMinThresholdPackets);
  cmd.AddValue(
      "largeBurstQueueMaxThresholdPackets",
      "Maximum average length threshold for the large burst queue (in "
      "packets/bytes)",
      largeBurstQueueMaxThresholdPackets);
  cmd.AddValue(
      "largeBackgroundQueueMaxThresholdPackets",
      "Maximum average length threshold for the large background queue (in "
      "packets/bytes)",
      largeBackgroundQueueMaxThresholdPackets);
  cmd.AddValue(
      "rwndStrategy",
      "RWND tuning strategy to use [none, static, bdp+connections, scheduled]",
      rwndStrategy);
  cmd.AddValue(
      "staticRwndBytes",
      "If --rwndStrategy=static, then use this static RWND value",
      staticRwndBytes);
  cmd.AddValue(
      "rwndScheduleMaxConns",
      "If RwndStrategy==scheduled, then this is the max number of senders that "
      "are allowed to transmit concurrently.",
      rwndScheduleMaxConns);
  cmd.AddValue(
      "enableSenderPcap",
      "Enable pcap traces for the senders",
      enableSenderPcap);
  cmd.AddValue(
      "firstFlowOffsetMs",
      "Time to delay the request for the first sender in each burst (in "
      "milliseconds). Overrides any jitter at the aggregator node. 0 means no "
      "delay, and use jitter instead.",
      firstFlowOffsetMs);

  cmd.Parse(argc, argv);

  // Check if the large burst link will be overwhelmed
  uint32_t totalIncastMbps = smallLinkBandwidthMbps * numBurstSenders;

  if (totalIncastMbps > largeBurstLinkBandwidthMbps) {
    NS_LOG_WARN(
        "Total incast bandwidth ("
        << totalIncastMbps << "Mbps) exceeds large burst link bandwidth ("
        << largeBurstLinkBandwidthMbps << "Mbps)");
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

  std::ostringstream largeBurstLinkBandwidthMbpsString;
  largeBurstLinkBandwidthMbpsString << largeBurstLinkBandwidthMbps << "Mbps";
  StringValue largeBurstLinkBandwidthMbpsStringValue =
      StringValue(largeBurstLinkBandwidthMbpsString.str());

  std::ostringstream largeBackgroundLinkBandwidthMbpsString;
  largeBackgroundLinkBandwidthMbpsString << largeBackgroundLinkBandwidthMbps
                                         << "Mbps";
  StringValue largeBackgroundLinkBandwidthMbpsStringValue =
      StringValue(largeBackgroundLinkBandwidthMbpsString.str());

  std::ostringstream smallQueueSizePacketsString;
  smallQueueSizePacketsString << smallQueueSizePackets << "p";
  QueueSizeValue smallQueueSizePacketsValue =
      QueueSizeValue(QueueSize(smallQueueSizePacketsString.str()));

  std::ostringstream largeBurstQueueSizePacketsString;
  largeBurstQueueSizePacketsString << largeBurstQueueSizePackets << "p";
  QueueSizeValue largeBurstQueueSizePacketsValue =
      QueueSizeValue(QueueSize(largeBurstQueueSizePacketsString.str()));

  std::ostringstream largeBackgroundQueueSizePacketsString;
  largeBackgroundQueueSizePacketsString << largeBackgroundQueueSizePackets
                                        << "p";
  QueueSizeValue largeBackgroundQueueSizePacketsValue =
      QueueSizeValue(QueueSize(largeBackgroundQueueSizePacketsString.str()));

  NS_LOG_INFO("Building incast topology...");

  // Create links
  PointToPointHelper smallLinkHelper;
  smallLinkHelper.SetDeviceAttribute(
      "DataRate", smallLinkBandwidthMbpsStringValue);
  smallLinkHelper.SetChannelAttribute("Delay", delayPerLinkUsStringValue);

  PointToPointHelper largeBurstLinkHelper;
  largeBurstLinkHelper.SetDeviceAttribute(
      "DataRate", largeBurstLinkBandwidthMbpsStringValue);
  largeBurstLinkHelper.SetChannelAttribute("Delay", delayPerLinkUsStringValue);

  PointToPointHelper largeBackgroundLinkHelper;
  largeBackgroundLinkHelper.SetDeviceAttribute(
      "DataRate", largeBackgroundLinkBandwidthMbpsStringValue);
  largeBackgroundLinkHelper.SetChannelAttribute(
      "Delay", delayPerLinkUsStringValue);

  // Create dumbbell topologies
  PointToPointDumbbellHelper burstDumbbellHelper(
      1,
      smallLinkHelper,
      numBurstSenders,
      smallLinkHelper,
      largeBurstLinkHelper);

  PointToPointDumbbellHelper backgroundDumbbellHelper(
      1,
      smallLinkHelper,
      numBackgroundSenders,
      smallLinkHelper,
      largeBackgroundLinkHelper);

  // Print global node IDs
  std::ostringstream leftNodeIds;
  leftNodeIds << "Left node (aggregator): ";
  leftNodeIds << burstDumbbellHelper.GetLeft(0)->GetId() << " ";

  std::ostringstream rightBurstNodeIds;
  rightBurstNodeIds << "Right nodes (burst senders): ";
  for (uint32_t i = 0; i < burstDumbbellHelper.RightCount(); ++i) {
    rightBurstNodeIds << burstDumbbellHelper.GetRight(i)->GetId() << " ";
  }

  std::ostringstream rightBackgroundNodeIds;
  rightBackgroundNodeIds << "Right nodes (background senders): ";
  for (uint32_t i = 0; i < backgroundDumbbellHelper.RightCount(); ++i) {
    rightBackgroundNodeIds << backgroundDumbbellHelper.GetRight(i)->GetId()
                           << " ";
  }

  NS_LOG_INFO(
      "Node IDs:" << std::endl
                  << "\tLeft router (at the aggregator): "
                  << burstDumbbellHelper.GetLeft()->GetId() << std::endl
                  << "\tRight router (at the burst senders): "
                  << burstDumbbellHelper.GetRight()->GetId() << std::endl
                  << "\t" << leftNodeIds.str() << std::endl
                  << "\t" << rightBurstNodeIds.str() << std::endl
                  << "\t" << rightBackgroundNodeIds.str());

  NS_LOG_INFO("Installing the TCP stack on all nodes...");

  // Install the TCP stack on all nodes
  InternetStackHelper stackHelper;
  burstDumbbellHelper.InstallStack(stackHelper);
  backgroundDumbbellHelper.InstallStack(stackHelper);

  NS_LOG_INFO("Configuring TCP parameters...");

  // Set global TCP parameters
  Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(pow(10, 9)));
  Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(pow(10, 9)));
  Config::SetDefault(
      "ns3::TcpSocket::InitialCwnd", UintegerValue(initialCwndSegments));
  Config::SetDefault("ns3::TcpSocket::TcpNoDelay", BooleanValue(true));
  Config::SetDefault("ns3::TcpSocket::DelAckCount", UintegerValue(delAckCount));
  Config::SetDefault(
      "ns3::TcpSocket::DelAckTimeout",
      TimeValue(MilliSeconds(delAckTimeoutMs)));
  // TODO: Try 9k
  Config::SetDefault(
      "ns3::TcpSocket::SegmentSize", UintegerValue(segmentSizeBytes));
  Config::SetDefault(
      "ns3::TcpSocketBase::MinRto", TimeValue(MilliSeconds(200)));
  Config::SetDefault("ns3::TcpSocketBase::Timestamp", BooleanValue(true));
  Config::SetDefault("ns3::TcpSocketBase::WindowScaling", BooleanValue(true));

  // Important: Must set up queues before configuring IP addresses.
  NS_LOG_INFO("Creating queues...");

  // Set default parameters for RED queue disc
  Config::SetDefault("ns3::RedQueueDisc::UseHardDrop", BooleanValue(false));
  Config::SetDefault("ns3::RedQueueDisc::MeanPktSize", UintegerValue(1500));

  // Set QW = 1 because DCTCP tracks the instantaneous queue length only
  Config::SetDefault("ns3::RedQueueDisc::QW", DoubleValue(1));

  // Use ECN for DCTCP
  if (tcpTypeId == "TcpDctcp") {
    Config::SetDefault("ns3::TcpSocketBase::UseEcn", StringValue("On"));
    Config::SetDefault("ns3::RedQueueDisc::UseEcn", BooleanValue(true));
  }

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
      DoubleValue(1));

  TrafficControlHelper largeBurstLinkQueueHelper;
  largeBurstLinkQueueHelper.SetRootQueueDisc(
      "ns3::RedQueueDisc",
      "LinkBandwidth",
      largeBurstLinkBandwidthMbpsStringValue,
      "LinkDelay",
      delayPerLinkUsStringValue,
      "MaxSize",
      largeBurstQueueSizePacketsValue,
      "MinTh",
      DoubleValue(largeBurstQueueMinThresholdPackets),
      "MaxTh",
      DoubleValue(largeBurstQueueMaxThresholdPackets),
      "LInterm",
      DoubleValue(1));

  TrafficControlHelper largeBackgroundLinkQueueHelper;
  largeBackgroundLinkQueueHelper.SetRootQueueDisc(
      "ns3::RedQueueDisc",
      "LinkBandwidth",
      largeBackgroundLinkBandwidthMbpsStringValue,
      "LinkDelay",
      delayPerLinkUsStringValue,
      "MaxSize",
      largeBackgroundQueueSizePacketsValue,
      "MinTh",
      DoubleValue(largeBackgroundQueueMinThresholdPackets),
      "MaxTh",
      DoubleValue(largeBackgroundQueueMaxThresholdPackets),
      "LInterm",
      DoubleValue(1));

  // Install small queues on all the NetDevices connected to small links.
  QueueDiscContainer leftQueues =
      smallLinkQueueHelper.Install(burstDumbbellHelper.GetLeftDevices());
  QueueDiscContainer leftRouterQueues =
      smallLinkQueueHelper.Install(burstDumbbellHelper.GetLeftRouterDevices());
  QueueDiscContainer rightBurstQueues =
      smallLinkQueueHelper.Install(burstDumbbellHelper.GetRightDevices());
  QueueDiscContainer rightBurstRouterQueues =
      smallLinkQueueHelper.Install(burstDumbbellHelper.GetRightRouterDevices());
  QueueDiscContainer rightBackgroundQueues =
      smallLinkQueueHelper.Install(backgroundDumbbellHelper.GetRightDevices());
  QueueDiscContainer rightBackgroundRouterQueues = smallLinkQueueHelper.Install(
      backgroundDumbbellHelper.GetRightRouterDevices());

  // Get the queue from the left switch to the aggregator.
  Ptr<QueueDisc> incastQueue = leftRouterQueues.Get(0);

  // Install large queues on all the NetDevices connected to large links.
  QueueDiscContainer burstSwitchQueues =
      largeBurstLinkQueueHelper.Install(burstDumbbellHelper.GetRouterDevices());

  // Get the queue from the right switch to the left switch.
  Ptr<QueueDisc> uplinkQueue = burstSwitchQueues.Get(1);

  NS_LOG_INFO("Assigning IP addresses...");

  // Assign IP Addresses
  burstDumbbellHelper.AssignIpv4Addresses(
      Ipv4AddressHelper("10.0.0.0", "255.255.255.0"),
      Ipv4AddressHelper("11.0.0.0", "255.255.255.0"),
      Ipv4AddressHelper("12.0.0.0", "255.255.255.0"));

  backgroundDumbbellHelper.AssignIpv4Addresses(
      Ipv4AddressHelper("20.0.0.0", "255.255.255.0"),
      Ipv4AddressHelper("21.0.0.0", "255.255.255.0"),
      Ipv4AddressHelper("22.0.0.0", "255.255.255.0"));

  NS_LOG_INFO("Configuring static global routing...");

  // Build the global routing table
  Ipv4GlobalRoutingHelper::PopulateRoutingTables();

  NS_LOG_INFO("Creating applications...");

  // Global record of burst senders, which maps the sender node ID to a pair of
  // SenderApp and sender IP address.
  std::unordered_map<uint32_t, std::pair<Ptr<IncastSender>, Ipv4Address>>
      burstSenders;
  // Global record which burst is currently running.
  uint32_t currentBurstCount = 0;
  // Global record of flow start and end times, which is a vector of burst info,
  // where each entry maps the sender node ID to a (start time, end time) pair.
  std::vector<std::unordered_map<uint32_t, std::vector<Time>>> flowTimes;

  // Create the aggregator application
  Ptr<IncastAggregator> aggregatorApp = CreateObject<IncastAggregator>();
  aggregatorApp->SetCurrentBurstCount(&currentBurstCount);
  aggregatorApp->SetFlowTimesRecord(&flowTimes);
  aggregatorApp->SetStartTime(Seconds(1.0));
  aggregatorApp->SetAttribute("OutputDirectory", StringValue(outputDirectory));
  aggregatorApp->SetAttribute("TraceDirectory", StringValue(traceDirectory));
  aggregatorApp->SetAttribute("NumBursts", UintegerValue(numBursts));
  aggregatorApp->SetAttribute("BytesPerBurstSender", UintegerValue(bytesPerBurstSender));
  aggregatorApp->SetAttribute("RequestJitterUs", UintegerValue(jitterUs));
  aggregatorApp->SetAttribute(
      "CCA", TypeIdValue(TypeId::LookupByName("ns3::" + tcpTypeId)));
  aggregatorApp->SetAttribute("RwndStrategy", StringValue(rwndStrategy));
  aggregatorApp->SetAttribute(
      "StaticRwndBytes", UintegerValue(staticRwndBytes));
  aggregatorApp->SetAttribute(
      "BandwidthMbps", UintegerValue(smallLinkBandwidthMbps));
  aggregatorApp->SetAttribute(
      "RwndScheduleMaxTokens", UintegerValue(rwndScheduleMaxConns));
  aggregatorApp->SetAttribute(
      "PhysicalRTT", TimeValue(MicroSeconds(6 * delayPerLinkUs)));
  aggregatorApp->SetAttribute(
      "FirstFlowOffset", TimeValue(MilliSeconds(firstFlowOffsetMs)));
  aggregatorApp->SetAttribute("DctcpShiftG", DoubleValue(dctcpShiftG));
  burstDumbbellHelper.GetLeft(0)->AddApplication(aggregatorApp);

  // Create the burst sender applications
  for (size_t i = 0; i < burstDumbbellHelper.RightCount(); ++i) {
    Ptr<BurstSender> senderApp = CreateObject<BurstSender>();
    burstSenders[burstDumbbellHelper.GetRight(i)->GetId()] = {
        senderApp, burstDumbbellHelper.GetRightIpv4Address(i)};

    senderApp->SetCurrentBurstCount(&currentBurstCount);
    senderApp->SetFlowTimesRecord(&flowTimes);
    senderApp->SetAttribute("OutputDirectory", StringValue(outputDirectory));
    senderApp->SetAttribute("TraceDirectory", StringValue(traceDirectory));
    senderApp->SetAttribute("NumBursts", UintegerValue(numBursts));
    senderApp->SetAttribute(
        "Aggregator",
        Ipv4AddressValue(burstDumbbellHelper.GetLeftIpv4Address(0)));
    senderApp->SetAttribute("ResponseJitterUs", UintegerValue(jitterUs));
    senderApp->SetAttribute(
        "CCA", TypeIdValue(TypeId::LookupByName("ns3::" + tcpTypeId)));
    senderApp->SetStartTime(Seconds(1.0));
    senderApp->SetAttribute("DctcpShiftG", DoubleValue(dctcpShiftG));
    burstDumbbellHelper.GetRight(i)->AddApplication(senderApp);
  }

  // Global record of background senders, which maps the sender node ID to a
  // pair of SenderApp and sender IP address.
  std::unordered_map<uint32_t, std::pair<Ptr<IncastSender>, Ipv4Address>>
      backgroundSenders;

  // Create the background sender applications
  for (size_t i = 0; i < backgroundDumbbellHelper.RightCount(); ++i) {
    Ptr<BackgroundSender> senderApp = CreateObject<BackgroundSender>();
    backgroundSenders[backgroundDumbbellHelper.GetRight(i)->GetId()] = {
        senderApp, backgroundDumbbellHelper.GetRightIpv4Address(i)};

    senderApp->SetAttribute("OutputDirectory", StringValue(outputDirectory));
    senderApp->SetAttribute("TraceDirectory", StringValue(traceDirectory));
    senderApp->SetAttribute("NumBursts", UintegerValue(numBursts));
    senderApp->SetAttribute(
        "Aggregator",
        Ipv4AddressValue(burstDumbbellHelper.GetLeftIpv4Address(0)));
    senderApp->SetAttribute("ResponseJitterUs", UintegerValue(jitterUs));
    senderApp->SetAttribute(
        "CCA", TypeIdValue(TypeId::LookupByName("ns3::TcpCubic")));
    senderApp->SetStartTime(Seconds(0.0));
    backgroundDumbbellHelper.GetRight(i)->AddApplication(senderApp);
  }

  // Store burst and background senders 
  aggregatorApp->SetBurstSenders(&burstSenders);
  aggregatorApp->SetBackgroundSenders(&backgroundSenders);

  NS_LOG_INFO("Enabling tracing...");

  // Use nanosecond timestamps for PCAP traces
  Config::SetDefault("ns3::PcapFileWrapper::NanosecMode", BooleanValue(true));

  // Enable tracing at the aggregator
  largeBurstLinkHelper.EnablePcap(
      outputDirectory + traceDirectory + "/pcap/incast",
      burstDumbbellHelper.GetLeft(0)->GetId(),
      0);

  // Enable tracing at each burst sender
  if (enableSenderPcap) {
    for (uint32_t i = 0; i < burstDumbbellHelper.RightCount(); ++i) {
      largeBurstLinkHelper.EnablePcap(
          outputDirectory + traceDirectory + "/pcap/incast",
          burstDumbbellHelper.GetRight(i)->GetId(),
          0);
    }
  }

  // Enable tracing at each background sender
  if (enableSenderPcap) {
    for (uint32_t i = 0; i < backgroundDumbbellHelper.RightCount(); ++i) {
      largeBackgroundLinkHelper.EnablePcap(
          outputDirectory + traceDirectory + "/pcap/incast",
          backgroundDumbbellHelper.GetRight(i)->GetId(),
          0);
    }
  }

  // Compute the data per burst
  double totalBytesPerBurst = bytesPerBurstSender * numBurstSenders;
  double totalBytes = totalBytesPerBurst * numBursts;
  uint32_t megaToBase = pow(10, 6);

  NS_LOG_INFO(
      "Data per burst: " << totalBytesPerBurst / megaToBase
                         << " MB, Total data: " << totalBytes / megaToBase
                         << " MB");

  // Trace queue depths
  incastQueueDepthOut.open(
      outputDirectory + traceDirectory + "/logs/incast_queue_depth.log",
      std::ios::out);
  incastQueueDepthOut << std::fixed << std::setprecision(12)
                      << "# Time (s) qlen (pkts)" << std::endl;
  incastQueue->TraceConnectWithoutContext(
      "PacketsInQueue", MakeCallback(&LogIncastQueueDepth));
  uplinkQueueDepthOut.open(
      outputDirectory + traceDirectory + "/logs/uplink_queue_depth.log",
      std::ios::out);
  uplinkQueueDepthOut << std::fixed << std::setprecision(12)
                      << "# Time (s) qlen (pkts)" << std::endl;
  uplinkQueue->TraceConnectWithoutContext(
      "PacketsInQueue", MakeCallback(&LogUplinkQueueDepth));

  // Trace queue marks
  incastQueueMarkOut.open(
      outputDirectory + traceDirectory + "/logs/incast_queue_mark.log",
      std::ios::out);
  incastQueueMarkOut << std::fixed << std::setprecision(12) << "# Time (s)"
                     << std::endl;
  incastQueue->TraceConnectWithoutContext(
      "Mark", MakeCallback(&LogIncastQueueMark));
  uplinkQueueMarkOut.open(
      outputDirectory + traceDirectory + "/logs/uplink_queue_mark.log",
      std::ios::out);
  uplinkQueueMarkOut << std::fixed << std::setprecision(12) << "# Time (s)"
                     << std::endl;
  uplinkQueue->TraceConnectWithoutContext(
      "Mark", MakeCallback(&LogUplinkQueueMark));

  // Trace queue drops
  incastQueueDropOut.open(
      outputDirectory + traceDirectory + "/logs/incast_queue_drop.log",
      std::ios::out);
  incastQueueDropOut << std::fixed << std::setprecision(12)
                     << "# Time (s) Drop type" << std::endl;
  incastQueue->TraceConnectWithoutContext(
      "Drop", MakeCallback(&LogIncastQueueDrop));
  incastQueue->TraceConnectWithoutContext(
      "DropBeforeEnqueue", MakeCallback(&LogIncastQueueDropBeforeEnqueue));
  incastQueue->TraceConnectWithoutContext(
      "DropAfterDequeue", MakeCallback(&LogIncastQueueDropAfterDequeue));
  uplinkQueueDropOut.open(
      outputDirectory + traceDirectory + "/logs/uplink_queue_drop.log",
      std::ios::out);
  uplinkQueueDropOut << std::fixed << std::setprecision(12)
                     << "# Time (s) Drop type" << std::endl;
  uplinkQueue->TraceConnectWithoutContext(
      "Drop", MakeCallback(&LogUplinkQueueDrop));
  uplinkQueue->TraceConnectWithoutContext(
      "DropBeforeEnqueue", MakeCallback(&LogUplinkQueueDropBeforeEnqueue));
  uplinkQueue->TraceConnectWithoutContext(
      "DropAfterDequeue", MakeCallback(&LogUplinkQueueDropAfterDequeue));

  // Serialize all configuration parameters to a JSON file
  nlohmann::json configJson;
  configJson["outputDirectory"] = outputDirectory;
  configJson["traceDirectory"] = traceDirectory;
  configJson["cca"] = tcpTypeId;
  configJson["numBursts"] = numBursts;
  configJson["numBurstSenders"] = numBurstSenders;
  configJson["numBackgroundSenders"] = numBackgroundSenders;
  configJson["bytesPerBurstSender"] = bytesPerBurstSender;
  configJson["jitterUs"] = jitterUs;
  configJson["segmentSizeBytes"] = segmentSizeBytes;
  configJson["delAckCount"] = delAckCount;
  configJson["delAckTimeoutMs"] = delAckTimeoutMs;
  configJson["initialCwnd"] = initialCwndSegments;
  configJson["dctcpShiftG"] = dctcpShiftG;
  configJson["smallLinkBandwidthMbps"] = smallLinkBandwidthMbps;
  configJson["largeBurstLinkBandwidthMbps"] = largeBurstLinkBandwidthMbps;
  configJson["largeBackgroundLinkBandwidthMbps"] =
      largeBackgroundLinkBandwidthMbps;
  configJson["delayPerLinkUs"] = delayPerLinkUs;
  configJson["smallQueueSizePackets"] = smallQueueSizePackets;
  configJson["smallQueueMinThresholdPackets"] = smallQueueMinThresholdPackets;
  configJson["smallQueueMaxThresholdPackets"] = smallQueueMaxThresholdPackets;
  configJson["largeBurstQueueSizePackets"] = largeBurstQueueSizePackets;
  configJson["largeBurstQueueMinThresholdPackets"] =
      largeBurstQueueMinThresholdPackets;
  configJson["largeBurstQueueMaxThresholdPackets"] =
      largeBurstQueueMaxThresholdPackets;
  configJson["largeBackgroundQueueSizePackets"] =
      largeBackgroundQueueSizePackets;
  configJson["largeBackgroundQueueMinThresholdPackets"] =
      largeBackgroundQueueMinThresholdPackets;
  configJson["largeBackgroundQueueMaxThresholdPackets"] =
      largeBackgroundQueueMaxThresholdPackets;
  configJson["rwndStrategy"] = rwndStrategy;
  configJson["staticRwndBytes"] = staticRwndBytes;
  configJson["rwndScheduleMaxConns"] = rwndScheduleMaxConns;
  configJson["enableSenderPcap"] = enableSenderPcap;
  configJson["firstFlowOffsetMs"] = firstFlowOffsetMs;

  std::ofstream configOut;
  configOut.open(
      outputDirectory + "/" + traceDirectory + "/config.json", std::ios::out);
  configOut << std::fixed << std::setprecision(12) << std::setw(4) << configJson
            << std::endl;
  configOut.close();

  NS_LOG_INFO("Running simulation...");

  Simulator::Run();

  NS_LOG_INFO("Finished simulation.");

  // Write application output files
  aggregatorApp->WriteLogs();
  // This must take place before writing the flowTimes json below because this
  // function fills in the firstPacket time in the flowTimes data structure.
  for (const auto &[id, pair] : burstSenders) {
    pair.first->WriteLogs();
  }

  Simulator::Destroy();

  incastQueueDepthOut.close();
  uplinkQueueDepthOut.close();
  incastQueueMarkOut.close();
  uplinkQueueMarkOut.close();
  incastQueueDropOut.close();
  uplinkQueueDropOut.close();

  // Compute the ideal and actual burst durations
  uint32_t numBitsPerByte = 8;
  uint32_t numHops = 3;
  uint32_t baseToMilli = pow(10, 3);
  uint32_t baseToMicro = pow(10, 6);

  double burstTransmissionSec = (double)bytesPerBurstSender * numBurstSenders *
                                numBitsPerByte /
                                ((double)smallLinkBandwidthMbps * megaToBase);
  double firstRttSec = numHops * 2 * delayPerLinkUs / baseToMicro;
  double idealBurstDurationSec = burstTransmissionSec + firstRttSec;
  double idealBurstDurationMs = idealBurstDurationSec * baseToMilli;

  NS_LOG_INFO("Ideal burst duration: " << idealBurstDurationMs << "ms");
  NS_LOG_INFO("Burst durations (x ideal):");

  for (const auto &p : aggregatorApp->GetBurstTimes()) {
    Time burstDuration = p.second - p.first;
    NS_LOG_INFO(
        burstDuration.As(Time::MS)
        << " (" << burstDuration.GetSeconds() / idealBurstDurationSec << "x)");
  }

  // Serialize the flow times to a JSON file. This must take place after writing
  // the sender logs above, because that function fills in the firstPacket time
  // in the flowTimes data structure.
  nlohmann::json flowTimesJson;

  for (uint32_t i = 0; i < flowTimes.size(); ++i) {
    nlohmann::json burstJson;

    for (const auto &flow : flowTimes[i]) {
      Ipv4Address ip = burstSenders[flow.first].second;
      std::ostringstream ipStr;
      ip.Print(ipStr);

      burstJson[ipStr.str()] = {
          {"id", flow.first},
          {"start", flow.second[0].GetSeconds()},
          {"firstPacket", flow.second[1].GetSeconds()},
          {"end", flow.second[2].GetSeconds()}};
    }

    flowTimesJson[std::to_string(i)] = burstJson;
  }

  std::ofstream burstTimesOut;
  burstTimesOut.open(
      outputDirectory + "/" + traceDirectory + "/logs/flow_times.json",
      std::ios::out);
  burstTimesOut << std::fixed << std::setprecision(12) << std::setw(4)
                << flowTimesJson << std::endl;
  burstTimesOut.close();

  return 0;
}