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
class BackgroundSender : public IncastSender {
 public:
  /**
   * @brief TODO
   */
  static TypeId GetTypeId();

 protected:
  /**
   * @brief Repeatedly send totalBytes through socket until the final burst has
   * completed.
   */
  void SendData(Ptr<Socket> socket, uint32_t totalBytes) override;

  /**
   * @brief Schedule this function to set m_done.
   */
  void Stop();

 private:
  // Whether this background sender should be done and stop sending. Set by
  // Stop().
  bool m_done = false;

  // Whether we have scheduled an outstanding call to Stop() to set m_done.
  bool m_doneScheduled = false;
};
}  // namespace ns3