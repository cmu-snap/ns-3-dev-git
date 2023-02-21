/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2011 New York University
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
 * Author: Adrian Sai-wah Tam <adrian.sw.tam@gmail.com>
 */

#include "ns3/log.h"
#include "ns3/uinteger.h"
#include "ns3/boolean.h"
#include "ns3/internet-module.h"
#include "ns3/tcp-congestion-ops.h"
#include "incast-send.h"

NS_LOG_COMPONENT_DEFINE ("IncastSender");

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED (IncastSender);

TypeId
IncastSender::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::IncastSender")
    .SetParent<Application> ()
    .AddConstructor<IncastSender> ()
    .AddAttribute ("SRU",
                   "Size of one server request unit",
                   UintegerValue (1000),
                   MakeUintegerAccessor (&IncastSender::m_sru),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("Port",
                   "TCP port number for the incast applications",
                   UintegerValue (5000),
                   MakeUintegerAccessor (&IncastSender::m_port),
                   MakeUintegerChecker<uint16_t> ())
    .AddAttribute ("Initiator",
                   "The sender is the initiator of the connections",
                   BooleanValue (false),
                   MakeBooleanAccessor (&IncastSender::m_init),
                   MakeBooleanChecker ())
    .AddAttribute ("Aggregator", "The address of the aggregator",
                   Ipv4AddressValue (),
                   MakeIpv4AddressAccessor (&IncastSender::m_agg),
                   MakeIpv4AddressChecker ())
    .AddAttribute ("Protocol", "The type of connection-oriented protocol to use.",
                  //  TypeIdValue(TcpNewReno::GetTypeId()),
                   TypeIdValue(TcpSocketFactory::GetTypeId()),
                   MakeTypeIdAccessor (&IncastSender::m_tid),
                   MakeTypeIdChecker ())
    .AddAttribute ("BurstCount",
                  //  "TCP port number for the incast applications",
                  "",
                   UintegerValue (10),
                   MakeUintegerAccessor (&IncastSender::m_burstCount),
                   MakeUintegerChecker<uint32_t> ())
  ;
  return tid;
}

IncastSender::IncastSender () : m_socket (0)
{
  NS_LOG_FUNCTION (this);
}

IncastSender::~IncastSender ()
{
  NS_LOG_FUNCTION (this);
}

void
IncastSender::DoDispose (void)
{
  NS_LOG_FUNCTION (this);

  m_socket = 0;
  // chain up
  Application::DoDispose ();
}

// Application Methods
void IncastSender::StartApplication (void) // Called at time specified by Start
{
  NS_LOG_FUNCTION (this);

  for (uint32_t i = 0; i < m_burstCount; ++i) {
    // Connection responder. Wait for connection and send data.
    m_sentCount = 0;
    // m_socket  = GetNode ()->GetObject<TcpL4Protocol> ()->CreateSocket(m_tid);
    m_socket = Socket::CreateSocket(GetNode (), m_tid);
    m_socket->Bind (InetSocketAddress (Ipv4Address::GetAny (), m_port));
    m_socket->Listen ();
    m_socket->ShutdownRecv ();
    m_socket->SetSendCallback (MakeCallback (&IncastSender::HandleSend, this));
    m_socket->SetAcceptCallback (
      MakeNullCallback<bool, Ptr<Socket>, const Address &> (),
      MakeCallback (&IncastSender::HandleAccept, this));
  }
}

void IncastSender::HandleSend (Ptr<Socket> socket, uint32_t n)
{
  NS_LOG_FUNCTION (this);

  while (m_sentCount < m_sru && socket->GetTxAvailable ())
    { // Not yet finish the one SRU data
      Ptr<Packet> packet = Create<Packet> (std::min(m_sru - m_sentCount, n));
      int actual = socket->Send (packet);
      if (actual > 0) m_sentCount += actual;
      NS_LOG_LOGIC(actual << " bytes sent");
    }
  // If finished, close the socket
  if (m_sentCount == m_sru)
    {
      NS_LOG_LOGIC("Socket close");
      socket->Close ();
    }
}

void IncastSender::HandleAccept (Ptr<Socket> socket, const Address& from)
{
  NS_LOG_FUNCTION (this << socket);
  InetSocketAddress addr = InetSocketAddress::ConvertFrom(from);
  NS_LOG_LOGIC ("Accepting connection from " << addr.GetIpv4() << ":" <<addr.GetPort());
  if (!m_init)
    { // Stop listening sockets to prevent parallel connections
      m_socket->Close ();
      m_socket = socket;
    };
  socket->SetSendCallback (MakeCallback (&IncastSender::HandleSend, this));
}

void IncastSender::StopApplication (void) // Called at time specified by Stop
{
  NS_LOG_FUNCTION (this);

  if (m_socket) m_socket->Close ();
}

} // Namespace ns3