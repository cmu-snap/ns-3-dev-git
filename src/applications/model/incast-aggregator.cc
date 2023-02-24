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

#include <unistd.h>

#include "ns3/boolean.h"
#include "ns3/internet-module.h"
#include "ns3/log.h"
#include "ns3/tcp-congestion-ops.h"
#include "ns3/uinteger.h"

NS_LOG_COMPONENT_DEFINE("IncastAggregator");

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED(IncastAggregator);

TypeId IncastAggregator::GetTypeId() {
  static TypeId tid =
      TypeId("ns3::IncastAggregator")
          .SetParent<Application>()
          .AddConstructor<IncastAggregator>()
          .AddAttribute("NumBursts", "Number of bursts to simulate",
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
          .AddAttribute("Port", "TCP port for all applications",
                        UintegerValue(8888),
                        MakeUintegerAccessor(&IncastAggregator::m_port),
                        MakeUintegerChecker<uint16_t>())
          .AddAttribute("Protocol", "TypeId of the protocol used",
                        TypeIdValue(TcpSocketFactory::GetTypeId()),
                        MakeTypeIdAccessor(&IncastAggregator::m_tid),
                        MakeTypeIdChecker());

  return tid;
}

IncastAggregator::IncastAggregator() : m_burstCount(0), m_totalBytesSoFar(0) {
  NS_LOG_FUNCTION(this);
}

IncastAggregator::~IncastAggregator() { NS_LOG_FUNCTION(this); }

void IncastAggregator::StartEvent() { NS_LOG_FUNCTION(this); }

void IncastAggregator::DoDispose() {
  NS_LOG_FUNCTION(this);

  m_sockets.clear();
  Application::DoDispose();
}

void IncastAggregator::SetSenders(const std::list<Ipv4Address>& senders) {
  NS_LOG_FUNCTION(this);
  m_senders = senders;
}

void IncastAggregator::StartApplication() {
  NS_LOG_FUNCTION(this);

  // // Do nothing if incast is running
  // if (m_isRunning) {
  //   return;
  // }

  // m_isRunning = true;

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
    } else {
      std::cout << "Aggregator bind succeeded\n";
    }

    socket->SetRecvCallback(MakeCallback(&IncastAggregator::HandleRead, this));
    // socket->SetAcceptCallback(
    //   MakeNullCallback<bool, Ptr<Socket>, const Address &>(),
    //   MakeCallback(&IncastAggregator::HandleAccept, this)
    // );
    std::cout << "Aggregator registered accept callback\n";

    socket->Connect(InetSocketAddress(sender, m_port));
    // socket->ShutdownSend();
    m_sockets.push_back(socket);
  }

  // for (uint32_t burstCount = 0; burstCount < m_numBursts; ++burstCount) {
  //   ScheduleBurst(burstCount);
  // }
  ScheduleNextBurst();
}

void IncastAggregator::ScheduleNextBurst() {
  NS_LOG_FUNCTION(this);

  std::cout << "Aggregator: Schedule next burst " << m_burstCount << ", "
            << m_numBursts << std::endl;

  ++m_burstCount;

  if (m_burstCount > m_numBursts) {
    Simulator::Schedule(Seconds(0), &IncastAggregator::StopApplication, this);
    return;
  }

  // Schedule the next burst for 1 second later
  Simulator::Schedule(Seconds(1), &IncastAggregator::StartBurst, this);
}

void IncastAggregator::StartBurst() {
  NS_LOG_FUNCTION(this);
  std::cout << "Start burst " << std::endl;

  m_totalBytesSoFar = 0;
  m_currentBurstStartTimeSec = Simulator::Now();

  for (Ptr<Socket> socket : m_sockets) {
    double jitterSec = 0;
    if (m_requestJitterUs > 0) {
      jitterSec = ((double)(rand() % m_requestJitterUs)) / 1000000;
    }

    Simulator::Schedule(Seconds(jitterSec), &IncastAggregator::SendRequest,
                        this, socket);
  }
}

void IncastAggregator::SendRequest(Ptr<Socket> socket) {
  Ptr<Packet> packet =
      Create<Packet>((uint8_t*)&m_burstBytes, sizeof(uint32_t));
  socket->Send(packet);
}

void IncastAggregator::HandleRead(Ptr<Socket> socket) {
  NS_LOG_FUNCTION(this << socket);
  // std::cout << "Aggregator: HandleRead()" << std::endl;
  Ptr<Packet> packet;
  while (packet = socket->Recv()) {
    m_totalBytesSoFar += packet->GetSize();
  };

  if (m_totalBytesSoFar == m_burstBytes * m_senders.size()) {
    std::cout << "Aggregator: All bytes received" << std::endl;
    m_burstDurationsSec.push_back(Simulator::Now() -
                                  m_currentBurstStartTimeSec);
    ScheduleNextBurst();
  } else if (m_totalBytesSoFar > m_burstBytes * m_senders.size()) {
    NS_FATAL_ERROR("Aggregator: Received too many bytes");
  }
}

void IncastAggregator::HandleAccept(Ptr<Socket> socket, const Address& from) {
  NS_LOG_FUNCTION(this << socket << from);
  std::cout << "Aggregator: HandleAccept()" << std::endl;

  socket->SetRecvCallback(MakeCallback(&IncastAggregator::HandleRead, this));
}

std::vector<Time> IncastAggregator::GetBurstDurations() {
  return m_burstDurationsSec;
}

void IncastAggregator::StopApplication() {
  NS_LOG_FUNCTION(this);
  std::cout << "Aggregator: StopApplication()" << std::endl;

  for (Ptr<Socket> socket : m_sockets) {
    // Send a large packet
    // Ptr<Packet> packet = Create<Packet>(50);
    // socket->Send(packet);
    socket->Close();
  }
}

// void IncastAggregator::SetRoundFinishCallback(Callback<void> callback) {
//   NS_LOG_FUNCTION(this);

//   m_roundFinish = callback;
// };

}  // Namespace ns3