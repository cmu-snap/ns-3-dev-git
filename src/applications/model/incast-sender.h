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

#ifndef INCAST_SENDER_H
#define INCAST_SENDER_H

#include "ns3/application.h"
#include "ns3/inet-socket-address.h"
#include "ns3/ipv4-interface-container.h"

namespace ns3 {

/**
 * @brief TODO
 */
class IncastSender : public Application {
 public:
  /**
   * @brief TODO
   */
  static TypeId GetTypeId();

  /**
   * @brief TODO
   */
  IncastSender();

  /**
   * @brief TODO
   */
  ~IncastSender() override;

 protected:
  void DoDispose() override;

 private:
  /**
   * @brief TODO
   */
  void StartApplication() override;

  /**
   * @brief TODO
   */
  void StopApplication() override;

  /**
   * @brief TODO
   */
  void SendBurst(Ptr<Socket> socket, uint32_t burstBytes);

  /**
   * @brief TODO
   */
  void HandleAccept(Ptr<Socket> socket, const Address& from);

  /**
   * @brief TODO
   */
  void HandleRead(Ptr<Socket> socket);

  /**
   * @brief TODO
   */
  static uint32_t ParseRequestedBytes(Ptr<Packet> packet);

  // TCP port for all applications
  uint16_t m_port;

  // TypeId of the protocol used
  TypeId m_tid;

  // Address of the associated aggregator
  Ipv4Address m_aggregator;

  // Socket for communication with the aggregator
  Ptr<Socket> m_socket;

  // Max random jitter in microseconds
  uint32_t m_responseJitterUs;
};

}  // namespace ns3

#endif /* INCAST_SENDER_H */