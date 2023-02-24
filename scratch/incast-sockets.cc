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
//     $ NS_LOG='' ./ns3 run "scratch/incast-sockets --totalBytes=31250 \
//           --numBursts=5 --numSenders=500 --bwMbps=12500"

#include <fstream>
#include <iomanip>
#include <iostream>

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/drop-tail-queue.h"
#include "ns3/incast-aggregator.h"
#include "ns3/incast-sender.h"
#include "ns3/internet-module.h"
#include "ns3/netanim-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-layout-module.h"
#include "ns3/point-to-point-module.h"

// Network topology (default)
//
//        n2 n3 n4              .
//         \ | /                .
//          \|/                 .
//     n1--- n0---n5            .
//          /|\                 .
//         / | \                .
//        n8 n7 n6              .
//

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("IncastSim");

int main(int argc, char* argv[]) {
  uint32_t numSenders = 3;
  // uint32_t bufferSize = 32768;
  uint32_t totalBytes = 4096;
  uint32_t unitSize = 3000;
  uint16_t maxWin = 65535;
  bool useStdout = false;
  uint32_t numBursts = 10;
  uint32_t jitterUs = 0;
  float bwMbps = 12.5;

  CommandLine cmd;
  cmd.AddValue("numSenders", "Number of incast senders", numSenders);
  cmd.AddValue("useStdout", "Output packet trace to stdout", useStdout);
  // cmd.AddValue("buffersize", "Drop-tail queue buffer size in bytes",
  // bufferSize);
  cmd.AddValue("totalBytes", "Number of bytes to send for each burst",
               totalBytes);
  cmd.AddValue("bwMbps", "Link bandwidth, in Mbps", bwMbps);
  cmd.AddValue("unitSize", "Size of virtual bytes increment upon SYN packets",
               unitSize);
  cmd.AddValue("maxWin", "Maximum size of advertised window", maxWin);
  cmd.AddValue("numBursts", "Number of bursts to simulate", numBursts);
  cmd.AddValue(
      "jitterUs",
      "Max random jitter in sending request and responses, in microseconds",
      jitterUs);
  cmd.Parse(argc, argv);

  std::ostringstream bwMbps_str;
  bwMbps_str << bwMbps << "Mbps";

  // Use nanosecond timestamps for PCAP traces
  Config::SetDefault("ns3::PcapFileWrapper::NanosecMode", BooleanValue(true));

  NS_LOG_INFO("Build star topology.");
  PointToPointHelper pointToPoint;
  pointToPoint.SetDeviceAttribute("DataRate", StringValue(bwMbps_str.str()));
  // pointToPoint.SetDeviceAttribute ("UnitSize", UintegerValue (unitsize));
  pointToPoint.SetChannelAttribute("Delay", StringValue("25us"));
  // pointToPoint.SetQueue("ns3::DropTailQueue",
  //   // "Mode", EnumValue(DropTailQueue::BYTES),
  //   "MaxBytes", UintegerValue(bufferSize));
  PointToPointStarHelper star(numSenders + 1, pointToPoint);

  NS_LOG_INFO("Install internet stack on all nodes.");
  Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(1448));
  Config::SetDefault("ns3::TcpSocketBase::MaxWindowSize",
                     UintegerValue(maxWin));
  // Config::SetDefault ("ns3::TcpNewReno::ReTxThreshold", UintegerValue(2));
  // Config::SetDefault ("ns3::TcpSocket::DelAckCount", UintegerValue(0));
  InternetStackHelper internet;
  star.InstallStack(internet);

  NS_LOG_INFO("Assign IP Addresses.");
  star.AssignIpv4Addresses(Ipv4AddressHelper("10.1.1.0", "255.255.255.0"));
  std::list<Ipv4Address> senders;
  for (uint32_t i = 1; i <= numSenders; i++) {
    NS_LOG_INFO("Sender IP " << star.GetSpokeIpv4Address(i));
    senders.push_back(star.GetSpokeIpv4Address(i));
  };

  NS_LOG_INFO("Create applications.");

  // Create a packet sink on spoke 0 to receive packets.
  Ptr<IncastAggregator> app = CreateObject<IncastAggregator>();
  app->SetSenders(senders);
  app->SetStartTime(Seconds(1.0));
  app->SetAttribute("NumBursts", UintegerValue(numBursts));
  app->SetAttribute("BurstBytes", UintegerValue(totalBytes));
  app->SetAttribute("RequestJitterUs", UintegerValue(jitterUs));
  star.GetSpokeNode(0)->AddApplication(app);

  // Create send applications to send TCP to spoke 0
  for (uint32_t i = 1; i < star.SpokeCount(); ++i) {
    Ptr<IncastSender> sendApp = CreateObject<IncastSender>();
    sendApp->SetAttribute("Aggregator",
                          Ipv4AddressValue(star.GetSpokeIpv4Address(0)));
    sendApp->SetAttribute("ResponseJitterUs", UintegerValue(jitterUs));
    sendApp->SetStartTime(Seconds(1.0));
    star.GetSpokeNode(i)->AddApplication(sendApp);
  }

  NS_LOG_INFO("Enable static global routing.");

  // Turn on global static routing
  Ipv4GlobalRoutingHelper::PopulateRoutingTables();

  // Enable tracing across the middle link
  NS_LOG_INFO("Enabling tracing...");
  pointToPoint.EnablePcap("scratch/traces/incast-sockets", 0, 0);

  // LogComponentEnableAll(LOG_PREFIX_TIME);

  NS_LOG_INFO("Run Simulation.");
  Simulator::Run();
  Simulator::Stop();
  Simulator::Destroy();
  NS_LOG_INFO("Done.");

  std::cout << "Ideal burst duration: "
            << (double)totalBytes * numSenders * 8 / (bwMbps * 1000) << "ms"
            << std::endl;
  std::cout << "Burst durations:" << std::endl;
  for (const auto& burstDurationSec : app->GetBurstDurations()) {
    std::cout << "\t" << burstDurationSec.As(Time::MS) << std::endl;
  }

  return 0;
}