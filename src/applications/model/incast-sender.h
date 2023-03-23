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
 * @brief
 *
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

  void WriteLogs();

 protected:
  void DoDispose() override;

 private:
  /**
   * Callback to log congestion window changes
   *
   * \param oldCwnd old congestion window
   * \param newCwnd new congestion window
   */
  void LogCwnd(uint32_t oldCwnd, uint32_t newCwnd);

  /**
   * Callback to log round-trip time changes
   *
   * \param oldRtt old round-trip time
   * \param newRtt new round-trip time
   */
  void LogRtt(Time oldRtt, Time newRtt);

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
  static uint32_t ParseRequestedBytes(
      Ptr<Packet> packet, bool containsRttProbe);

  // Directory for all log and pcap traces
  std::string m_outputDirectory;

  // Sub-directory for this experiment's log and pcap traces
  std::string m_traceDirectory;

  // Node ID of the sender
  uint32_t m_nid;

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

  // TCP congestion control algorithm
  TypeId m_cca;

  // Log streams
  std::vector<std::pair<Time, uint32_t>> m_cwndLog;
  std::vector<std::pair<Time, Time>> m_rttLog;

  // Prefix to prepend to all NS_LOG_* messages
  std::string m_logPrefix;
};

}  // namespace ns3

#endif /* INCAST_SENDER_H */