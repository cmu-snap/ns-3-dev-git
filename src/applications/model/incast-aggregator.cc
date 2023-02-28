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
#include "ns3/string.h"
#include "ns3/tcp-congestion-ops.h"
#include "ns3/uinteger.h"

#include <unistd.h>

NS_LOG_COMPONENT_DEFINE("IncastAggregator");

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED(IncastAggregator);

TypeId
IncastAggregator::GetTypeId() {
  static TypeId tid =
      TypeId("ns3::IncastAggregator")
          .SetParent<Application>()
          .AddConstructor<IncastAggregator>()
          .AddAttribute(
              "NumBursts",
              "Number of bursts to simulate",
              UintegerValue(10),
              MakeUintegerAccessor(&IncastAggregator::m_numBursts),
              MakeUintegerChecker<uint32_t>())
          .AddAttribute(
              "BurstBytes",
              "For each burst, the number of bytes to request from each worker",
              UintegerValue(1448),
              MakeUintegerAccessor(&IncastAggregator::m_burstBytes),
              MakeUintegerChecker<uint32_t>())
          .AddAttribute(
              "RequestJitterUs",
              "Max random jitter in sending requests, in microseconds",
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
              ("If RwndStrategy=bdp+connections, then assume that "
               "this is the bottleneck bandwidth"),
              UintegerValue(0),
              MakeUintegerAccessor(&IncastAggregator::m_bandwidthMbps),
              MakeUintegerChecker<uint32_t>());

  return tid;
}

IncastAggregator::IncastAggregator()
    : m_numBursts(10),
      m_burstCount(0),
      m_burstBytes(1448),
      m_totalBytesSoFar(0),
      m_port(8888),
      m_requestJitterUs(0),
      m_rwndStrategy("none"),
      m_staticRwndBytes(65535) {
  NS_LOG_FUNCTION(this);
}

IncastAggregator::~IncastAggregator() { NS_LOG_FUNCTION(this); }

void
IncastAggregator::StartEvent() {
  NS_LOG_FUNCTION(this);
}

void
IncastAggregator::DoDispose() {
  NS_LOG_FUNCTION(this);

  m_sockets.clear();
  Application::DoDispose();
}

void
IncastAggregator::SetSenders(const std::list<Ipv4Address> &senders) {
  NS_LOG_FUNCTION(this);
  m_senders = senders;
}

void
IncastAggregator::StartApplication() {
  NS_LOG_FUNCTION(this);

  for (Ipv4Address sender : m_senders) {
    Ptr<Socket> socket = Socket::CreateSocket(GetNode(), m_tid);

    if (socket->GetSocketType() != Socket::NS3_SOCK_STREAM &&
        socket->GetSocketType() != Socket::NS3_SOCK_SEQPACKET) {
      NS_FATAL_ERROR(
          "Only NS_SOCK_STREAM or NS_SOCK_SEQPACKET sockets are allowed.");
    }

    // Connect to each sender
    NS_LOG_LOGIC("Connect to " << sender);

    if (socket->Bind() == -1) {
      NS_FATAL_ERROR("Aggregator bind failed");
    }

    socket->SetRecvCallback(MakeCallback(&IncastAggregator::HandleRead, this));
    socket->Connect(InetSocketAddress(sender, m_port));
    m_sockets.push_back(socket);

    if (socket->GetSocketType() == Socket::NS3_SOCK_STREAM) {
      Ptr<TcpSocketBase> tcpSocket = DynamicCast<TcpSocketBase>(socket);
      // Enable TCP timestamp option.
      tcpSocket->SetAttribute("Timestamp", BooleanValue(true));

      if (m_rwndStrategy == "static") {
        // Basic static RWND tuning. Set the RWND to 64KB for all sockets.
        if (m_staticRwndBytes < 2000 || m_staticRwndBytes > 65535) {
          NS_FATAL_ERROR(
              "RWND tuning is only supported for values in the range [2000, "
              "65535]");
        }
        tcpSocket->SetOverrideWindowSize(
            m_staticRwndBytes >> tcpSocket->GetRcvWindShift());
      }
    }
  }

  ScheduleNextBurst();
}

void
IncastAggregator::ScheduleNextBurst() {
  NS_LOG_FUNCTION(this);

  ++m_burstCount;
  m_totalBytesSoFar = 0;

  if (m_burstCount > m_numBursts) {
    Simulator::Schedule(Seconds(0), &IncastAggregator::StopApplication, this);
    return;
  }

  // Schedule the next burst for 1 second later
  Simulator::Schedule(Seconds(1), &IncastAggregator::StartBurst, this);
  // Start the RTT probes 10ms before the next burst
  ScheduleRttProbe(MilliSeconds(990));
}

void
IncastAggregator::ScheduleRttProbe(Time when) {
  NS_LOG_FUNCTION(this);

  // Schedule the next RTT probe for 1ms in the future
  Simulator::Schedule(when, &IncastAggregator::SendRttProbe, this);
}

void
IncastAggregator::SendRttProbe() {
  NS_LOG_FUNCTION(this);

  // Send a 1-byte packet to each sender
  for (Ptr<Socket> socket : m_sockets) {
    Ptr<Packet> packet = Create<Packet>(1);
    socket->Send(packet);
  }

  // If the burst is not done yet, then schedule another RTT probe
  if (m_totalBytesSoFar < m_burstBytes * m_senders.size()) {
    ScheduleRttProbe(MilliSeconds(1));
  }
}

void
IncastAggregator::StartBurst() {
  NS_LOG_FUNCTION(this);
  NS_LOG_INFO("Burst " << m_burstCount << " of " << m_numBursts);

  m_currentBurstStartTimeSec = Simulator::Now();

  for (Ptr<Socket> socket : m_sockets) {
    // Add jitter to each request
    Time jitter;
    if (m_requestJitterUs > 0) {
      jitter = MicroSeconds(rand() % m_requestJitterUs);
    }
    Simulator::Schedule(jitter, &IncastAggregator::SendRequest, this, socket);
  }
}

void
IncastAggregator::SendRequest(Ptr<Socket> socket) {
  NS_LOG_FUNCTION(this << socket);

  Ptr<Packet> packet =
      Create<Packet>((uint8_t *)&m_burstBytes, sizeof(uint32_t));
  socket->Send(packet);
}

void
IncastAggregator::HandleRead(Ptr<Socket> socket) {
  NS_LOG_FUNCTION(this << socket);

  Ptr<Packet> packet;

  while ((packet = socket->Recv())) {
    m_totalBytesSoFar += packet->GetSize();

    if (m_rwndStrategy == "bdp+connections") {
      // Set RWND based on the number of the BDP and number of connections.
      Ptr<TcpSocketBase> tcpSocket = DynamicCast<TcpSocketBase>(socket);
      auto rtt = tcpSocket->GetTcpSocketState()->m_minRtt;
      auto bdpBytes = rtt.GetSeconds() * m_bandwidthMbps * 1000000 / 8;
      auto numConns = m_sockets.size();
      uint16_t rwndBytes = (uint16_t)std::floor(bdpBytes / numConns);

      NS_LOG_LOGIC(
          "Rtt: " << rtt.As(Time::US) << ", Bandwidth: " << m_bandwidthMbps
                  << " Mbps, Connections: " << numConns << ", BDP: " << bdpBytes
                  << " bytes, RWND: " << rwndBytes << " bytes");
      if (rwndBytes < 2000) {
        NS_LOG_WARN(
            "RWND tuning is only supported for values >= 2KB, but chosen RWND "
            "is: "
            << rwndBytes << " bytes");
        rwndBytes = std::max(rwndBytes, (uint16_t)2000u);
      }
      if (rwndBytes > 65535) {
        NS_LOG_WARN(
            "RWND tuning is only supported for values <= 64KB, but chosen RWND "
            "is: "
            << rwndBytes << " bytes");
        rwndBytes = std::min(rwndBytes, (uint16_t)65535u);
      }
      NS_LOG_LOGIC(rwndBytes);
      tcpSocket->SetOverrideWindowSize(
          rwndBytes >> tcpSocket->GetRcvWindShift());
    }
  };

  if (m_totalBytesSoFar == m_burstBytes * m_senders.size()) {
    m_burstDurationsSec.push_back(
        Simulator::Now() - m_currentBurstStartTimeSec);
    ScheduleNextBurst();
  } else if (m_totalBytesSoFar > m_burstBytes * m_senders.size()) {
    NS_FATAL_ERROR("Aggregator: Received too many bytes");
  }
}

void
IncastAggregator::HandleAccept(Ptr<Socket> socket, const Address &from) {
  NS_LOG_FUNCTION(this << socket << from);

  socket->SetRecvCallback(MakeCallback(&IncastAggregator::HandleRead, this));
}

std::vector<Time>
IncastAggregator::GetBurstDurations() {
  NS_LOG_FUNCTION(this);

  return m_burstDurationsSec;
}

void
IncastAggregator::StopApplication() {
  NS_LOG_FUNCTION(this);

  for (Ptr<Socket> socket : m_sockets) {
    socket->Close();
  }
}

}  // Namespace ns3