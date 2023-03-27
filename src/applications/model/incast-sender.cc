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

#include "incast-sender.h"

#include "ns3/boolean.h"
#include "ns3/internet-module.h"
#include "ns3/log.h"
#include "ns3/pointer.h"
#include "ns3/string.h"
#include "ns3/tcp-congestion-ops.h"
#include "ns3/uinteger.h"

#include <fstream>
#include <iomanip>
#include <iostream>

NS_LOG_COMPONENT_DEFINE("IncastSender");

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED(IncastSender);

/**
 * Callback to log congestion window changes
 *
 * \param oldCwndBytes old congestion window
 * \param newCwndBytes new congestion window
 */
void
IncastSender::LogCwnd(uint32_t oldCwndBytes, uint32_t newCwndBytes) {
  m_cwndLog.push_back({Simulator::Now(), newCwndBytes});
}

/**
 * Callback to log round-trip time changes
 *
 * \param oldRtt old round-trip time
 * \param newRtt new round-trip time
 */
void
IncastSender::LogRtt(Time oldRtt, Time newRtt) {
  m_rttLog.push_back({Simulator::Now(), newRtt});
}

void
IncastSender::LogCongEst(
    uint32_t bytesMarked, uint32_t bytesAcked, double alpha) {
  struct congEstEntry entry;
  entry.time = Simulator::Now();
  entry.bytesMarked = bytesMarked;
  entry.bytesAcked = bytesAcked;
  entry.alpha = alpha;
  m_congEstLog.push_back(entry);
}

TypeId
IncastSender::GetTypeId() {
  static TypeId tid =
      TypeId("ns3::IncastSender")
          .SetParent<Application>()
          .AddConstructor<IncastSender>()
          .AddAttribute(
              "OutputDirectory",
              "Directory for all log and pcap traces",
              StringValue("output_directory/"),
              MakeStringAccessor(&IncastSender::m_outputDirectory),
              MakeStringChecker())
          .AddAttribute(
              "TraceDirectory",
              "Sub-directory for this experiment's log and pcap traces",
              StringValue("trace_directory/"),
              MakeStringAccessor(&IncastSender::m_traceDirectory),
              MakeStringChecker())
          .AddAttribute(
              "ResponseJitterUs",
              "Max random jitter in sending responses, in microseconds",
              UintegerValue(0),
              MakeUintegerAccessor(&IncastSender::m_responseJitterUs),
              MakeUintegerChecker<uint32_t>())
          .AddAttribute(
              "Port",
              "TCP port for all applications",
              UintegerValue(8888),
              MakeUintegerAccessor(&IncastSender::m_port),
              MakeUintegerChecker<uint16_t>())
          .AddAttribute(
              "Protocol",
              "TypeId of the protocol used",
              TypeIdValue(TcpSocketFactory::GetTypeId()),
              MakeTypeIdAccessor(&IncastSender::m_tid),
              MakeTypeIdChecker())
          .AddAttribute(
              "CCA",
              "TypeId of the CCA",
              TypeIdValue(TcpSocketFactory::GetTypeId()),
              MakeTypeIdAccessor(&IncastSender::m_cca),
              MakeTypeIdChecker())
          .AddAttribute(
              "Aggregator",
              "Aggregator to send packets to",
              Ipv4AddressValue(),
              MakeIpv4AddressAccessor(&IncastSender::m_aggregator),
              MakeIpv4AddressChecker());

  return tid;
}

IncastSender::IncastSender()
    : m_socket(nullptr) {
  NS_LOG_FUNCTION(this);
}

IncastSender::~IncastSender() { NS_LOG_FUNCTION(this); }

void
IncastSender::DoDispose() {
  NS_LOG_FUNCTION(this);

  m_socket = nullptr;
  Application::DoDispose();
}

void
IncastSender::StartApplication() {
  NS_LOG_FUNCTION(this);

  // Set the log prefix based on Node ID and IP address
  std::ostringstream logPrefix;
  Ptr<Ipv4> ipv4 = GetNode()->GetObject<Ipv4>();
  Ipv4InterfaceAddress iaddr = ipv4->GetAddress(1, 0);
  Ipv4Address ipAddr = iaddr.GetLocal();
  logPrefix << "Sender ID " << GetNode()->GetId() << " (" << ipAddr << "): ";
  m_logPrefix = logPrefix.str();

  m_socket = Socket::CreateSocket(GetNode(), m_tid);
  // Enable TCP timestamp option.
  if (m_socket->GetSocketType() == Socket::NS3_SOCK_STREAM) {
    // Set the congestion control algorithm
    Ptr<TcpSocketBase> tcpSocket = DynamicCast<TcpSocketBase>(m_socket);
    ObjectFactory ccaFactory;
    ccaFactory.SetTypeId(m_cca);
    Ptr<TcpCongestionOps> ccaPtr = ccaFactory.Create<TcpCongestionOps>();
    tcpSocket->SetCongestionControlAlgorithm(ccaPtr);

    // Enable TCP timestamp option
    tcpSocket->SetAttribute("Timestamp", BooleanValue(true));
  }

  InetSocketAddress local_address =
      InetSocketAddress(Ipv4Address::GetAny(), m_port);
  if (m_socket->Bind(local_address) == -1) {
    NS_FATAL_ERROR("Worker bind failed");
  }

  m_socket->SetAcceptCallback(
      MakeNullCallback<bool, Ptr<Socket>, const Address &>(),
      MakeCallback(&IncastSender::HandleAccept, this));
  m_socket->Listen();
}

void
IncastSender::HandleRead(Ptr<Socket> socket) {
  NS_LOG_FUNCTION(this << socket);

  Ptr<Packet> packet;

  while ((packet = socket->Recv())) {
    size_t size = packet->GetSize();

    if (size == sizeof(uint32_t) || size == 1 + sizeof(uint32_t)) {
      bool containsRttProbe = (size == 1 + sizeof(uint32_t));
      uint32_t requestedBytes = ParseRequestedBytes(packet, containsRttProbe);
      NS_LOG_LOGIC(
          m_logPrefix << "Received request for " << requestedBytes << " bytes");

      // Add jitter to the first packet of the response
      Time jitter;
      if (m_responseJitterUs > 0) {
        jitter = MicroSeconds(rand() % m_responseJitterUs);
      }
      Simulator::Schedule(
          jitter, &IncastSender::SendBurst, this, socket, requestedBytes);
    } else if (size == 1) {
      // This is an RTT probe. Do nothing.
      NS_LOG_LOGIC(m_logPrefix << "Received RTT probe");
    } else {
      // Could be coalesced RTT probes: If multiple RTT probes are lost, they
      // may accumulate before being retransmited.
      NS_LOG_WARN(m_logPrefix << "Strange size received: " << size);
    }
  }
}

uint32_t
IncastSender::ParseRequestedBytes(Ptr<Packet> packet, bool containsRttProbe) {
  uint8_t *buffer = new uint8_t[packet->GetSize()];
  packet->CopyData(buffer, packet->GetSize());
  uint32_t requestedBytes = *(uint32_t *)(buffer + containsRttProbe);
  delete[] buffer;
  return requestedBytes;
}

void
IncastSender::SendBurst(Ptr<Socket> socket, uint32_t totalBytes) {
  NS_LOG_FUNCTION(this);

  // Record the start time for this flow in the current burst
  (*m_flowTimes)[*m_currentBurstCount - 1][GetNode()->GetId()].first =
      Simulator::Now();

  size_t sentBytes = 0;

  while (sentBytes < totalBytes && socket->GetTxAvailable()) {
    int toSend = totalBytes - sentBytes;
    Ptr<Packet> packet = Create<Packet>(toSend);

    NS_LOG_LOGIC(m_logPrefix << "Sending " << toSend << " bytes");
    int newSentBytes = socket->Send(packet);
    NS_LOG_LOGIC(m_logPrefix << "Sent " << newSentBytes << " bytes");

    if (newSentBytes > 0) {
      sentBytes += newSentBytes;
    } else {
      NS_FATAL_ERROR(
          m_logPrefix << "Error: could not send " << toSend
                      << " bytes. Check your SndBufSize.");
    }
  }
}

void
IncastSender::HandleAccept(Ptr<Socket> socket, const Address &from) {
  NS_LOG_FUNCTION(this << socket << from);

  InetSocketAddress addr = InetSocketAddress::ConvertFrom(from);
  NS_LOG_LOGIC(
      m_logPrefix << "Accepting connection from " << addr.GetIpv4() << ":"
                  << addr.GetPort());

  socket->SetRecvCallback(MakeCallback(&IncastSender::HandleRead, this));

  // Enable tracing for the CWND
  socket->TraceConnectWithoutContext(
      "CongestionWindow", MakeCallback(&IncastSender::LogCwnd, this));
  // Enable tracing for the RTT
  socket->TraceConnectWithoutContext(
      "RTT", MakeCallback(&IncastSender::LogRtt, this));

  Ptr<TcpSocketBase> tcpSocket = DynamicCast<TcpSocketBase>(socket);
  PointerValue congOpsValue;
  tcpSocket->GetAttribute("CongestionOps", congOpsValue);
  Ptr<TcpCongestionOps> congsOps = congOpsValue.Get<TcpCongestionOps>();
  Ptr<TcpDctcp> dctcp = DynamicCast<TcpDctcp>(congsOps);
  dctcp->TraceConnectWithoutContext(
      "CongestionEstimate", MakeCallback(&IncastSender::LogCongEst, this));
}

void
IncastSender::StopApplication() {
  NS_LOG_FUNCTION(this);

  if (m_socket) {
    m_socket->Close();
  }
}

void
IncastSender::WriteLogs() {
  std::ofstream cwndOut;
  cwndOut.open(
      m_outputDirectory + "/" + m_traceDirectory + "/log/sender" +
          std::to_string(GetNode()->GetId()) + "_cwnd.log",
      std::ios::out);
  cwndOut << "# Time (s) CWND (bytes)" << std::endl;
  for (const auto &p : m_cwndLog) {
    cwndOut << p.first.GetSeconds() << " " << p.second << std::endl;
  }
  cwndOut.close();

  std::ofstream rttOut;
  rttOut.open(
      m_outputDirectory + "/" + m_traceDirectory + "/log/sender" +
          std::to_string(GetNode()->GetId()) + "_rtt.log",
      std::ios::out);
  rttOut << "# Time (s) RTT (us)" << std::endl;
  for (const auto &p : m_rttLog) {
    rttOut << p.first.GetSeconds() << " " << p.second.GetMicroSeconds()
           << std::endl;
  }
  rttOut.close();

  std::ofstream congEstOut;
  congEstOut.open(
      m_outputDirectory + "/" + m_traceDirectory + "/log/sender" +
          std::to_string(GetNode()->GetId()) + "_congest.log",
      std::ios::out);
  congEstOut << "Time (s) BytesMarked BytesAcked Alpha" << std::endl;
  for (const auto &entry : m_congEstLog) {
    congEstOut << std::fixed << std::setprecision(9) << entry.time.GetSeconds()
               << " " << entry.bytesMarked << " " << entry.bytesAcked << " "
               << entry.alpha << std::endl;
  }
  congEstOut.close();
}

void
IncastSender::SetCurrentBurstCount(uint32_t *currentBurstCount) {
  m_currentBurstCount = currentBurstCount;
}

void
IncastSender::SetFlowTimesRecord(
    std::vector<std::unordered_map<uint32_t, std::pair<Time, Time>>>
        *flowTimes) {
  m_flowTimes = flowTimes;
}

}  // Namespace ns3