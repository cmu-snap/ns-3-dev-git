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
//       --numSenders=100 --smallBandwidthMbps=12500
//       --largeBandwidthMbps=100000"

#include "ns3/applications-module.h"
#include "ns3/core-module.h"

#include <fstream>
#include <iomanip>
#include <iostream>
// #include "ns3/drop-tail-queue.h"
#include "ns3/incast-aggregator.h"
#include "ns3/incast-sender.h"
#include "ns3/internet-module.h"
// #include "ns3/netanim-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-layout-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/traffic-control-module.h"

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

void
CheckQueueSize(
    std::string queueName, Ptr<QueueDisc> queue, float bandwidthMbps) {
  uint32_t packetsPerQueue = queue->GetNPackets();
  uint32_t bytesPerPacket = 1500;
  uint32_t bitsPerByte = 8;
  uint32_t gigaToMega = pow(10, 3);

  Time backlog = Seconds(
      static_cast<double>(packetsPerQueue * bytesPerPacket * bitsPerByte) /
      (bandwidthMbps * gigaToMega));

  std::ofstream *out;
  if (queueName == "incastQueue") {
    out = &incastQueueOut;
  } else if (queueName == "uplinkQueue") {
    out = &uplinkQueueOut;
  } else {
    NS_ABORT_MSG("Unknown queue name: " << queueName);
  }

  // Report the queue size in units of packets and ms
  (*out) << std::fixed << std::setprecision(6) << Simulator::Now().GetSeconds()
         << " " << packetsPerQueue << " " << backlog.GetMicroSeconds() << std::endl;

  // Check queue size every 1/1000 of a second
  Simulator::Schedule(
      MicroSeconds(100), &CheckQueueSize, queueName, queue, bandwidthMbps);
}

int
main(int argc, char *argv[]) {
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
  float perLinkDelayUs = 5;
  uint32_t jitterUs = 100;

  // Parameters for the small links (ToR to node)
  float smallBandwidthMbps = 12500;
  uint32_t smallQueueSize = 2666;
  uint32_t smallMinThreshold = 60;
  uint32_t smallMaxThreshold = 60;

  // Parameters for the large links (ToR to ToR)
  float largeBandwidthMbps = 100000;
  uint32_t largeQueueSize = 2666;
  uint32_t largeMinThreshold = 150;
  uint32_t largeMaxThreshold = 150;

  // RWND tuning parameters
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
      "smallBandwidthMbps",
      "Small link bandwidth (in Mbps)",
      smallBandwidthMbps);
  cmd.AddValue(
      "largeBandwidthMbps",
      "Large link bandwidth (in Mbps)",
      largeBandwidthMbps);
  cmd.AddValue(
      "smallQueueSize",
      "Maximum number of packets accepted by queues on the small link",
      smallQueueSize);
  cmd.AddValue(
      "smallMinThreshold",
      "Minimum average length threshold (in packets/bytes)",
      smallMinThreshold);
  cmd.AddValue(
      "smallMaxThreshold",
      "Maximum average length threshold (in packets/bytes)",
      smallMaxThreshold);
  cmd.AddValue(
      "largeQueueSize",
      "Maximum number of packets accepted by queues on the large link",
      largeQueueSize);
  cmd.AddValue(
      "largeMinThreshold",
      "Minimum average length threshold (in packets/bytes)",
      largeMinThreshold);
  cmd.AddValue(
      "largeMaxThreshold",
      "Maximum average length threshold (in packets/bytes)",
      largeMaxThreshold);
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
  uint32_t totalIncastMbps = smallBandwidthMbps * numSenders;

  if (totalIncastMbps > largeBandwidthMbps) {
    NS_LOG_WARN(
        "Total incast bandwidth (" << totalIncastMbps
                                   << "Mbps) exceeds large link bandwidth ("
                                   << largeBandwidthMbps << "Mbps)");
  }

  // Convert numeric values to NS3 values
  std::ostringstream perLinkDelayUsString;
  perLinkDelayUsString << perLinkDelayUs << "us";
  StringValue perLinkDelayUsStringValue =
      StringValue(perLinkDelayUsString.str());

  std::ostringstream smallBandwidthMbpsString;
  smallBandwidthMbpsString << smallBandwidthMbps << "Mbps";
  StringValue smallBandwidthMbpsStringValue =
      StringValue(smallBandwidthMbpsString.str());

  std::ostringstream largeBandwidthMbpsString;
  largeBandwidthMbpsString << largeBandwidthMbps << "Mbps";
  StringValue largeBandwidthMbpsStringValue =
      StringValue(largeBandwidthMbpsString.str());

  std::ostringstream smallQueueSizeString;
  smallQueueSizeString << smallQueueSize << "p";
  QueueSizeValue smallQueueSizeValue =
      QueueSizeValue(QueueSize(smallQueueSizeString.str()));

  std::ostringstream largeQueueSizeString;
  largeQueueSizeString << largeQueueSize << "p";
  QueueSizeValue largeQueueSizeValue =
      QueueSizeValue(QueueSize(largeQueueSizeString.str()));

  NS_LOG_INFO("Building incast topology...");

  // Create links
  PointToPointHelper smallLinkHelper;
  smallLinkHelper.SetDeviceAttribute("DataRate", smallBandwidthMbpsStringValue);
  smallLinkHelper.SetChannelAttribute("Delay", perLinkDelayUsStringValue);

  PointToPointHelper largeLinkHelper;
  largeLinkHelper.SetDeviceAttribute("DataRate", largeBandwidthMbpsStringValue);
  largeLinkHelper.SetChannelAttribute("Delay", perLinkDelayUsStringValue);

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

  // Configure a different queues for the large and small links.

  TrafficControlHelper smallLinkQueueHelper;
  smallLinkQueueHelper.SetRootQueueDisc(
      "ns3::RedQueueDisc",
      "LinkBandwidth",
      smallBandwidthMbpsStringValue,
      "LinkDelay",
      perLinkDelayUsStringValue,
      "MaxSize",
      smallQueueSizeValue,
      "MinTh",
      DoubleValue(smallMinThreshold),
      "MaxTh",
      DoubleValue(smallMaxThreshold),
      "LInterm", DoubleValue(100));

  TrafficControlHelper largeLinkQueueHelper;
  largeLinkQueueHelper.SetRootQueueDisc(
      "ns3::RedQueueDisc",
      "LinkBandwidth",
      largeBandwidthMbpsStringValue,
      "LinkDelay",
      perLinkDelayUsStringValue,
      "MaxSize",
      largeQueueSizeValue,
      "MinTh",
      DoubleValue(largeMinThreshold),
      "MaxTh",
      DoubleValue(largeMaxThreshold),
      "LInterm", DoubleValue(100));

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
      "BandwidthMbps", UintegerValue(smallBandwidthMbps));
  aggregatorApp->SetAttribute(
      "PhysicalRTT", TimeValue(MicroSeconds(6 * perLinkDelayUs)));
  dumbbellHelper.GetLeft(0)->AddApplication(aggregatorApp);

  // Create the sender applications
  for (size_t i = 0; i < dumbbellHelper.RightCount(); ++i) {
    Ptr<IncastSender> senderApp = CreateObject<IncastSender>();
    senderApp->SetAttribute("NodeID", UintegerValue(dumbbellHelper.GetRight(i)->GetId()));
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
      "scratch/traces/incast-sockets", dumbbellHelper.GetLeft(0)->GetId(), 0);

  // Enable tracing at each sender
  for (uint32_t i = 0; i < dumbbellHelper.RightCount(); ++i) {
    largeLinkHelper.EnablePcap(
        "scratch/traces/incast-sockets",
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
  incastQueueOut.open("scratch/traces/incast_queue.log", std::ios::out);
  incastQueueOut << "#Time(s) qlen(pkts) qlen(us)" << std::endl;
  Simulator::Schedule(
      Seconds(0),
      &CheckQueueSize,
      "incastQueue",
      incastQueue,
      smallBandwidthMbps);
  uplinkQueueOut.open("scratch/traces/uplink_queue.log", std::ios::out);
  uplinkQueueOut << "#Time(s) qlen(pkts) qlen(us)" << std::endl;
  Simulator::Schedule(
      Seconds(0),
      &CheckQueueSize,
      "uplinkQueue",
      uplinkQueue,
      largeBandwidthMbps);

  Simulator::Run();
  Simulator::Destroy();

  incastQueueOut.close();
  uplinkQueueOut.close();

  NS_LOG_INFO("Finished simulation.");

  // Compute the ideal and actual burst durations
  int numBitsInByte = 8;
  int numHops = 3;
  int numMsInSec = 1000;

  double idealBurstDurationSec =
      // Time to transmit the burst.
      (double)bytesPerSender * numSenders * numBitsInByte /
          (smallBandwidthMbps * megaToBase) +
      // 1 RTT for the request and the first response packet to propgate.
      numHops * 2 * perLinkDelayUs / megaToBase;

  NS_LOG_INFO(
      "Ideal burst duration: " << idealBurstDurationSec * numMsInSec << "ms");

  NS_LOG_INFO("Burst durations (x ideal):");
  for (const auto &burstDurationSec : aggregatorApp->GetBurstDurations()) {
    NS_LOG_INFO(
        burstDurationSec.As(Time::MS)
        << " (" << burstDurationSec.GetSeconds() / idealBurstDurationSec
        << "x)");
  }

  return 0;
}