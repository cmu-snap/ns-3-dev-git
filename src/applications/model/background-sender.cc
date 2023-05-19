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

#include "background-sender.h"

NS_LOG_COMPONENT_DEFINE("BackgroundSender");

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED(BackgroundSender);

TypeId
BackgroundSender::GetTypeId() {
  static TypeId tid = TypeId("ns3::BackgroundSender")
                          .SetParent<IncastSender>()
                          .AddConstructor<BackgroundSender>();
  return tid;
}

void
BackgroundSender::SendData(Ptr<Socket> socket, uint32_t totalBytes) {
  NS_LOG_FUNCTION(
      this << " socket: " << socket << " totalBytes: " << totalBytes);

  if (m_done) {
    NS_LOG_INFO(m_logPrefix << "BackgroundSender done.");
    return;
  }

  if (*m_currentBurstCount > m_numBursts && !m_doneScheduled) {
    Simulator::Schedule(MilliSeconds(10), &BackgroundSender::Stop, this);
    m_doneScheduled = true;
  }

  // If there is space in the send buffer, then send some data. Otherwise, wait
  // until the next invocation.
  while (socket->GetTxAvailable() > totalBytes) {
    NS_LOG_INFO(m_logPrefix << "BackgroundSender sending " << totalBytes);
    if (socket->Send(Create<Packet>(totalBytes)) != totalBytes) {
      NS_FATAL_ERROR(
          m_logPrefix << "Error: Background sender could not send "
                      << totalBytes << " bytes.");
    }
  }

  // Schedule this function again in the future to tru sending more data.
  Simulator::Schedule(
      MilliSeconds(1), &BackgroundSender::SendData, this, socket, totalBytes);
}

void
BackgroundSender::Stop() {
  m_done = true;
}
}  // namespace ns3