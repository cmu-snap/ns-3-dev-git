/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright 2011 (c) New York University
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
 * Author: Adrian Sai-wah Tam <adrian.sw.tam@google.com>
 *
 */

#include <iostream>
#include <iomanip>
#include <fstream>
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/netanim-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/point-to-point-layout-module.h"
#include "ns3/drop-tail-queue.h"
#include "ns3/incast-agg.h"
#include "ns3/incast-send.h"

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

NS_LOG_COMPONENT_DEFINE ("IncastSim");

int
main (int argc, char *argv[])
{
  //
  // Default number of nodes in the star.  Overridable by command line argument.
  //
  uint32_t nIncaster = 3;     // Number of incast senders
  uint32_t bufSize = 32768;
  uint32_t sru = 4096;
  uint32_t unitsize = 3000;
  uint16_t maxwin = 65535;
  bool useStdout = true;

  CommandLine cmd;
  cmd.AddValue ("incaster", "Number of incast senders", nIncaster);
  cmd.AddValue ("stdout", "Output packet trace to stdout", useStdout);
  cmd.AddValue ("bufsize", "Drop-tail queue buffer size in bytes", bufSize);
  cmd.AddValue ("sru", "Size of server request unit in bytes", sru);
  cmd.AddValue ("unitsize", "Size of virtual bytes increment upon SYN packets", unitsize);
  cmd.AddValue ("maxwin", "Maximum size of advertised window", maxwin);

  cmd.Parse (argc, argv);

  NS_LOG_INFO ("Build star topology.");
  PointToPointHelper pointToPoint;
  pointToPoint.SetDeviceAttribute ("DataRate", StringValue ("1000Mbps"));
  // pointToPoint.SetDeviceAttribute ("UnitSize", UintegerValue (unitsize));
  pointToPoint.SetChannelAttribute ("Delay", StringValue ("25us"));
  // pointToPoint.SetQueue("ns3::DropTailQueue",
  //   // "Mode", EnumValue(DropTailQueue::BYTES),
  //   "MaxBytes", UintegerValue(bufSize));
  PointToPointStarHelper star (nIncaster+1, pointToPoint);

  NS_LOG_INFO ("Install internet stack on all nodes.");
  Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue(1448));
  Config::SetDefault ("ns3::TcpSocketBase::MaxWindowSize", UintegerValue(maxwin));
  // Config::SetDefault ("ns3::TcpNewReno::ReTxThreshold", UintegerValue(2));
  //Config::SetDefault ("ns3::TcpSocket::DelAckCount", UintegerValue(0));
  InternetStackHelper internet;
  star.InstallStack (internet);

  NS_LOG_INFO ("Assign IP Addresses.");
  star.AssignIpv4Addresses (Ipv4AddressHelper ("10.1.1.0", "255.255.255.0"));
  std::list<Ipv4Address> senders;
  for (uint32_t i=1; i <= nIncaster; i++)
    {
      NS_LOG_INFO ("Sender IP " << star.GetSpokeIpv4Address (i));
      senders.push_back (star.GetSpokeIpv4Address (i));
    };


  NS_LOG_INFO ("Create applications.");

  //
  // Create a packet sink on spoke 0 to receive packets.
  //
  Ptr<IncastAggregator> app = CreateObject<IncastAggregator> ();
  app->SetSenders (senders);
  app->SetStartTime (Seconds (1.0));
  star.GetSpokeNode (0)->AddApplication(app);

  //
  // Create send applications to send TCP to spoke 0, one on each other spoke node.
  //
  for (uint32_t i = 1; i < star.SpokeCount (); ++i)
    {
      Ptr<IncastSender> sendApp = CreateObject<IncastSender> ();
      sendApp->SetAttribute ("Aggregator", Ipv4AddressValue(star.GetSpokeIpv4Address (0)));
      sendApp->SetAttribute ("SRU", UintegerValue(sru));
      sendApp->SetStartTime (Seconds (1.0));
      star.GetSpokeNode (i)->AddApplication(sendApp);
    }

  NS_LOG_INFO ("Enable static global routing.");
  //
  // Turn on global static routing so we can actually be routed across the star.
  //
  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  //
  // Turn on trace
  //
  if (useStdout)
    {
      std::cout << std::setprecision(9) << std::fixed;
      std::cerr << std::setprecision(9) << std::fixed;
      std::clog << std::setprecision(9) << std::fixed;
      pointToPoint.EnableAsciiAll (Create<OutputStreamWrapper>(&std::clog));
    }
  else
    {
      AsciiTraceHelper ascii;
      Ptr<OutputStreamWrapper> tracefile = ascii.CreateFileStream ("incast.tr");
      *(tracefile->GetStream()) << std::setprecision(9) << std::fixed;
      pointToPoint.EnableAsciiAll (tracefile);
    }

  NS_LOG_INFO ("Run Simulation.");
  Simulator::Run ();
  Simulator::Destroy ();
  NS_LOG_INFO ("Done.");

  return 0;
}