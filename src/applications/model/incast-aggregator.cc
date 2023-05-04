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

/**
 * Callback to log congestion window changes
 *
 * \param oldCwndBytes old congestion window
 * \param newCwndBytes new congestion window
 */
void
IncastAggregator::LogCwnd(uint32_t oldCwndBytes, uint32_t newCwndBytes) {
  NS_LOG_FUNCTION(this << " old: " << oldCwndBytes << " new: " << newCwndBytes);

  m_cwndLog.push_back({Simulator::Now(), newCwndBytes});
}

/**
 * Callback to log round-trip time changes
 *
 * \param oldRtt old round-trip time
 * \param newRtt new round-trip time
 */
void
IncastAggregator::LogRtt(Time oldRtt, Time newRtt) {
  NS_LOG_FUNCTION(this << " old: " << oldRtt << " new: " << newRtt);

  m_rttLog.push_back({Simulator::Now(), newRtt});
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
              "BytesPerSender",
              "Number of bytes to request from each sender for each burst",
              UintegerValue(MSS),
              MakeUintegerAccessor(&IncastAggregator::m_bytesPerSender),
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
              "TypeId of the CCA",
              TypeIdValue(TcpSocketFactory::GetTypeId()),
              MakeTypeIdAccessor(&IncastAggregator::m_cca),
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
              MakeTimeChecker())
          .AddAttribute(
              "DctcpShiftG",
              "Parameter G for updating dctcp_alpha",
              DoubleValue(0.0625),
              MakeDoubleAccessor(&IncastAggregator::m_dctcpShiftG),
              MakeDoubleChecker<double>(0, 1));

  // TODO: Need to configure scheduled RWND tuning parameters.

  return tid;
}

IncastAggregator::IncastAggregator()
    : m_numBursts(10),
      m_bytesPerSender(MSS),
      m_totalBytesSoFar(0),
      m_port(8888),
      m_requestJitterUs(0),
      m_rwndStrategy("none"),
      m_staticRwndBytes(MAX_RWND),
      m_bandwidthMbps(0),
      m_physicalRtt(Seconds(0)),
      m_minRtt(Seconds(0)),
      m_probingRtt(false),
      m_firstFlowOffset(MilliSeconds(0)),
      m_dctcpShiftG(0.0625) {
  NS_LOG_FUNCTION(this);
}

IncastAggregator::~IncastAggregator() { NS_LOG_FUNCTION(this); }

void
IncastAggregator::DoDispose() {
  NS_LOG_FUNCTION(this);

  m_sockets.clear();
  Application::DoDispose();
}

void
IncastAggregator::SetBurstSenders(
    std::unordered_map<uint32_t, std::pair<Ptr<IncastSender>, Ipv4Address>>
        *burstSenders) {
  NS_LOG_FUNCTION(this);
  m_burstSenders = burstSenders;
}

Ptr<Socket>
IncastAggregator::SetupConnection(Ipv4Address sender, bool scheduleNextBurst) {
  NS_LOG_FUNCTION(
      this << " sender: " << sender
           << " scheduleNextBurst: " << scheduleNextBurst);
  NS_LOG_LOGIC("Aggregator: Setup connection to " << sender);

  Ptr<Socket> socket = Socket::CreateSocket(GetNode(), m_tid);

  NS_ASSERT(socket->GetSocketType() == Socket::NS3_SOCK_STREAM);

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

  // Look up the node ID for this sender
  uint32_t nid = 0;

  for (const auto &p : *m_burstSenders) {
    if (p.second.second == sender) {
      nid = p.first;
      break;
    }
  }

  // Map the socket to the sender node ID
  NS_ASSERT(nid != 0);
  m_sockets[socket] = nid;

  // Set the congestion control algorithm
  Ptr<TcpSocketBase> tcpSocket = DynamicCast<TcpSocketBase>(socket);
  ObjectFactory ccaFactory;
  ccaFactory.SetTypeId(m_cca);
  Ptr<TcpCongestionOps> ccaPtr = ccaFactory.Create<TcpCongestionOps>();
  tcpSocket->SetCongestionControlAlgorithm(ccaPtr);

  // Set DctcpShiftG.
  if (m_cca.GetName() == "ns3::TcpDctcp") {
    PointerValue congOpsValue;
    tcpSocket->GetAttribute("CongestionOps", congOpsValue);
    Ptr<TcpCongestionOps> congsOps = congOpsValue.Get<TcpCongestionOps>();
    Ptr<TcpDctcp> dctcp = DynamicCast<TcpDctcp>(congsOps);
    dctcp->SetAttribute("DctcpShiftG", DoubleValue(m_dctcpShiftG));
  }

  // Enable tracing for the CWND
  socket->TraceConnectWithoutContext(
      "CongestionWindow", MakeCallback(&IncastAggregator::LogCwnd, this));

  // Enable tracing for the RTT
  socket->TraceConnectWithoutContext(
      "RTT", MakeCallback(&IncastAggregator::LogRtt, this));

  // Enable TCP timestamp option
  tcpSocket->SetAttribute("Timestamp", BooleanValue(true));

  if (scheduleNextBurst) {
    // Schedule the first burst
    ScheduleNextBurst();
  }

  return socket;
}

void
IncastAggregator::StartApplication() {
  NS_LOG_FUNCTION(this);

  NS_ASSERT(m_currentBurstCount != nullptr && *m_currentBurstCount == 0);
  NS_ASSERT(m_burstSenders != nullptr);
  m_sockets.clear();

  NS_LOG_LOGIC("Aggregator: num senders: " << m_burstSenders->size());

  // Setup a connection with each sender.
  uint32_t i = 0;

  for (const auto &p : *m_burstSenders) {
    Simulator::Schedule(
        MilliSeconds(1 + i),
        &IncastAggregator::SetupConnection,
        this,
        p.second.second,
        i == m_burstSenders->size() - 1);
    ++i;
  }

  // Fill the available tokens.
  NS_LOG_LOGIC(
      "Aggregator: Fill available tokens: " << m_rwndScheduleMaxTokens);
  m_rwndScheduleAvailableTokens = m_rwndScheduleMaxTokens;
  // Make sure that no tokens are assigned.
  m_burstSendersWithAToken.clear();
}

void
IncastAggregator::CloseConnections() {
  NS_LOG_FUNCTION(this);

  for (const auto &p : m_sockets) {
    p.first->Close();
  }

  Simulator::Schedule(
      MilliSeconds(10), &IncastAggregator::StopApplication, this);
}

void
IncastAggregator::ScheduleNextBurst() {
  NS_LOG_FUNCTION(this);

  if (*m_currentBurstCount == m_numBursts) {
    Simulator::Schedule(
        MilliSeconds(10), &IncastAggregator::CloseConnections, this);
    return;
  }

  ++(*m_currentBurstCount);
  m_totalBytesSoFar = 0;
  m_burstSendersFinished = 0;

  for (const auto &p : m_sockets) {
    m_bytesReceived[p.first] = 0;
  }

  // Create a new entry in the flow times map for the next burst
  std::unordered_map<uint32_t, std::vector<Time>> newFlowTimesEntry;
  m_flowTimes->push_back(newFlowTimesEntry);

  // Schedule the next burst for 1 second later
  Simulator::Schedule(Seconds(1), &IncastAggregator::StartBurst, this);

  // Start the RTT probes 10ms before the next burst
  // Simulator::Schedule(
  //     MilliSeconds(990), &IncastAggregator::StartRttProbes, this);
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
  for (const auto &p : m_sockets) {
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

  // Get a list of sockets so that we do not modify m_sockets while iterating
  // over it.
  std::vector<Ptr<Socket>> sockets;
  for (const auto &p : m_sockets) {
    sockets.push_back(p.first);
  }
  NS_ASSERT(sockets.size() == m_burstSenders->size());

  // Send a request to each socket.
  for (const auto &socket : sockets) {
    // Whether this the sender gets offset.
    bool offsetThisSender = m_sockets[socket] == firstSender &&
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
        jitter, &IncastAggregator::SendRequest, this, socket, offsetThisSender);
  }
}

void
IncastAggregator::SendRequest(Ptr<Socket> socket, bool createNewConn) {
  NS_LOG_FUNCTION(
      this << " socket: " << socket << " createNewConn: " << createNewConn);

  if (createNewConn) {
    // Remove old socket from m_bytesReceived.
    m_bytesReceived.erase(socket);
    // Close old socket.
    socket->Close();
    Ptr<Socket> old_socket = socket;
    // Create a new socket and add it to m_sockets.
    socket = SetupConnection((*m_burstSenders)[m_sockets[socket]].second, false);
    // Remove the old socket from m_sockets.
    m_sockets.erase(old_socket);
    // Add the new socket to m_bytesReceived.
    m_bytesReceived[socket] = 0;
    // Give the socket time to do the handshake. Then call this function again.
    Simulator::Schedule(
        MilliSeconds(1), &IncastAggregator::SendRequest, this, socket, false);
    return;
  }

  // If we are doing scheduled RWND tuning and this sender has not been assigned
  // a token, then we need to set the RWND to 0
  if (m_rwndStrategy == "scheduled" &&
      m_burstSendersWithAToken.find(m_sockets[socket]) ==
          m_burstSendersWithAToken.end()) {
    SafelySetRwnd(DynamicCast<TcpSocketBase>(socket), 0, true);
  } else {
    StaticRwndTuning(DynamicCast<TcpSocketBase>(socket));
  }

  NS_LOG_LOGIC(
      "Sending request to sender " << (*m_burstSenders)[m_sockets[socket]].second);
  Ptr<Packet> packet =
      Create<Packet>((uint8_t *)&m_bytesPerSender, sizeof(uint32_t));
  socket->Send(packet);
}

void
IncastAggregator::HandleRead(Ptr<Socket> socket) {
  NS_LOG_FUNCTION(this << socket);

  Ptr<Packet> packet;

  // TODO: Write sender IP and port to config file

  while ((packet = socket->Recv())) {
    auto size = packet->GetSize();
    m_totalBytesSoFar += size;
    m_bytesReceived[socket] += size;

    DynamicRwndTuning(DynamicCast<TcpSocketBase>(socket));
  }

  bool socketDone = m_bytesReceived[socket] >= m_bytesPerSender;
  ScheduledRwndTuning(DynamicCast<TcpSocketBase>(socket), socketDone);

  if (socketDone) {
    ++m_burstSendersFinished;

    // Record when this flow finished.
    (*m_flowTimes)[*m_currentBurstCount - 1][m_sockets[socket]][2] =
        Simulator::Now();

    NS_LOG_INFO(
        "Aggregator: " << m_burstSendersFinished << "/" << m_burstSenders->size()
                       << " senders finished");

    // if (m_burstSendersFinished == m_burstSenders->size() - 1) {
    //   Ptr<Socket> remainingSocket = nullptr;
    //   for (const auto &p : m_bytesReceived) {
    //     if (p.second < m_bytesPerSender) {
    //       remainingSocket = p.first;
    //       break;
    //     }
    //   }
    //   NS_ASSERT(remainingSocket != nullptr);
    //   NS_LOG_INFO(
    //       "Remaining socket: " << m_sockets[remainingSocket] << " only sent "
    //                            << m_bytesReceived[remainingSocket] << "/"
    //                            << m_bytesPerSender << " bytes");
    // }

    if (m_bytesReceived[socket] > m_bytesPerSender) {
      NS_LOG_ERROR(
          "Aggregator: Received too many bytes from sender "
          << (*m_burstSenders)[m_sockets[socket]].second);
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

    ScheduleNextBurst();
    Simulator::Schedule(
        MilliSeconds(10), &IncastAggregator::StopRttProbes, this);
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
    if (p.second < m_bytesPerSender) {
      uint32_t nid = m_sockets[p.first];
      NS_LOG_ERROR(
          "Sender " << nid << " (" << (*m_burstSenders)[nid].second << ") only sent "
                    << p.second << "/" << m_bytesPerSender << " bytes");
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
      << (*m_burstSenders)[m_sockets[tcpSocket]].second << " ("
      << m_sockets[tcpSocket] << ") "
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
  auto numConns = m_sockets.size();
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
        m_burstSendersWithAToken.find(m_sockets[tcpSocket]) !=
            m_burstSendersWithAToken.end(),
        "Sender " << (*m_burstSenders)[m_sockets[tcpSocket]].second << " ("
                  << m_sockets[tcpSocket]
                  << ") is attempting to release a token it does not own.");

    // Increment the available tokens.
    m_rwndScheduleAvailableTokens++;
    m_burstSendersWithAToken.erase(m_sockets[tcpSocket]);

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
  for (auto &p : m_sockets) {
    // If no tokens are available, then abort.
    if (m_rwndScheduleAvailableTokens == 0) {
      break;
    }

    // If this socket is done already, then skip it.
    if (m_bytesReceived[p.first] >= m_bytesPerSender) {
      continue;
    }

    // If this socket already has a token, then skip it.
    if (m_burstSendersWithAToken.find(m_sockets[p.first]) !=
        m_burstSendersWithAToken.end()) {
      continue;
    }

    // Assign this sender a token.
    // Decrement the available tokens.
    m_rwndScheduleAvailableTokens--;
    m_burstSendersWithAToken.insert(m_sockets[p.first]);
    // Set the socket's RWND to the configured value.
    SafelySetRwnd(
        DynamicCast<TcpSocketBase>(p.first), m_staticRwndBytes, false);

    NS_LOG_LOGIC(
        "Aggregator: Assigned token to sender "
        << (*m_burstSenders)[m_sockets[p.first]].second << " (" << m_sockets[p.first]
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
  return m_bytesPerSender * m_burstSenders->size();
}

}  // Namespace ns3