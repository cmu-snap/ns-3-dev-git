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

std::ofstream incastQueueOut;
std::ofstream uplinkQueueOut;
std::ofstream dctcpAlphaOut;
std::ofstream rxDropsOut;

void
LogQueueDepth(std::ofstream *out, uint32_t oldDepth, uint32_t newDepth) {
  (*out) << std::fixed << std::setprecision(9) << Simulator::Now().GetSeconds()
         << " " << newDepth << std::endl;
}

void
LogIncastQueueDepth(uint32_t oldDepth, uint32_t newDepth) {
  LogQueueDepth(&incastQueueOut, oldDepth, newDepth);
}

void
LogUplinkQueueDepth(uint32_t oldDepth, uint32_t newDepth) {
  LogQueueDepth(&uplinkQueueOut, oldDepth, newDepth);
}

void
LogDctcpAlpha(
    std::ofstream *out,
    uint32_t bytesMarked,
    uint32_t bytesAcked,
    double alpha) {
  (*out) << std::fixed << std::setprecision(9) << Simulator::Now().GetSeconds()
         << " " << alpha << std::endl;
}

void
LogRxDrops(Ptr<const Packet> p) {
  rxDropsOut << std::fixed << std::setprecision(9)
             << Simulator::Now().GetSeconds() << std::endl;
}

int
main(int argc, char *argv[]) {
  // Define log configurations
  LogLevel logConfig =
      (LogLevel)(LOG_PREFIX_LEVEL | LOG_PREFIX_TIME | LOG_PREFIX_NODE | LOG_LEVEL_INFO);
  LogComponentEnable("IncastSim", logConfig);
  LogComponentEnable("IncastAggregator", logConfig);
  LogComponentEnable("IncastSender", logConfig);

  // Initialize variables
  std::string tcpTypeId = "TcpCubic";
  uint32_t numBursts = 5;
  uint32_t numSenders = 10;
  uint32_t bytesPerSender = 500000;
  float delayPerLinkUs = 5;
  uint32_t jitterUs = 100;

  // Parameters for the small links (ToR to node)
  float smallLinkBandwidthMbps = 12500;
  uint32_t smallQueueSizePackets = 2666;
  uint32_t smallQueueMinThresholdPackets = 60;
  uint32_t smallQueueMaxThresholdPackets = 60;

  // Parameters for the large links (ToR to ToR)
  float largeLinkBandwidthMbps = 100000;
  uint32_t largeQueueSizePackets = 2666;
  uint32_t largeQueueMinThresholdPackets = 150;
  uint32_t largeQueueMaxThresholdPackets = 150;

  // Parameters for RWND tuning
  std::string rwndStrategy = "none";
  uint32_t staticRwndBytes = 65535;

  // Define command line arguments
  CommandLine cmd;
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
      "smallQueueSize",
      "Maximum number of packets accepted by queues on the small link",
      smallQueueSizePackets);
  cmd.AddValue(
      "smallQueueMinThreshold",
      "Minimum average length threshold for the small queue (in packets/bytes)",
      smallQueueMinThresholdPackets);
  cmd.AddValue(
      "smallQueueMaxThreshold",
      "Maximum average length threshold for the small queue (in packets/bytes)",
      smallQueueMaxThresholdPackets);
  cmd.AddValue(
      "largeQueueSize",
      "Maximum number of packets accepted by queues on the large link",
      largeQueueSizePackets);
  cmd.AddValue(
      "largeQueueMinThreshold",
      "Minimum average length threshold for the large queue (in packets/bytes)",
      largeQueueMinThresholdPackets);
  cmd.AddValue(
      "largeQueueMaxThreshold",
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
  Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(1448));

  // Important: Must do this before configuring IP addresses.
  NS_LOG_INFO("Creating queues...");

  // Set default parameters for RED queue disc
  Config::SetDefault("ns3::RedQueueDisc::UseEcn", BooleanValue(true));
  Config::SetDefault("ns3::TcpSocketBase::UseEcn", StringValue("On"));

  // ARED may be used but the queueing delays will increase; it is disabled
  // here because the SIGCOMM paper did not mention it
  // Config::SetDefault ("ns3::RedQueueDisc::ARED", BooleanValue (true));
  // Config::SetDefault ("ns3::RedQueueDisc::Gentle", BooleanValue (true));
  Config::SetDefault("ns3::RedQueueDisc::UseHardDrop", BooleanValue(false));
  Config::SetDefault("ns3::RedQueueDisc::MeanPktSize", UintegerValue(1500));

  // Triumph and Scorpion switches used in DCTCP Paper have 4 MB of buffer
  // If every packet is 1500 bytes, 2666 packets can be stored in 4 MB
  // Config::SetDefault(
  //     "ns3::RedQueueDisc::MaxSize", QueueSizeValue(QueueSize("2666p")));
  // DCTCP tracks instantaneous queue length only; so set QW = 1
  Config::SetDefault("ns3::RedQueueDisc::QW", DoubleValue(1));
  // Config::SetDefault("ns3::RedQueueDisc::MinTh", minThresholdValue);
  // Config::SetDefault("ns3::RedQueueDisc::MaxTh", maxThresholdValue);

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

  // Collect all sender addresses
  std::vector<Ipv4Address> allSenderAddresses;
  for (size_t i = 0; i < dumbbellHelper.RightCount(); ++i) {
    allSenderAddresses.push_back(dumbbellHelper.GetRightIpv4Address(i));
  }

  NS_LOG_INFO("Configuring static global routing...");

  // Build the global routing table
  Ipv4GlobalRoutingHelper::PopulateRoutingTables();

  NS_LOG_INFO("Creating applications...");

  // Create the aggregator application
  Ptr<IncastAggregator> aggregatorApp = CreateObject<IncastAggregator>();
  aggregatorApp->SetSenders(allSenderAddresses);
  aggregatorApp->SetStartTime(Seconds(1.0));
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
    senderApp->SetAttribute(
        "NodeID", UintegerValue(dumbbellHelper.GetRight(i)->GetId()));
    senderApp->SetAttribute(
        "Aggregator", Ipv4AddressValue(dumbbellHelper.GetLeftIpv4Address(0)));
    senderApp->SetAttribute("ResponseJitterUs", UintegerValue(jitterUs));
    senderApp->SetAttribute(
        "CCA", TypeIdValue(TypeId::LookupByName("ns3::" + tcpTypeId)));
    senderApp->SetStartTime(Seconds(1.0));
    dumbbellHelper.GetRight(i)->AddApplication(senderApp);
  }

  NS_LOG_INFO("Enabling tracing...");

  // Use nanosecond timestamps for PCAP traces
  Config::SetDefault("ns3::PcapFileWrapper::NanosecMode", BooleanValue(true));

  // Enable tracing at the aggregator
  largeLinkHelper.EnablePcap(
      "scratch/traces/pcap/incast-sockets",
      dumbbellHelper.GetLeft(0)->GetId(),
      0);

  // Enable tracing at each sender
  for (uint32_t i = 0; i < dumbbellHelper.RightCount(); ++i) {
    largeLinkHelper.EnablePcap(
        "scratch/traces/pcap/incast-sockets",
        dumbbellHelper.GetRight(i)->GetId(),
        0);
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
  incastQueueOut.open("scratch/traces/log/incast_queue.log", std::ios::out);
  incastQueueOut << "Time (s) qlen (pkts)" << std::endl;
  incastQueue->TraceConnectWithoutContext(
      "PacketsInQueue", MakeCallback(&LogIncastQueueDepth));
  uplinkQueueOut.open("scratch/traces/log/uplink_queue.log", std::ios::out);
  uplinkQueueOut << "Time (s) qlen (pkts)" << std::endl;
  uplinkQueue->TraceConnectWithoutContext(
      "PacketsInQueue", MakeCallback(&LogUplinkQueueDepth));

  // TODO: Trace alpha from DCTCP
  if (tcpTypeId == "TcpDctcp") {
    dctcpAlphaOut.open("scratch/traces/log/dctcp_alpha.log", std::ios::out);
    dctcpAlphaOut << "Time (s) Alpha" << std::endl;

    std::ostringstream pathStream;
    pathStream << "/NodeList/" << dumbbellHelper.GetLeft(0)->GetId() << "/"
               << "$ns3::TcpL4Protocol/SocketList/0/"
               << "CongestionOps/$ns3::TcpDctcp/CongestionEstimate";

    std::cout << pathStream.str() << std::endl;

    Config::ConnectWithoutContext(
        pathStream.str(), MakeBoundCallback(&LogDctcpAlpha, &dctcpAlphaOut));
  }

  // TODO: Trace packet drops during reception
  rxDropsOut.open("scratch/traces/log/incast_rxdrops.log", std::ios::out);
  rxDropsOut << "Drop Times (s)" << std::endl;
  dumbbellHelper.GetLeftRouterDevices().Get(0)->TraceConnectWithoutContext(
      "PhyRxDrop", MakeCallback(&LogRxDrops));

  // Run the simulator
  Simulator::Run();
  Simulator::Destroy();

  // Close streams
  incastQueueOut.close();
  uplinkQueueOut.close();
  dctcpAlphaOut.close();
  rxDropsOut.close();

  NS_LOG_INFO("Finished simulation.");

  // Compute the ideal and actual burst durations
  uint32_t numBitsPerByte = 8;
  uint32_t numHops = 3;
  uint32_t baseToMilli = pow(10, 3);
  uint32_t baseToMicro = pow(10, 6);

  double burstTransmissionSec = (double)bytesPerSender * numSenders *
                                numBitsPerByte /
                                (smallLinkBandwidthMbps * megaToBase);
  double firstRttSec = numHops * 2 * delayPerLinkUs / baseToMicro;
  double idealBurstDurationSec = burstTransmissionSec + firstRttSec;

  NS_LOG_INFO(
      "Ideal burst duration: " << idealBurstDurationSec * baseToMilli << "ms");

  NS_LOG_INFO("Burst durations (x ideal):");
  for (const auto &burstDurationSec : aggregatorApp->GetBurstDurations()) {
    NS_LOG_INFO(
        burstDurationSec.As(Time::MS)
        << " (" << burstDurationSec.GetSeconds() / idealBurstDurationSec
        << "x)");
  }

  return 0;
}