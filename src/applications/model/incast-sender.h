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
#include "ns3/pointer.h"
#include "ns3/tcp-header.h"
#include "ns3/tcp-socket-base.h"

#include <string>
#include <utility>
#include <vector>

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
  ~IncastSender();

  /**
   * @brief TODO
   */
  void WriteLogs();

  /**
   * @brief TODO
   */
  void SetCurrentBurstCount(uint32_t *currentBurstCount);

 protected:
  void DoDispose();

  /**
   * @brief TODO
   */
  virtual void SendData(Ptr<Socket> socket, uint32_t burstBytes);

  // Prefix to prepend to all NS_LOG_* messages
  std::string m_logPrefix;

  // Max random jitter in microseconds
  uint32_t m_responseJitterUs;

  // Pointer to the global record which burst is currently running.
  uint32_t *m_currentBurstCount;

  // Number of bursts to simulate
  uint32_t m_numBursts;

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
  void LogCongEst(uint32_t bytesMarked, uint32_t bytesAcked, double alpha);

  /**
   * @brief TODO
   */
  void LogTx(
      Ptr<const Packet> packet,
      const TcpHeader &tcpHeader,
      Ptr<const TcpSocketBase> tcpSocket);

  /**
   * @brief TODO
   */
  void HandleAccept(Ptr<Socket> socket, const Address &from);

  /**
   * @brief TODO
   */
  static uint32_t ParseRequestedBytes(
      Ptr<Packet> packet, bool containsRttProbe);

  /**
   * @brief TODO
   */
  void HandleRead(Ptr<Socket> socket);

  /**
   * @brief TODO
   */
  void StartApplication();

  /**
   * @brief TODO
   */
  void StopApplication();

  // Directory for all log and pcap traces
  std::string m_outputDirectory;

  // Sub-directory for this experiment's log and pcap traces
  std::string m_traceDirectory;

  // TCP port for all applications
  uint16_t m_port;

  // TypeId of the protocol used
  TypeId m_tid;

  // Address of the associated aggregator
  Ipv4Address m_aggregator;

  // Socket for listening for connections from the aggregator
  Ptr<Socket> m_socket{nullptr};

  // TCP congestion control algorithm
  TypeId m_cca;

  // Struct for logging congestion estimate info
  struct congEstEntry {
    Time time;
    uint32_t bytesMarked;
    uint32_t bytesAcked;
    double alpha;
  };

  // Log streams
  std::vector<std::pair<Time, uint32_t>> m_cwndLog;
  std::vector<std::pair<Time, Time>> m_rttLog;
  std::vector<struct congEstEntry> m_congEstLog;
  std::vector<Time> m_txLog;

  // Parameter G for updating dctcp_alpha.
  double m_dctcpShiftG{0.0625};
};

}  // namespace ns3

#endif /* INCAST_SENDER_H */