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
//     $ NS_LOG='' ./ns3 run "scratch/incast-sockets --totalBytes=31250
//           --numBursts=5 --numSenders=500 --bwMbps=12500"

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
 */

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("IncastSim");

int
main(int argc, char *argv[]) {
  LogLevel logConfig = (LogLevel)(LOG_PREFIX_LEVEL | LOG_PREFIX_TIME |
                                  LOG_PREFIX_NODE | LOG_LEVEL_INFO);
  LogComponentEnable("IncastSim", logConfig);
  LogComponentEnable("IncastAggregator", logConfig);
  LogComponentEnable("IncastSender", logConfig);

  // Initialize variables
  uint32_t numSenders = 3;
  // uint32_t bufferSize = 32768;
  uint32_t burstBytes = 4096;
  uint32_t unitSize = 3000;
  uint16_t maxWin = 65535;
  uint32_t numBursts = 10;
  uint32_t jitterUs = 0;
  float delayUs = 25;  // TODO: reconfigure
  float smallBandwidthMbps = 12.5;
  float largeBandwidthMbps = 800.0;

  // Define command line arguments
  CommandLine cmd;
  cmd.AddValue("numSenders", "Number of incast senders", numSenders);
  // cmd.AddValue("buffersize", "Drop-tail queue buffer size in bytes",
  // bufferSize);
  cmd.AddValue("burstBytes",
               "Number of bytes for each worker to send for each burst",
               burstBytes);
  cmd.AddValue("smallBandwidthMbps",
               "Small link bandwidth (in Mbps)",
               smallBandwidthMbps);
  cmd.AddValue("largeBandwidthMbps",
               "Large link bandwidth (in Mbps)",
               largeBandwidthMbps);
  cmd.AddValue("unitSize",
               "Size of virtual bytes increment upon SYN packets",
               unitSize);
  cmd.AddValue("maxWin", "Maximum size of advertised window", maxWin);
  cmd.AddValue("numBursts", "Number of bursts to simulate", numBursts);
  cmd.AddValue(
      "jitterUs",
      "Max random jitter in sending request and responses, in microseconds",
      jitterUs);
  cmd.Parse(argc, argv);

  uint32_t totalIncastMbps = smallBandwidthMbps * numSenders;
  if (totalIncastMbps > largeBandwidthMbps) {
    NS_LOG_WARN("Total incast bandwidth ("
                << totalIncastMbps << "Mbps) exceeds large link bandwidth ("
                << largeBandwidthMbps << "Mbps)");
  }

  NS_LOG_INFO("Building incast topology...");

  // Convert float values to string values
  std::ostringstream delayUsString;
  delayUsString << delayUs << "us";
  StringValue delayUsStringValue = StringValue(delayUsString.str());

  std::ostringstream smallBandwidthMbpsString;
  smallBandwidthMbpsString << smallBandwidthMbps << "Mbps";
  StringValue smallBandwidthMbpsStringValue =
      StringValue(smallBandwidthMbpsString.str());

  std::ostringstream largeBandwidthMbpsString;
  largeBandwidthMbpsString << largeBandwidthMbps << "Mbps";
  StringValue largeBandwidthMbpsStringValue =
      StringValue(largeBandwidthMbpsString.str());

  // Create links
  PointToPointHelper smallLink;
  smallLink.SetDeviceAttribute("DataRate", smallBandwidthMbpsStringValue);
  smallLink.SetChannelAttribute("Delay", delayUsStringValue);
  // smallLink.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue("150p"));

  PointToPointHelper largeLink;
  largeLink.SetDeviceAttribute("DataRate", largeBandwidthMbpsStringValue);
  largeLink.SetChannelAttribute("Delay", delayUsStringValue);
  // largeLink.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue("150p"));

  // Create dumbbell topology
  PointToPointDumbbellHelper dumbbellHelper(1,
                                            smallLink,
                                            numSenders,
                                            smallLink,
                                            largeLink);

  NS_LOG_INFO("Installing TCP stack on all nodes...");

  // Install TCP stack on all nodes
  InternetStackHelper stack;

  for (uint32_t i = 0; i < dumbbellHelper.LeftCount(); ++i) {
    stack.Install(dumbbellHelper.GetLeft(i));
  }
  for (uint32_t i = 0; i < dumbbellHelper.RightCount(); ++i) {
    stack.Install(dumbbellHelper.GetRight(i));
  }

  stack.Install(dumbbellHelper.GetLeft());
  stack.Install(dumbbellHelper.GetRight());

  NS_LOG_INFO("Assigning IP addresses...");

  // Assign IP Addresses
  dumbbellHelper.AssignIpv4Addresses(
      Ipv4AddressHelper("10.0.0.0", "255.255.255.0"),
      Ipv4AddressHelper("10.1.0.0", "255.255.255.0"),
      Ipv4AddressHelper("10.2.0.0", "255.255.255.0"));

  NS_LOG_INFO("Configuring queue settings...");

  TrafficControlHelper redHelper;
  redHelper.SetRootQueueDisc("ns3::RedQueueDisc",
                             "LinkBandwidth",
                             largeBandwidthMbpsStringValue,
                             "LinkDelay",
                             delayUsStringValue);
  NetDeviceContainer switchDevices =
      largeLink.Install(dumbbellHelper.GetLeft(), dumbbellHelper.GetRight());
  QueueDiscContainer queueDiscs1 = redHelper.Install(switchDevices);

  // TODO: Add the same red queue disc to all of the other links as well
  // TODO: Two separate configurations, one for small and one for large

  NS_LOG_INFO("Creating applications...");

  // Collect sender addresses
  std::list<Ipv4Address> senderAddresses;

  for (size_t i = 0; i < dumbbellHelper.RightCount(); ++i) {
    senderAddresses.push_back(dumbbellHelper.GetRightIpv4Address(i));
  }

  // Create the aggregator application
  Ptr<IncastAggregator> aggregatorApp = CreateObject<IncastAggregator>();
  aggregatorApp->SetSenders(senderAddresses);
  aggregatorApp->SetStartTime(Seconds(1.0));
  aggregatorApp->SetAttribute("NumBursts", UintegerValue(numBursts));
  aggregatorApp->SetAttribute("BurstBytes", UintegerValue(burstBytes));
  aggregatorApp->SetAttribute("RequestJitterUs", UintegerValue(jitterUs));
  dumbbellHelper.GetLeft(0)->AddApplication(aggregatorApp);

  // Create the sender applications
  for (size_t i = 0; i < dumbbellHelper.RightCount(); ++i) {
    Ptr<IncastSender> senderApp = CreateObject<IncastSender>();
    senderApp->SetAttribute(
        "Aggregator",
        Ipv4AddressValue(dumbbellHelper.GetLeftIpv4Address(0)));
    senderApp->SetAttribute("ResponseJitterUs", UintegerValue(jitterUs));
    senderApp->SetStartTime(Seconds(1.0));
    dumbbellHelper.GetRight(i)->AddApplication(senderApp);
  }

  NS_LOG_INFO("Enabling static global routing...");

  // Turn on global static routing
  Ipv4GlobalRoutingHelper::PopulateRoutingTables();

  NS_LOG_INFO("Enabling tracing...");

  // Use nanosecond timestamps for PCAP traces
  Config::SetDefault("ns3::PcapFileWrapper::NanosecMode", BooleanValue(true));
  // Enable tracing at the aggregator.
  largeLink.EnablePcap("scratch/traces/incast-sockets", 2, 0);

  NS_LOG_INFO("Configuring various default parameters...");
  // Set the maximum segment size to 1448 bytes
  Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(1448));

  // Config::SetDefault("ns3::TcpSocketBase::MaxWindowSize",
  //                    UintegerValue(maxWin));
  // Config::SetDefault ("ns3::TcpNewReno::ReTxThreshold", UintegerValue(2));
  // Config::SetDefault ("ns3::TcpSocket::DelAckCount", UintegerValue(0));

  // Set default parameters for RED queue disc
  Config::SetDefault("ns3::RedQueueDisc::UseEcn", BooleanValue(true));
  // ARED may be used but the queueing delays will increase; it is disabled
  // here because the SIGCOMM paper did not mention it
  // Config::SetDefault ("ns3::RedQueueDisc::ARED", BooleanValue (true));
  // Config::SetDefault ("ns3::RedQueueDisc::Gentle", BooleanValue (true));
  Config::SetDefault("ns3::RedQueueDisc::UseHardDrop", BooleanValue(false));
  Config::SetDefault("ns3::RedQueueDisc::MeanPktSize", UintegerValue(1500));
  // Triumph and Scorpion switches used in DCTCP Paper have 4 MB of buffer
  // If every packet is 1500 bytes, 2666 packets can be stored in 4 MB
  Config::SetDefault("ns3::RedQueueDisc::MaxSize",
                     QueueSizeValue(QueueSize("2666p")));
  // DCTCP tracks instantaneous queue length only; so set QW = 1
  Config::SetDefault("ns3::RedQueueDisc::QW", DoubleValue(1));
  Config::SetDefault("ns3::RedQueueDisc::MinTh", DoubleValue(20));
  Config::SetDefault("ns3::RedQueueDisc::MaxTh", DoubleValue(20));

  NS_LOG_INFO("Run Simulation.");
  Simulator::Run();
  Simulator::Stop();
  Simulator::Destroy();
  NS_LOG_INFO("Done.");

  double idealBurstDurationSec =
      // Time to transmit the burst.
      (double)burstBytes * numSenders * 8 / (smallBandwidthMbps * 1000000) +
      // 1 RTT for the request and the first response to propgate.
      6 * delayUs / 1000000;
  NS_LOG_INFO("Ideal burst duration: " << idealBurstDurationSec * 1000 << "ms");
  NS_LOG_INFO("Burst durations (x ideal):");
  for (const auto &burstDurationSec : aggregatorApp->GetBurstDurations()) {
    NS_LOG_INFO("\t" << burstDurationSec.As(Time::MS) << " ("
                     << burstDurationSec.GetSeconds() / idealBurstDurationSec
                     << "x)");
  }

  return 0;
}