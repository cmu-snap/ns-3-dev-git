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
#include "ns3/tcp-congestion-ops.h"
#include "ns3/uinteger.h"

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
  m_cwndOut << Simulator::Now().GetSeconds() << "\t" << newCwndBytes
            << std::endl;
}

/**
 * Callback to log round-trip time changes
 *
 * \param oldRtt old round-trip time
 * \param newRtt new round-trip time
 */
void
IncastSender::LogRtt(Time oldRtt, Time newRtt) {
  m_rttOut << Simulator::Now().GetSeconds() << "\t" << newRtt.GetMicroSeconds()
           << std::endl;
}

TypeId
IncastSender::GetTypeId() {
  static TypeId tid =
      TypeId("ns3::IncastSender")
          .SetParent<Application>()
          .AddConstructor<IncastSender>()
          .AddAttribute(
              "NodeID",
              "Node ID of the sender",
              UintegerValue(0),
              MakeUintegerAccessor(&IncastSender::m_nid),
              MakeUintegerChecker<uint32_t>())
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

  m_cwndOut.open(
      "scratch/traces/sender" + std::to_string(m_nid) + "_cwnd.log",
      std::ios::out);
  m_cwndOut << "#Time(s)\tCWND (bytes)" << std::endl;

  m_rttOut.open(
      "scratch/traces/sender" + std::to_string(m_nid) + "_rtt.log",
      std::ios::out);
  m_rttOut << "#Time(s)\tRTT(us)" << std::endl;

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
      NS_LOG_LOGIC("Received request for " << requestedBytes << " bytes");

      // Add jitter to the first packet of the response
      Time jitter;
      if (m_responseJitterUs > 0) {
        jitter = MicroSeconds(rand() % m_responseJitterUs);
      }
      Simulator::Schedule(
          jitter, &IncastSender::SendBurst, this, socket, requestedBytes);
    } else if (size == 1) {
      // This is an RTT probe. Do nothing.
      NS_LOG_LOGIC("Received RTT probe");
    } else {
      NS_LOG_WARN("Strange size received: " << size);
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

  size_t sentBytes = 0;

  Ptr<TcpSocketBase> tcpSocket = DynamicCast<TcpSocketBase>(socket);
  PointerValue ccPtr;
  tcpSocket->GetAttribute("CongestionOps", ccPtr);
  Ptr<TcpCongestionOps> cc = ccPtr.Get<TcpCongestionOps>();

  while (sentBytes < totalBytes && socket->GetTxAvailable()) {
    int toSend = totalBytes - sentBytes;
    Ptr<Packet> packet = Create<Packet>(toSend);
    int newSentBytes = socket->Send(packet);

    if (newSentBytes > 0) {
      sentBytes += newSentBytes;
    } else {
      NS_FATAL_ERROR(
          "Error: could not send " << toSend
                                   << " bytes. Check your SndBufSize.");
    }
  }
}

void
IncastSender::HandleAccept(Ptr<Socket> socket, const Address &from) {
  NS_LOG_FUNCTION(this << socket << from);

  InetSocketAddress addr = InetSocketAddress::ConvertFrom(from);
  NS_LOG_LOGIC(
      "Accepting connection from " << addr.GetIpv4() << ":" << addr.GetPort());

  socket->SetRecvCallback(MakeCallback(&IncastSender::HandleRead, this));

  // Enable tracing for the CWND
  socket->TraceConnectWithoutContext(
      "CongestionWindow", MakeCallback(&IncastSender::LogCwnd, this));
  // Enable tracing for the RTT
  socket->TraceConnectWithoutContext(
      "RTT", MakeCallback(&IncastSender::LogRtt, this));
}

void
IncastSender::StopApplication() {
  NS_LOG_FUNCTION(this);

  if (m_socket) {
    m_socket->Close();
  }

  m_cwndOut.close();
  m_rttOut.close();
}

}  // Namespace ns3