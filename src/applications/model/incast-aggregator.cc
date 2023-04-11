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

#include <iomanip>
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
              UintegerValue(1448),
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
              "RWND tuning strategy to use [none, static, bdp+connections]",
              StringValue("none"),
              MakeStringAccessor(&IncastAggregator::m_rwndStrategy),
              MakeStringChecker())
          .AddAttribute(
              "StaticRwndBytes",
              "If RwndStrategy=static, then use this static RWND value",
              UintegerValue(65535),
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

  return tid;
}

IncastAggregator::IncastAggregator()
    : m_numBursts(10),
      m_bytesPerSender(1448),
      m_totalBytesSoFar(0),
      m_port(8888),
      m_requestJitterUs(0),
      m_rwndStrategy("none"),
      m_staticRwndBytes(65535),
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

  m_sockets.clear();
  Application::DoDispose();
}

void
IncastAggregator::SetSenders(
    std::unordered_map<uint32_t, std::pair<Ptr<IncastSender>, Ipv4Address>>
        *senders) {
  NS_LOG_FUNCTION(this);
  m_senders = senders;
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

  // Store a mapping from socket to sender node ID
  // Look up the node ID for this sender
  uint32_t nid = 0;
  for (const auto &p : *m_senders) {
    if (p.second.second == sender) {
      nid = p.first;
      break;
    }
  }
  NS_ASSERT(nid != 0);
  m_sockets[socket] = nid;

  // Set the congestion control algorithm
  Ptr<TcpSocketBase> tcpSocket = DynamicCast<TcpSocketBase>(socket);
  ObjectFactory ccaFactory;
  ccaFactory.SetTypeId(m_cca);
  Ptr<TcpCongestionOps> ccaPtr = ccaFactory.Create<TcpCongestionOps>();
  tcpSocket->SetCongestionControlAlgorithm(ccaPtr);

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

  // Make sure that the burst index pointer is defined and set to 0.
  NS_ASSERT(m_currentBurstCount != nullptr && *m_currentBurstCount == 0);
  NS_ASSERT(m_senders != nullptr);

  NS_LOG_LOGIC("Aggregator: num senders: " << m_senders->size());

  // Setup a connection with each sender.
  uint32_t i = 0;
  for (const auto &p : *m_senders) {
    Simulator::Schedule(
        MilliSeconds(1 + i),
        &IncastAggregator::SetupConnection,
        this,
        p.second.second,
        i == m_senders->size() - 1);
    ++i;
  }
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
  m_sendersFinished = 0;
  for (const auto &p : m_sockets) {
    m_bytesReceived[p.first] = 0;
  }
  // Create a new entry in the flow times map for the next burst
  std::unordered_map<uint32_t, std::pair<Time, Time>> newFlowTimesEntry;
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

  // Get a list of sockets so that we do not modify m_sockets while iterating
  // over it.
  std::vector<Ptr<Socket>> sockets;
  for (const auto &p : m_sockets) {
    sockets.push_back(p.first);
  }
  NS_ASSERT(sockets.size() == m_senders->size());

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
  NS_LOG_FUNCTION(this << socket);

  if (createNewConn) {
    // Remove old socket from m_bytesReceived.
    m_bytesReceived.erase(socket);
    // Close old socket.
    socket->Close();
    Ptr<Socket> old_socket = socket;
    // Create a new socket and add it to m_sockets.
    socket = SetupConnection((*m_senders)[m_sockets[socket]].second, false);
    // Remove the old socket from m_sockets.
    m_sockets.erase(old_socket);
    // Add the new socket to m_bytesReceived.
    m_bytesReceived[socket] = 0;
    // Give the socket time to do the handshake. Then call this function again.
    Simulator::Schedule(
        MilliSeconds(1), &IncastAggregator::SendRequest, this, socket, false);
    return;
  }

  StaticRwndTuning(DynamicCast<TcpSocketBase>(socket));

  NS_LOG_LOGIC(
      "Sending request to sender " << (*m_senders)[m_sockets[socket]].second);
  Ptr<Packet> packet =
      Create<Packet>((uint8_t *)&m_bytesPerSender, sizeof(uint32_t));
  socket->Send(packet);
}

void
IncastAggregator::HandleRead(Ptr<Socket> socket) {
  NS_LOG_FUNCTION(this << socket);

  Ptr<Packet> packet;

  // TODO: Write sender IP and port to config file

  // Maybe TODO: Plot CDF of CWND

  while ((packet = socket->Recv())) {
    auto size = packet->GetSize();
    m_totalBytesSoFar += size;
    m_bytesReceived[socket] += size;

    DynamicRwndTuning(DynamicCast<TcpSocketBase>(socket));
  }

  if (m_bytesReceived[socket] >= m_bytesPerSender) {
    ++m_sendersFinished;

    // Record when this flow finished.
    (*m_flowTimes)[*m_currentBurstCount - 1][m_sockets[socket]].second =
        Simulator::Now();

    NS_LOG_INFO(
        "Aggregator: " << m_sendersFinished << "/" << m_senders->size()
                       << " senders finished");

    // if (m_sendersFinished == m_senders->size() - 1) {
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
          << (*m_senders)[m_sockets[socket]].second);
    }
  }

  NS_LOG_LOGIC(
      "Aggregator: Received " << m_totalBytesSoFar << "/"
                              << m_bytesPerSender * m_senders->size()
                              << " bytes");
  if (m_totalBytesSoFar > m_bytesPerSender * m_senders->size()) {
    NS_LOG_ERROR("Aggregator: Received too many bytes");
  }

  if (m_sendersFinished == m_senders->size()) {
    NS_LOG_INFO("Burst done.");
    m_burstTimesLog.push_back({m_currentBurstStartTime, Simulator::Now()});

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
          "Sender " << nid << " (" << (*m_senders)[nid].second << ") only sent "
                    << p.second << "/" << m_bytesPerSender << " bytes");
    }
  }

  std::ofstream burstTimesOut;
  burstTimesOut.open(
      m_outputDirectory + "/" + m_traceDirectory + "/log/burst_times.log",
      std::ios::out);
  burstTimesOut << "# Start time (s) End time (s)" << std::endl;
  for (const auto &p : m_burstTimesLog) {
    burstTimesOut << std::fixed << std::setprecision(12) << p.first.GetSeconds()
                  << " " << p.second.GetSeconds() << std::endl;
  }
  burstTimesOut.close();

  std::ofstream cwndOut;
  cwndOut.open(
      m_outputDirectory + "/" + m_traceDirectory + "/log/aggregator_cwnd.log",
      std::ios::out);
  cwndOut << "# Time (s) CWND (bytes)" << std::endl;
  for (const auto &p : m_cwndLog) {
    cwndOut << std::fixed << std::setprecision(12) << p.first.GetSeconds()
            << " " << p.second << std::endl;
  }
  cwndOut.close();

  std::ofstream rttOut;
  rttOut.open(
      m_outputDirectory + "/" + m_traceDirectory + "/log/aggregator_rtt.log",
      std::ios::out);
  rttOut << "# Time (s) RTT (us)" << std::endl;
  for (const auto &p : m_rttLog) {
    rttOut << std::fixed << std::setprecision(12) << p.first.GetSeconds() << " "
           << p.second.GetMicroSeconds() << std::endl;
  }
  rttOut.close();
}

void
IncastAggregator::StaticRwndTuning(Ptr<TcpSocketBase> tcpSocket) {
  NS_LOG_FUNCTION(this << tcpSocket);

  if (m_rwndStrategy != "static") {
    return;
  }

  // Set the RWND to 64KB for all sockets
  if (m_staticRwndBytes < 2000 || m_staticRwndBytes > 65535) {
    NS_FATAL_ERROR(
        "RWND tuning is only supported for values in the range [2000, "
        "65535]");
  }

  uint32_t rwndToSet = m_staticRwndBytes >> tcpSocket->GetRcvWindShift();
  NS_LOG_LOGIC(
      "StaticRwndTuning for socket: "
      << tcpSocket << " - Base RWND: " << m_staticRwndBytes << " bytes, Shift: "
      << tcpSocket->GetRcvWindShift() << " to set: " << rwndToSet);

  tcpSocket->SetOverrideWindowSize(rwndToSet);
}

void
IncastAggregator::DynamicRwndTuning(Ptr<TcpSocketBase> tcpSocket) {
  NS_LOG_FUNCTION(this << tcpSocket);

  if (m_rwndStrategy == "bdp+connections") {
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
    if (rwndBytes < 2000) {
      NS_LOG_WARN(
          "Aggregator: RWND tuning is only supported for values >= 2KB, but "
          "chosen RWND "
          "is: "
          << rwndBytes << " bytes");
      rwndBytes = std::max(rwndBytes, (uint16_t)2000u);
    }
    if (rwndBytes > 65535) {
      NS_LOG_WARN(
          "Aggregator: RWND tuning is only supported for values <= 64KB, "
          "but chosen RWND is: "
          << rwndBytes << " bytes");
      rwndBytes = std::min(rwndBytes, (uint16_t)65535u);
    }
    NS_LOG_LOGIC(rwndBytes);
    tcpSocket->SetOverrideWindowSize(rwndBytes >> tcpSocket->GetRcvWindShift());
  }
}

void
IncastAggregator::SetCurrentBurstCount(uint32_t *currentBurstCount) {
  NS_LOG_FUNCTION(this << currentBurstCount);

  m_currentBurstCount = currentBurstCount;
}

void
IncastAggregator::SetFlowTimesRecord(
    std::vector<std::unordered_map<uint32_t, std::pair<Time, Time>>>
        *flowTimes) {
  NS_LOG_FUNCTION(this << flowTimes);

  m_flowTimes = flowTimes;
}

uint32_t
IncastAggregator::GetFirstSender() {
  NS_LOG_FUNCTION(this);
  NS_ASSERT(m_senders != nullptr);
  NS_ASSERT(m_senders->size() > 0);
  // Create a list of node IDs.
  std::vector<uint32_t> nids;
  for (const auto &p : *m_senders) {
    nids.push_back(p.first);
  }
  // Find the min node ID.
  std::vector<uint32_t>::iterator result =
      std::min_element(nids.begin(), nids.end());
  return (*result);
}

}  // Namespace ns3