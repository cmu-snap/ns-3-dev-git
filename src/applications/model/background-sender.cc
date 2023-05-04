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

// #include "ns3/boolean.h"
#include "ns3/internet-module.h"
// #include "ns3/log.h"
#include "ns3/pointer.h"
#include "ns3/string.h"
// #include "ns3/tcp-congestion-ops.h"
// #include "ns3/uinteger.h"

#include <fstream>
#include <iomanip>
#include <iostream>

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

  while (*m_currentBurstCount <= m_numBursts) {
    Ptr<Packet> packet = Create<Packet>(totalBytes);
    int newSentBytes = socket->Send(packet);

    if (newSentBytes <= 0) {
      NS_FATAL_ERROR(
          m_logPrefix
          << "Error: could not send data from the background sender.");
    }
  }
}
}  // namespace ns3