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
#include "incast-agg.h"

NS_LOG_COMPONENT_DEFINE ("IncastAggregator");

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED (IncastAggregator);

TypeId
IncastAggregator::GetTypeId()
{
  static TypeId tid = TypeId ("ns3::IncastAggregator")
    .SetParent<Application> ()
    .AddConstructor<IncastAggregator> ()
    .AddAttribute ("Port",
                   "TCP port number for the incast applications",
                   UintegerValue (5000),
                   MakeUintegerAccessor (&IncastAggregator::m_port),
                   MakeUintegerChecker<uint16_t> ())
    .AddAttribute ("Initiator",
                   "The aggregator is the initiator of the connections",
                   BooleanValue (true),
                   MakeBooleanAccessor (&IncastAggregator::m_init),
                   MakeBooleanChecker ())
    .AddAttribute ("Protocol", "The type of connection-oriented protocol to use.",
                   TypeIdValue (TcpSocketFactory::GetTypeId()),
                   MakeTypeIdAccessor (&IncastAggregator::m_tid),
                   MakeTypeIdChecker ())
  ;
  return tid;
}


IncastAggregator::IncastAggregator ()
  : m_running (false),
    m_listened (false)
{
  NS_LOG_FUNCTION (this);
}

IncastAggregator::~IncastAggregator ()
{
  NS_LOG_FUNCTION (this);
}

void
IncastAggregator::DoDispose (void)
{
  NS_LOG_FUNCTION (this);

  m_sockets.clear ();
  // chain up
  Application::DoDispose ();
}

void
IncastAggregator::SetSenders (const std::list<Ipv4Address>& n)
{
  NS_LOG_FUNCTION (this);
  m_senders = n;
}

// Application Methods
void IncastAggregator::StartApplication (void) // Called at time specified by Start
{
  NS_LOG_FUNCTION (this);

  // Do nothing if incast is running
  if (m_running) return;

  m_running = true;
  m_closeCount = 0;

  if (m_init)
    { // Connection initiator. Connect to each peer and wait for data.
      for (std::list<Ipv4Address>::iterator i = m_senders.begin (); i != m_senders.end (); ++i)
        {
          Ptr<TcpSocketBase> s = GetNode ()->GetObject<TcpL4Protocol> ()->CreateSocket(m_tid)->GetObject<TcpSocketBase> ();
          // Fatal error if socket type is not NS3_SOCK_STREAM or NS3_SOCK_SEQPACKET
          if (s->GetSocketType () != Socket::NS3_SOCK_STREAM &&
              s->GetSocketType () != Socket::NS3_SOCK_SEQPACKET)
            {
              NS_FATAL_ERROR ("Only NS_SOCK_STREAM or NS_SOCK_SEQPACKET sockets are allowed.");
            }

          // Bind, connect, and wait for data
          NS_LOG_LOGIC ("Connect to " << *i);
          s->Bind ();
          s->Connect (InetSocketAddress (*i, m_port));
          s->ShutdownSend ();
          s->SetRecvCallback (MakeCallback (&IncastAggregator::HandleRead, this));
          s->SetCloseCallbacks (
            MakeCallback (&IncastAggregator::HandleClose, this),
            MakeCallback (&IncastAggregator::HandleClose, this));
          // s->SetAdmCtrlCallback (MakeCallback (&IncastAggregator::HandleAdmCtrl, this));
          // Remember this socket, in case we need to terminate all of them prematurely
          m_sockets.push_back(s);
        }
    }
  else if (!m_listened)
    { // Connection responder. Wait for connection.
      Ptr<TcpSocketBase> s = GetNode ()->GetObject<TcpL4Protocol> ()->CreateSocket(m_tid)->GetObject<TcpSocketBase> ();
      s->Bind (InetSocketAddress (Ipv4Address::GetAny (), m_port));
      s->Listen ();
      s->ShutdownSend ();
      s->SetRecvCallback (MakeCallback (&IncastAggregator::HandleRead, this));
      s->SetAcceptCallback (
        MakeNullCallback<bool, Ptr<Socket>, const Address &> (),
        MakeCallback (&IncastAggregator::HandleAccept, this));
      // s->SetAdmCtrlCallback (MakeCallback (&IncastAggregator::HandleAdmCtrl, this));
      m_listened = true;
      // Remember this listening socket, terminate this when application finishes
      m_sockets.push_back(s);
    };
}

void IncastAggregator::HandleAdmCtrl (Ptr<TcpSocketBase> sock, Ipv4Address addr, uint16_t port)
{
  NS_LOG_FUNCTION (this << addr << port);
  m_suspendedSocket.push_back(sock);
}

void IncastAggregator::HandleRead (Ptr<Socket> socket)
{
  // Discard all data being read
  NS_LOG_FUNCTION (this << socket);
  Ptr<Packet> packet;
  uint32_t bytecount = 0;
  while (packet == socket->Recv ())
    {
      bytecount += packet->GetSize ();
    };
  std::list<Ptr<Socket> >::iterator i = std::find(m_runningSockets.begin (), m_runningSockets.end (), socket);
  std::list<uint32_t>::iterator p = m_byteCount.begin ();
  if (i == m_runningSockets.end ())
    {
      m_runningSockets.push_back (socket);
      p = m_byteCount.insert (m_byteCount.end (), bytecount);
    }
  else
    {
      std::list<Ptr<Socket> >::iterator q = m_runningSockets.begin ();
      while (q != i)
        {
          ++q; ++p;
        }
      *p += bytecount;
    }
  std::list<uint32_t>::iterator mincount;
  std::list<uint32_t>::iterator maxcount;
  mincount = std::min_element (m_byteCount.begin(), m_byteCount.end());
  maxcount = std::max_element (m_byteCount.begin(), m_byteCount.end());
  if ((*maxcount - *mincount >= 4096) && (*maxcount - *p <= 2048))
    {
      socket->SetAttribute ("MaxWindowSize", UintegerValue(6000));
    }
  else
    {
      socket->SetAttribute ("MaxWindowSize", UintegerValue(65535));
    }
}

void IncastAggregator::HandleClose (Ptr<Socket> socket)
{
  NS_LOG_FUNCTION (this << socket);
  ++m_closeCount;
  m_sockets.remove (socket);
  std::list<uint32_t>::iterator p = m_byteCount.begin ();
  std::list<Ptr<Socket> >::iterator q = m_runningSockets.begin ();
  while (*q != socket && q != m_runningSockets.end ())
    {
      ++q; ++p;
    }
  if (q != m_runningSockets.end ())
    {
      m_byteCount.erase (p);
      m_runningSockets.erase (q);
    }
  if (m_closeCount == m_senders.size ())
    {
      // Start next round of incast
      m_running = false;
      Simulator::ScheduleNow (&IncastAggregator::RoundFinish, this);
    }
  else if (!m_suspendedSocket.empty())
    {
      // Replace the terminated connection with a suspended one
      Ptr<TcpSocketBase> sock = m_suspendedSocket.front();
      m_suspendedSocket.pop_front();
      // sock->ResumeConnection ();
    }
}

void IncastAggregator::RoundFinish (void)
{
  NS_LOG_FUNCTION (this);
  if (!m_roundFinish.IsNull ()) m_roundFinish ();
}

void IncastAggregator::HandleAccept (Ptr<Socket> socket, const Address& from)
{
  NS_LOG_FUNCTION (this << socket << from);
  socket->SetRecvCallback (MakeCallback (&IncastAggregator::HandleRead, this));
  socket->SetCloseCallbacks (
    MakeCallback (&IncastAggregator::HandleClose, this),
    MakeCallback (&IncastAggregator::HandleClose, this));
}

void IncastAggregator::StopApplication (void) // Called at time specified by Stop
{
  NS_LOG_FUNCTION (this);

  for (std::list<Ptr<Socket> >::iterator i = m_sockets.begin (); i != m_sockets.end (); ++i)
    {
      (*i)->Close ();
    }
}

void IncastAggregator::SetRoundFinishCallback (Callback<void> cb)
{
  NS_LOG_FUNCTION (this);
  m_roundFinish = cb;
};

} // Namespace ns3