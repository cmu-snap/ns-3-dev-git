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

#include "burst-sender.h"

NS_LOG_COMPONENT_DEFINE("BurstSender");

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED(BurstSender);

TypeId
BurstSender::GetTypeId() {
  static TypeId tid = TypeId("ns3::BurstSender")
                          .SetParent<IncastSender>()
                          .AddConstructor<BurstSender>();

  return tid;
}

void
BurstSender::SetFlowTimesRecord(
    std::vector<std::unordered_map<uint32_t, std::vector<Time>>> *flowTimes) {
  NS_LOG_FUNCTION(this << flowTimes);

  m_flowTimes = flowTimes;
}

void
BurstSender::SendData(Ptr<Socket> socket, uint32_t totalBytes) {
  NS_LOG_FUNCTION(
      this << " socket: " << socket << " totalBytes: " << totalBytes);

  // TODO: Update start time to be when the first packet is actually sent to
  // make graphs meaningful for scheduled RWND strategy.

  // Record the start time for this flow in the current burst
  (*m_flowTimes)[*m_currentBurstCount - 1][GetNode()->GetId()] = {
      Simulator::Now(), Seconds(0), Seconds(0)};

  size_t sentBytes = 0;

  while (sentBytes < totalBytes && socket->GetTxAvailable()) {
    int toSend = totalBytes - sentBytes;
    Ptr<Packet> packet = Create<Packet>(toSend);

    int newSentBytes = socket->Send(packet);
    if (newSentBytes > 0) {
      sentBytes += newSentBytes;
    } else {
      NS_FATAL_ERROR(
          m_logPrefix << "Error: could not send " << toSend
                      << " bytes. Check your SndBufSize.");
    }
  }
}
}  // namespace ns3