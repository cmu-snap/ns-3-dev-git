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

#include "incast-aggregator.h"

#include "ns3/boolean.h"
#include "ns3/internet-module.h"
#include "ns3/log.h"
#include "ns3/pointer.h"
#include "ns3/string.h"
#include "ns3/tcp-congestion-ops.h"
#include "ns3/uinteger.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <unistd.h>

NS_LOG_COMPONENT_DEFINE("IncastAggregator");

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED(IncastAggregator);

void
IncastAggregator::LogCwnd(uint32_t oldCwndBytes, uint32_t newCwndBytes) {
  NS_LOG_FUNCTION(this << " old: " << oldCwndBytes << " new: " << newCwndBytes);

  m_cwndLog.push_back({Simulator::Now(), newCwndBytes});
}

void
IncastAggregator::LogRtt(Time oldRtt, Time newRtt) {
  NS_LOG_FUNCTION(this << " old: " << oldRtt << " new: " << newRtt);

  m_rttLog.push_back({Simulator::Now(), newRtt});
}

void
IncastAggregator::LogBytesInAck(
    Ipv4Address sender_ip,
    uint16_t sender_port,
    Ipv4Address aggregator_ip,
    uint16_t aggregator_port,
    uint32_t bytesInAck) {
  struct bytesInAckEntry entry;
  entry.time = Simulator::Now();
  entry.sender_ip = sender_ip;
  entry.sender_port = sender_port;
  entry.aggregator_ip = aggregator_ip;
  entry.aggregator_port = aggregator_port;
  entry.bytesInAck = bytesInAck;
  m_bytesInAckLog.push_back(entry);
}

TypeId
IncastAggregator::GetTypeId() {
  static TypeId tid =
      TypeId("ns3::IncastAggregator")
          .SetParent<Application>()
          .AddConstructor<IncastAggregator>()
          .AddAttribute(
              "OutputDirectory",
              "Directory for all log and pcap traces",
              StringValue("output_directory/"),
              MakeStringAccessor(&IncastAggregator::m_outputDirectory),
              MakeStringChecker())
          .AddAttribute(
              "TraceDirectory",
              "Sub-directory for this experiment's log and pcap traces",
              StringValue("trace_directory/"),
              MakeStringAccessor(&IncastAggregator::m_traceDirectory),
              MakeStringChecker())
          .AddAttribute(
              "NumBursts",
              "Number of bursts to simulate",
              UintegerValue(10),
              MakeUintegerAccessor(&IncastAggregator::m_numBursts),
              MakeUintegerChecker<uint32_t>())
          .AddAttribute(
              "BytesPerBurstSender",
              "Number of bytes to request from each sender for each burst",
              UintegerValue(MSS),
              MakeUintegerAccessor(&IncastAggregator::m_bytesPerBurstSender),
              MakeUintegerChecker<uint32_t>())
          .AddAttribute(
              "RequestJitterUs",
              "Maximum random jitter when sending requests (in microseconds)",
              UintegerValue(0),
              MakeUintegerAccessor(&IncastAggregator::m_requestJitterUs),
              MakeUintegerChecker<uint32_t>())
          .AddAttribute(
              "Port",
              "TCP port for all applications",
              UintegerValue(8888),
              MakeUintegerAccessor(&IncastAggregator::m_port),
              MakeUintegerChecker<uint16_t>())
          .AddAttribute(
              "Protocol",
              "TypeId of the protocol used",
              TypeIdValue(TcpSocketFactory::GetTypeId()),
              MakeTypeIdAccessor(&IncastAggregator::m_tid),
              MakeTypeIdChecker())
          .AddAttribute(
              "CCA",
              "TypeId of the CCA used by the burst senders",
              TypeIdValue(TcpSocketFactory::GetTypeId()),
              MakeTypeIdAccessor(&IncastAggregator::m_burstSenderCca),
              MakeTypeIdChecker())
          .AddAttribute(
              "RwndStrategy",
              "RWND tuning strategy to use [none, static, bdp+connections, "
              "scheduled]",
              StringValue("none"),
              MakeStringAccessor(&IncastAggregator::m_rwndStrategy),
              MakeStringChecker())
          .AddAttribute(
              "StaticRwndBytes",
              "If RwndStrategy=static, then use this static RWND value",
              UintegerValue(MAX_RWND),
              MakeUintegerAccessor(&IncastAggregator::m_staticRwndBytes),
              MakeUintegerChecker<uint32_t>())
          .AddAttribute(
              "BandwidthMbps",
              "If RwndStrategy=bdp+connections, then assume that this is the "
              "bottleneck bandwidth",
              UintegerValue(0),
              MakeUintegerAccessor(&IncastAggregator::m_bandwidthMbps),
              MakeUintegerChecker<uint32_t>())
          .AddAttribute(
              "RwndScheduleMaxTokens",
              "If RwndStrategy==scheduled, then this is the max number of "
              "senders that are allowed to transmit concurrently.",
              UintegerValue(20),
              MakeUintegerAccessor(&IncastAggregator::m_rwndScheduleMaxTokens),
              MakeUintegerChecker<uint32_t>())
          .AddAttribute(
              "PhysicalRTT",
              "Physical RTT",
              TimeValue(Seconds(0)),
              MakeTimeAccessor(&IncastAggregator::m_physicalRtt),
              MakeTimeChecker())
          .AddAttribute(
              "FirstFlowOffset",
              "Time to delay the request for the first sender in each burst.",
              TimeValue(MilliSeconds(0)),
              MakeTimeAccessor(&IncastAggregator::m_firstFlowOffset),
              MakeTimeChecker());

  // TODO: Configure scheduled RWND tuning parameters.

  return tid;
}

IncastAggregator::IncastAggregator()
    : m_numBursts(10),
      m_bytesPerBurstSender(MSS),
      m_totalBytesSoFar(0),
      m_port(8888),
      m_requestJitterUs(0),
      m_rwndStrategy("none"),
      m_staticRwndBytes(MAX_RWND),
      m_bandwidthMbps(0),
      m_physicalRtt(Seconds(0)),
      m_minRtt(Seconds(0)),
      m_probingRtt(false),
      m_firstFlowOffset(MilliSeconds(0)) {
  NS_LOG_FUNCTION(this);
}

IncastAggregator::~IncastAggregator() { NS_LOG_FUNCTION(this); }

void
IncastAggregator::DoDispose() {
  NS_LOG_FUNCTION(this);

  m_burstSockets.clear();
  Application::DoDispose();
}

void
IncastAggregator::SetBurstSenders(
    std::unordered_map<uint32_t, std::pair<Ptr<IncastSender>, Ipv4Address>>
        *burstSenders) {
  NS_LOG_FUNCTION(this);
  m_burstSenders = burstSenders;
}

void
IncastAggregator::SetBackgroundSenders(
    std::unordered_map<uint32_t, std::pair<Ptr<IncastSender>, Ipv4Address>>
        *backgroundSenders) {
  NS_LOG_FUNCTION(this);
  m_backgroundSenders = backgroundSenders;
}

Ptr<Socket>
IncastAggregator::SetupConnection(
    Ipv4Address sender, bool canSchedule, bool isBurstSender, bool useEcn) {
  NS_LOG_FUNCTION(
      this << " sender: " << sender << " canSchedule: " << canSchedule
           << " isBurstSender: " << isBurstSender);
  NS_LOG_LOGIC("Aggregator: Setup connection to " << sender);

  Ptr<Socket> socket = Socket::CreateSocket(GetNode(), m_tid);
  // Config::SetDefault(
  //     "ns3::TcpSocketBase::UseEcn", EnumValue(TcpSocketState::On));
  NS_ASSERT(socket->GetSocketType() == Socket::NS3_SOCK_STREAM);

  if (useEcn) {
    // Enable support for this connection.
    socket->SetAttribute("UseEcn", EnumValue(TcpSocketState::On));
  }

  if (socket->Bind() == -1) {
    NS_FATAL_ERROR("Aggregator bind failed");
  }

  socket->SetRecvCallback(MakeCallback(&IncastAggregator::HandleRead, this));

  // Connect to each sender
  NS_LOG_LOGIC("Aggregator: Connect to " << sender);
  int err = socket->Connect(InetSocketAddress(sender, m_port));

  if (err != 0) {
    NS_FATAL_ERROR(
        "Aggregator connect to " << sender
                                 << " failed: " << socket->GetErrno());
  }

  std::unordered_map<uint32_t, std::pair<Ptr<IncastSender>, Ipv4Address>>
      *toSearch;
  std::unordered_map<Ptr<Socket>, uint32_t> *toSet;
  if (isBurstSender) {
    // This is a burst sender.
    toSearch = m_burstSenders;
    toSet = &m_burstSockets;
  } else {
    // This is a background sender.
    toSearch = m_backgroundSenders;
    toSet = &m_backgroundSockets;
  }
  // Look up the node ID for this sender
  uint32_t nid = 0;
  for (const auto &[id, pair] : *toSearch) {
    if (pair.second == sender) {
      nid = id;
      break;
    }
  }
  // Map the socket to the sender node ID
  NS_ASSERT(nid != 0);
  (*toSet)[socket] = nid;

  // Set the congestion control algorithm
  Ptr<TcpSocketBase> tcpSocket = DynamicCast<TcpSocketBase>(socket);

  // Enable tracing for the CWND
  socket->TraceConnectWithoutContext(
      "CongestionWindow", MakeCallback(&IncastAggregator::LogCwnd, this));

  // Enable tracing for the RTT
  socket->TraceConnectWithoutContext(
      "RTT", MakeCallback(&IncastAggregator::LogRtt, this));

  // Enable tracing for ACK size.
  socket->TraceConnectWithoutContext(
      "BytesInAck", MakeCallback(&IncastAggregator::LogBytesInAck, this));

  // Enable TCP timestamp option
  tcpSocket->SetAttribute("Timestamp", BooleanValue(true));

  if (canSchedule) {
    // Kick off the simulation by scheduling the background flows and first
    // burst. Start the background flows 10 ms before the first burst.
    ScheduleBackground(MilliSeconds(10));
    ScheduleNextBurst(MilliSeconds(20));
  }

  return socket;
}

void
IncastAggregator::StartApplication() {
  NS_LOG_FUNCTION(this);

  NS_ASSERT(m_currentBurstCount != nullptr && *m_currentBurstCount == 0);
  NS_ASSERT(m_burstSenders != nullptr);
  NS_ASSERT(m_backgroundSenders != nullptr);

  NS_LOG_LOGIC(
      "Aggregator: num burst senders: " << m_burstSenders->size()
                                        << ", num background senders: "
                                        << m_backgroundSenders->size());

  m_backgroundSockets.clear();
  m_burstSockets.clear();

  // Setup all connections to the senders. Stagger connection setup by 1
  // millisecond to avoid issues. Start with the background senders.

  // Setup a connection with each background sender.
  uint32_t i = 0;
  for (const auto &[id, pair] : *m_backgroundSenders) {
    Simulator::Schedule(
        MilliSeconds(1 + i),
        &IncastAggregator::SetupConnection,
        this,
        pair.second,
        // The last burst sender (below) will take care of starting the
        // background flows.
        false,
        false,
        false);
    ++i;
  }

  // Setup a connection with each burst sender.
  for (const auto &[id, pair] : *m_burstSenders) {
    Simulator::Schedule(
        MilliSeconds(1 + i),
        &IncastAggregator::SetupConnection,
        this,
        pair.second,
        // If this is the last burst sender that we connect to, then afterwards
        // we can schedule the burst itself. Also start the background flows.
        i - m_backgroundSenders->size() == m_burstSenders->size() - 1,
        true,
        m_burstSenderCca.GetName() == "ns3::TcpDctcp");
    ++i;
  }

  // For scheduled RWND tuning, fill the available tokens.
  NS_LOG_LOGIC(
      "Aggregator: Fill available tokens: " << m_rwndScheduleMaxTokens);
  m_rwndScheduleAvailableTokens = m_rwndScheduleMaxTokens;
  // Make sure that no tokens are assigned.
  m_burstSendersWithAToken.clear();
}

void
IncastAggregator::CloseConnections() {
  NS_LOG_FUNCTION(this);

  for (const auto &[socket, nid] : m_burstSockets) {
    socket->Close();
  }

  for (const auto &[socket, nid] : m_backgroundSockets) {
    socket->Close();
  }

  Simulator::Schedule(
      MilliSeconds(10), &IncastAggregator::StopApplication, this);
}

void
IncastAggregator::ScheduleNextBurst(Time when) {
  NS_LOG_FUNCTION(this);

  ++(*m_currentBurstCount);

  if (*m_currentBurstCount > m_numBursts) {
    Simulator::Schedule(
        MilliSeconds(10), &IncastAggregator::CloseConnections, this);
    return;
  }

  m_totalBytesSoFar = 0;
  m_burstSendersFinished = 0;

  for (const auto &p : m_burstSockets) {
    m_bytesReceived[p.first] = 0;
  }

  // Create a new entry in the flow times map for the next burst
  std::unordered_map<uint32_t, std::vector<Time>> newFlowTimesEntry;
  m_flowTimes->push_back(newFlowTimesEntry);

  // Schedule the next burst for 100 ms later
  Simulator::Schedule(when, &IncastAggregator::StartBurst, this);
}

void
IncastAggregator::ScheduleBackground(Time when) {
  NS_LOG_FUNCTION(this);

  // Start the background flows after the burst senders get connected
  if (!m_startedBackground) {
    Simulator::Schedule(when, &IncastAggregator::StartBackground, this);
  }
}

void
IncastAggregator::StartRttProbes() {
  NS_LOG_FUNCTION(this);

  m_probingRtt = true;
  Simulator::Schedule(Seconds(0), &IncastAggregator::SendRttProbe, this);
}

void
IncastAggregator::StopRttProbes() {
  NS_LOG_FUNCTION(this);

  m_probingRtt = false;
}

void
IncastAggregator::SendRttProbe() {
  NS_LOG_FUNCTION(this);

  // Send a 1-byte packet to each sender
  for (const auto &p : m_burstSockets) {
    Ptr<Packet> packet = Create<Packet>(1);
    p.first->Send(packet);
  }

  if (m_probingRtt) {
    Simulator::Schedule(MilliSeconds(1), &IncastAggregator::SendRttProbe, this);
  }
}

void
IncastAggregator::StartBurst() {
  NS_LOG_FUNCTION(this);
  NS_LOG_INFO("Burst " << *m_currentBurstCount << " of " << m_numBursts);

  m_currentBurstStartTime = Simulator::Now();
  uint32_t firstSender = GetFirstSender();

  // Make sure that at the start of a burst, all tokens are available.
  NS_ASSERT(m_rwndScheduleAvailableTokens == m_rwndScheduleMaxTokens);
  NS_ASSERT(m_burstSendersWithAToken.empty());

  // If doing scheduled RWND tuning, then assign initial tokens.
  ScheduledRwndTuning(nullptr, false);

  // Copy a list of burst sockets
  std::vector<Ptr<Socket>> sockets;

  for (const auto &[socket, nid] : m_burstSockets) {
    sockets.push_back(socket);
  }

  NS_ASSERT(sockets.size() == m_burstSenders->size());

  // Send a request to each socket.
  for (const auto &socket : sockets) {
    // Whether this the sender gets offset.
    bool offsetThisSender = m_burstSockets[socket] == firstSender &&
                            m_firstFlowOffset.GetMilliSeconds() > 0;

    // Add jitter to each request
    Time jitter = Seconds(0);
    if (offsetThisSender) {
      // Optionally delay the first sender. Allow 1ms for connection setup.
      jitter = MilliSeconds(m_firstFlowOffset.GetMilliSeconds() - 1);
    } else if (m_requestJitterUs > 0) {
      jitter = MicroSeconds(rand() % m_requestJitterUs);
    }

    Simulator::Schedule(
        jitter,
        &IncastAggregator::SendRequest,
        this,
        socket,
        offsetThisSender,
        true);
  }
}

void
IncastAggregator::StartBackground() {
  NS_LOG_FUNCTION(this);

  // Copy a list of background sockets
  std::vector<Ptr<Socket>> sockets;

  for (const auto &[socket, nid] : m_backgroundSockets) {
    sockets.push_back(socket);
  }

  NS_ASSERT(sockets.size() == m_backgroundSenders->size());

  // Send a request to each socket.
  for (const auto &socket : sockets) {
    Simulator::Schedule(
        Seconds(0), &IncastAggregator::SendRequest, this, socket, false, false);
  }

  m_startedBackground = true;
}

void
IncastAggregator::SendRequest(
    Ptr<Socket> socket, bool createNewConn, bool isBurstRequest) {
  NS_LOG_FUNCTION(
      this << " socket: " << socket << " createNewConn: " << createNewConn);

  // The amount of data to request.
  uint32_t requestBytes;

  if (isBurstRequest) {
    // This is a request for a burst.
    requestBytes = m_bytesPerBurstSender;

    // Optionally create a new connection instead of reusuing the existing one.
    if (createNewConn) {
      // Close old socket.
      socket->Close();
      // Create a new socket and add it to m_burstSockets.
      socket = SetupConnection(
          (*m_burstSenders)[m_burstSockets[socket]].second,
          false,
          true,
          m_burstSenderCca.GetName() == "ns3::TcpDctcp");
      // Remove the old socket from m_burstSockets.
      m_burstSockets.erase(socket);
      // Add the new socket to m_bytesReceived.
      m_bytesReceived[socket] = 0;
      // Remove old socket from m_bytesReceived.
      m_bytesReceived.erase(socket);
      // Give the socket time to do the handshake. Then call this function
      // again to send the actual request.
      Simulator::Schedule(
          MilliSeconds(1),
          &IncastAggregator::SendRequest,
          this,
          socket,
          false,
          isBurstRequest);
      return;
    }

    // If we are doing scheduled RWND tuning and this sender has not been
    // assigned a token, then we need to set the RWND to 0
    if (m_rwndStrategy == "scheduled" &&
        m_burstSendersWithAToken.find(m_burstSockets[socket]) ==
            m_burstSendersWithAToken.end()) {
      SafelySetRwnd(DynamicCast<TcpSocketBase>(socket), 0, true);
    } else {
      StaticRwndTuning(DynamicCast<TcpSocketBase>(socket));
    }

    NS_LOG_LOGIC(
        "Sending request to sender "
        << (*m_burstSenders)[m_burstSockets[socket]].second);
  } else {
    // This is a request for background traffic.
    requestBytes = 12500000;  // TODO: use a less arbitrary size
    NS_LOG_INFO("Sending request to a background sender");
  }

  socket->Send(Create<Packet>((uint8_t *)&requestBytes, sizeof(uint32_t)));
}

void
IncastAggregator::HandleRead(Ptr<Socket> socket) {
  NS_LOG_FUNCTION(this << socket);

  Ptr<Packet> packet;

  // TODO: Write sender IP and port to config file

  while ((packet = socket->Recv())) {
    auto size = packet->GetSize();

    if (m_backgroundSockets.find(socket) != m_backgroundSockets.end()) {
      return;
    }

    m_totalBytesSoFar += size;
    m_bytesReceived[socket] += size;

    DynamicRwndTuning(DynamicCast<TcpSocketBase>(socket));
  }

  bool socketDone = m_bytesReceived[socket] >= m_bytesPerBurstSender;
  ScheduledRwndTuning(DynamicCast<TcpSocketBase>(socket), socketDone);

  if (socketDone) {
    ++m_burstSendersFinished;

    // Record when this flow finished.
    (*m_flowTimes)[*m_currentBurstCount - 1][m_burstSockets[socket]][2] =
        Simulator::Now();

    NS_LOG_INFO(
        "Aggregator: " << m_burstSendersFinished << "/"
                       << m_burstSenders->size() << " senders finished");

    // if (m_burstSendersFinished == m_burstSenders->size() - 1) {
    //   Ptr<Socket> remainingSocket = nullptr;
    //   for (const auto &p : m_bytesReceived) {
    //     if (p.second < m_bytesPerBurstSender) {
    //       remainingSocket = p.first;
    //       break;
    //     }
    //   }
    //   NS_ASSERT(remainingSocket != nullptr);
    //   NS_LOG_INFO(
    //       "Remaining socket: " << m_burstSockets[remainingSocket] << " only
    //       sent "
    //                            << m_bytesReceived[remainingSocket] << "/"
    //                            << m_bytesPerBurstSender << " bytes");
    // }

    if (m_bytesReceived[socket] > m_bytesPerBurstSender) {
      NS_LOG_ERROR(
          "Aggregator: Received too many bytes from sender "
          << (*m_burstSenders)[m_burstSockets[socket]].second);
    }
  }

  NS_LOG_LOGIC(
      "Aggregator: Received " << m_totalBytesSoFar << "/"
                              << GetTotalExpectedBytesPerBurst() << " bytes");

  if (m_totalBytesSoFar > GetTotalExpectedBytesPerBurst()) {
    NS_LOG_ERROR("Aggregator: Received too many bytes");
  }

  if (m_burstSendersFinished == m_burstSenders->size()) {
    NS_LOG_INFO("Burst done.");
    m_burstTimesLog.push_back({m_currentBurstStartTime, Simulator::Now()});

    // Make sure that at the end of a burst, all tokens are available.
    NS_ASSERT(m_rwndScheduleAvailableTokens == m_rwndScheduleMaxTokens);
    NS_ASSERT(m_burstSendersWithAToken.empty());

    // Allow the RTT probes to run for 10 ms extra.
    Simulator::Schedule(
        MilliSeconds(10), &IncastAggregator::StopRttProbes, this);
    // // Start the RTT probes 10 ms before the next burst, at 20 ms.
    // Simulator::Schedule(
    //     MilliSeconds(20), &IncastAggregator::StartRttProbes, this);
    // Start the burst itself in 30 ms.
    ScheduleNextBurst(MilliSeconds(30));
  }
}

void
IncastAggregator::HandleAccept(Ptr<Socket> socket, const Address &from) {
  NS_LOG_FUNCTION(this << " socket: " << socket << " from: " << from);

  socket->SetRecvCallback(MakeCallback(&IncastAggregator::HandleRead, this));
}

std::vector<std::pair<Time, Time>>
IncastAggregator::GetBurstTimes() {
  NS_LOG_FUNCTION(this);

  return m_burstTimesLog;
}

void
IncastAggregator::StopApplication() {
  NS_LOG_FUNCTION(this);

  Simulator::Stop(MilliSeconds(10));
}

void
IncastAggregator::WriteLogs() {
  NS_LOG_FUNCTION(this);

  for (const auto &p : m_bytesReceived) {
    if (p.second < m_bytesPerBurstSender) {
      uint32_t nid = m_burstSockets[p.first];
      NS_LOG_ERROR(
          "Sender " << nid << " (" << (*m_burstSenders)[nid].second
                    << ") only sent " << p.second << "/"
                    << m_bytesPerBurstSender << " bytes");
    }
  }

  std::ofstream burstTimesOut;
  burstTimesOut.open(
      m_outputDirectory + "/" + m_traceDirectory + "/logs/burst_times.log",
      std::ios::out);
  burstTimesOut << "# Start time (s) End time (s)" << std::endl;

  for (const auto &p : m_burstTimesLog) {
    burstTimesOut << std::fixed << std::setprecision(12) << p.first.GetSeconds()
                  << " " << p.second.GetSeconds() << std::endl;
  }

  burstTimesOut.close();

  std::ofstream cwndOut;
  cwndOut.open(
      m_outputDirectory + "/" + m_traceDirectory + "/logs/aggregator_cwnd.log",
      std::ios::out);
  cwndOut << "# Time (s) CWND (bytes)" << std::endl;

  for (const auto &p : m_cwndLog) {
    cwndOut << std::fixed << std::setprecision(12) << p.first.GetSeconds()
            << " " << p.second << std::endl;
  }

  cwndOut.close();

  std::ofstream rttOut;
  rttOut.open(
      m_outputDirectory + "/" + m_traceDirectory + "/logs/aggregator_rtt.log",
      std::ios::out);
  rttOut << "# Time (s) RTT (us)" << std::endl;

  for (const auto &p : m_rttLog) {
    rttOut << std::fixed << std::setprecision(12) << p.first.GetSeconds() << " "
           << p.second.GetMicroSeconds() << std::endl;
  }

  rttOut.close();

  std::ofstream bytesInAckOut;
  bytesInAckOut.open(
      m_outputDirectory + "/" + m_traceDirectory +
          "/logs/aggregator_bytes_in_ack.log",
      std::ios::out);
  bytesInAckOut << "# Time (s) , sender IP , sender port , aggregator IP , "
                   "aggregator port , bytes in ack"
                << std::endl;

  for (const auto &entry : m_bytesInAckLog) {
    bytesInAckOut << std::fixed << std::setprecision(12)
                  << entry.time.GetSeconds() << " " << entry.sender_ip << " "
                  << entry.sender_port << " " << entry.aggregator_ip << " "
                  << entry.aggregator_port << " " << entry.bytesInAck
                  << std::endl;
  }

  bytesInAckOut.close();
}

void
IncastAggregator::SafelySetRwnd(
    Ptr<TcpSocketBase> tcpSocket, uint32_t rwndBytes, bool allowBelowMinRwnd) {
  NS_LOG_FUNCTION(
      this << " tcpSocket: " << tcpSocket << " rwndBytes: " << rwndBytes
           << " allowBelowMinRwnd: " << allowBelowMinRwnd);

  if (!allowBelowMinRwnd && rwndBytes < MIN_RWND) {
    NS_LOG_WARN(
        "Aggregator: RWND tuning is only supported for values >= 2KB, but "
        "chosen RWND "
        "is: "
        << rwndBytes << " bytes");
    rwndBytes = std::max(rwndBytes, (uint32_t)MIN_RWND);
  }

  uint32_t rwndScaled = rwndBytes >> tcpSocket->GetRcvWindShift();

  if (rwndScaled > MAX_RWND) {
    NS_LOG_WARN(
        "Aggregator: RWND tuning is only supported for values <= 65KB, "
        "but chosen RWND is: "
        << rwndScaled << " bytes");
    rwndScaled = std::min(rwndScaled, (uint32_t)MAX_RWND);
  }

  tcpSocket->SetOverrideWindowSize(rwndScaled);
  NS_LOG_LOGIC(
      "Aggregator: Set RWND for sender "
      << (*m_burstSenders)[m_burstSockets[tcpSocket]].second << " ("
      << m_burstSockets[tcpSocket] << ") "
      << " to " << rwndBytes << " bytes (scaled=" << rwndScaled << ")");
}

void
IncastAggregator::StaticRwndTuning(Ptr<TcpSocketBase> tcpSocket) {
  NS_LOG_FUNCTION(this << tcpSocket);

  if (m_rwndStrategy != "static") {
    return;
  }

  SafelySetRwnd(tcpSocket, m_staticRwndBytes, false);
}

void
IncastAggregator::DynamicRwndTuning(Ptr<TcpSocketBase> tcpSocket) {
  NS_LOG_FUNCTION(this << tcpSocket);

  if (m_rwndStrategy != "bdp+connections") {
    return;
  }

  // Set RWND based on the number of the BDP and number of connections.
  Time rtt = tcpSocket->GetRttEstimator()->GetEstimate();

  if (rtt < m_physicalRtt) {
    NS_LOG_LOGIC("Aggregator: Invalid RTT sample.");
  } else {
    if (m_minRtt == Seconds(0)) {
      m_minRtt = rtt;
    } else {
      m_minRtt = Min(m_minRtt, rtt);
    }
  }

  if (m_minRtt < m_physicalRtt) {
    NS_LOG_LOGIC(
        "Aggregator: Invalid or no minRtt measurement. Skipping RWND "
        "tuning.");
    return;
  }

  // auto rtt = tcpSocket->GetTcpSocketState()->m_minRtt;
  auto bdpBytes = rtt.GetSeconds() * m_bandwidthMbps * pow(10, 6) / 8;
  auto numConns = m_burstSockets.size();
  uint16_t rwndBytes = (uint16_t)std::floor(bdpBytes / numConns);

  NS_LOG_LOGIC(
      "Aggregator: minRtt: "
      << m_minRtt.As(Time::US) << ", srtt: " << rtt.As(Time::US)
      << ", Bandwidth: " << m_bandwidthMbps
      << " Mbps, Connections: " << numConns << ", BDP: " << bdpBytes
      << " bytes, RWND: " << rwndBytes << " bytes");

  SafelySetRwnd(tcpSocket, rwndBytes, false);
}

void
IncastAggregator::ScheduledRwndTuning(
    Ptr<TcpSocketBase> tcpSocket, bool socketDone) {
  NS_LOG_FUNCTION(
      this << " tcpSocket: " << tcpSocket << " socketDone: " << socketDone);

  if (m_rwndStrategy != "scheduled") {
    return;
  }

  // If the socket is done, then reclaim its token.
  if (tcpSocket != nullptr && socketDone) {
    // Make sure that this sender actually had a token.
    NS_ASSERT_MSG(
        m_burstSendersWithAToken.find(m_burstSockets[tcpSocket]) !=
            m_burstSendersWithAToken.end(),
        "Sender " << (*m_burstSenders)[m_burstSockets[tcpSocket]].second << " ("
                  << m_burstSockets[tcpSocket]
                  << ") is attempting to release a token it does not own.");

    // Increment the available tokens.
    m_rwndScheduleAvailableTokens++;
    m_burstSendersWithAToken.erase(m_burstSockets[tcpSocket]);

    // Set the connection's RWND to 0 to prevent it from sending at the
    // beginning of the next burst.
    SafelySetRwnd(tcpSocket, 0, true);

    NS_LOG_LOGIC(
        "Aggregator: Socket "
        << tcpSocket << " is done. Reclaimed "
        << "its token. Available tokens: " << m_rwndScheduleAvailableTokens);
  }

  // Make sure that the number of tokens is valid.
  NS_ASSERT(m_rwndScheduleAvailableTokens <= m_rwndScheduleMaxTokens);
  NS_LOG_LOGIC(
      "Aggregator: Available tokens: "
      << m_rwndScheduleAvailableTokens
      << " Max tokens: " << m_rwndScheduleMaxTokens
      << " Senders with a token: " << m_burstSendersWithAToken.size());
  NS_ASSERT(
      m_burstSendersWithAToken.size() ==
      m_rwndScheduleMaxTokens - m_rwndScheduleAvailableTokens);

  // Look through the list of sockets and try to assign a token to one.
  for (auto &p : m_burstSockets) {
    // If no tokens are available, then abort.
    if (m_rwndScheduleAvailableTokens == 0) {
      break;
    }

    // If this socket is done already, then skip it.
    if (m_bytesReceived[p.first] >= m_bytesPerBurstSender) {
      continue;
    }

    // If this socket already has a token, then skip it.
    if (m_burstSendersWithAToken.find(m_burstSockets[p.first]) !=
        m_burstSendersWithAToken.end()) {
      continue;
    }

    // Assign this sender a token.
    // Decrement the available tokens.
    m_rwndScheduleAvailableTokens--;
    m_burstSendersWithAToken.insert(m_burstSockets[p.first]);
    // Set the socket's RWND to the configured value.
    SafelySetRwnd(
        DynamicCast<TcpSocketBase>(p.first), m_staticRwndBytes, false);

    NS_LOG_LOGIC(
        "Aggregator: Assigned token to sender "
        << (*m_burstSenders)[m_burstSockets[p.first]].second << " ("
        << m_burstSockets[p.first]
        << "). Available "
           "tokens: "
        << m_rwndScheduleAvailableTokens);
  }

  NS_ASSERT(
      m_burstSendersWithAToken.size() ==
      m_rwndScheduleMaxTokens - m_rwndScheduleAvailableTokens);
}

void
IncastAggregator::SetCurrentBurstCount(uint32_t *currentBurstCount) {
  NS_LOG_FUNCTION(this << currentBurstCount);

  m_currentBurstCount = currentBurstCount;
}

void
IncastAggregator::SetFlowTimesRecord(
    std::vector<std::unordered_map<uint32_t, std::vector<Time>>> *flowTimes) {
  NS_LOG_FUNCTION(this << flowTimes);

  m_flowTimes = flowTimes;
}

uint32_t
IncastAggregator::GetFirstSender() {
  NS_LOG_FUNCTION(this);
  NS_ASSERT(m_burstSenders != nullptr);
  NS_ASSERT(m_burstSenders->size() > 0);

  // Create a list of node IDs.
  std::vector<uint32_t> nids;

  for (const auto &p : *m_burstSenders) {
    nids.push_back(p.first);
  }

  // Find the min node ID.
  std::vector<uint32_t>::iterator result =
      std::min_element(nids.begin(), nids.end());

  return (*result);
}

uint32_t
IncastAggregator::GetTotalExpectedBytesPerBurst() {
  NS_LOG_FUNCTION(this);
  return m_bytesPerBurstSender * m_burstSenders->size();
}

}  // Namespace ns3