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

namespace ns3 {

/**
 * @brief
 *
 */
class BurstSender : public IncastSender {
 public:
  /**
   * @brief TODO
   */
  static TypeId GetTypeId();

  /**
   * @brief TODO
   */
  void SetFlowTimesRecord(
      std::vector<std::unordered_map<uint32_t, std::vector<Time>>> *flowTimes);

 protected:
  /**
   * @brief TODO
   */
  void SendData(Ptr<Socket> socket, uint32_t burstBytes) override;

  // Pointer to the global record of flow start and end times, which is a vector
  // of bursts, where each entry is a maps from sender node ID to (start time,
  // time of first packet, end time).
  std::vector<std::unordered_map<uint32_t, std::vector<Time>>> *m_flowTimes;
};
}  // namespace ns3