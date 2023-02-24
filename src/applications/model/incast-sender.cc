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
#include "ns3/tcp-congestion-ops.h"
#include "ns3/uinteger.h"

NS_LOG_COMPONENT_DEFINE("IncastSender");

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED(IncastSender);

TypeId IncastSender::GetTypeId() {
  static TypeId tid =
      TypeId("ns3::IncastSender")
          .SetParent<Application>()
          .AddConstructor<IncastSender>()
          .AddAttribute(
              "ResponseJitterUs",
              "Max random jitter in sending responses, in microseconds",
              UintegerValue(0),
              MakeUintegerAccessor(&IncastSender::m_responseJitterUs),
              MakeUintegerChecker<uint32_t>())
          .AddAttribute("Port", "TCP port for all applications",
                        UintegerValue(8888),
                        MakeUintegerAccessor(&IncastSender::m_port),
                        MakeUintegerChecker<uint16_t>())
          .AddAttribute("Protocol", "TypeId of the protocol used",
                        TypeIdValue(TcpSocketFactory::GetTypeId()),
                        MakeTypeIdAccessor(&IncastSender::m_tid),
                        MakeTypeIdChecker())
          .AddAttribute("Aggregator", "Aggregator to send packets to",
                        Ipv4AddressValue(),
                        MakeIpv4AddressAccessor(&IncastSender::m_aggregator),
                        MakeIpv4AddressChecker());

  return tid;
}

IncastSender::IncastSender() : m_socket(nullptr) { NS_LOG_FUNCTION(this); }

IncastSender::~IncastSender() { NS_LOG_FUNCTION(this); }

void IncastSender::DoDispose() {
  NS_LOG_FUNCTION(this);

  m_socket = nullptr;
  Application::DoDispose();
}

void IncastSender::StartApplication() {
  NS_LOG_FUNCTION(this);

  m_socket = Socket::CreateSocket(GetNode(), m_tid);
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

void IncastSender::HandleRead(Ptr<Socket> socket) {
  NS_LOG_FUNCTION(this << socket);

  Ptr<Packet> packet;

  while ((packet = socket->Recv())) {
    size_t size = packet->GetSize();

    if (size == sizeof(uint32_t)) {
      uint32_t requestedBytes = ParseRequestedBytes(packet);

      double jitterSec = 0;
      if (m_responseJitterUs > 0) {
        jitterSec = ((double)(rand() % m_responseJitterUs)) / 1000000;
      }
      Time time = Seconds(jitterSec);
      Simulator::Schedule(time, &IncastSender::SendBurst, this, socket,
                          requestedBytes);
    } else {
      break;
    }
  }
}

uint32_t IncastSender::ParseRequestedBytes(Ptr<Packet> packet) {
  uint8_t *buffer = new uint8_t[packet->GetSize()];
  packet->CopyData(buffer, packet->GetSize());
  uint32_t requestedBytes = *(uint32_t *)buffer;
  delete[] buffer;
  return requestedBytes;
}

void IncastSender::SendBurst(Ptr<Socket> socket, uint32_t burstBytes) {
  NS_LOG_FUNCTION(this);

  size_t sentBytes = 0;
  
  while (sentBytes < burstBytes && socket->GetTxAvailable()) {
    int toSend = burstBytes - sentBytes;
    Ptr<Packet> packet = Create<Packet>(toSend);
    int newSentBytes = socket->Send(packet);

    if (newSentBytes > 0) {
      sentBytes += newSentBytes;
    } else {
      NS_LOG_LOGIC("Error: could not send " << toSend << " bytes");
      std::cout << "Error: could not send " << toSend << " bytes\n";
      break;
    }
  }
}

void IncastSender::HandleAccept(Ptr<Socket> socket, const Address &from) {
  NS_LOG_FUNCTION(this << socket << from);

  InetSocketAddress addr = InetSocketAddress::ConvertFrom(from);
  NS_LOG_LOGIC("Accepting connection from " << addr.GetIpv4() << ":"
                                            << addr.GetPort());

  socket->SetRecvCallback(MakeCallback(&IncastSender::HandleRead, this));
}

void IncastSender::StopApplication() {
  NS_LOG_FUNCTION(this);

  if (m_socket) {
    m_socket->Close();
  }
}

} // Namespace ns3