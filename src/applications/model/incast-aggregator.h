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

#ifndef INCAST_AGGREGATOR_H
#define INCAST_AGGREGATOR_H

#include "ns3/application.h"
#include "ns3/inet-socket-address.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4-interface-container.h"

namespace ns3 {

/**
 * @brief TODO
 */
class IncastAggregator : public Application {
 public:
  /**
   * @brief TODO
   */
  static TypeId GetTypeId();

  /**
   * @brief TODO
   */
  IncastAggregator();

  /**
   * @brief TODO
   */
  ~IncastAggregator() override;

  /**
   * @brief TODO
   */
  void SetSenders(const std::vector<Ipv4Address>& senders);

  /**
   * @brief TODO
   */
  std::vector<Time> GetBurstDurations();

 protected:
  /**
   * @brief TODO
   */
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
  void ScheduleNextBurst();

  /**
   * @brief TODO
   */
  void StartBurst();

  /**
   * @brief TODO
   */
  void SendRequest(Ptr<Socket> socket);

  /**
   * @brief TODO
   */
  void HandleAccept(Ptr<Socket> socket, const Address& from);

  /**
   * @brief TODO
   */
  void HandleClose(Ptr<Socket> socket);

  /**
   * @brief TODO
   */
  void HandleRead(Ptr<Socket> socket);

  /**
   * @brief TODO
   */
  void StartRttProbes();

  /**
   * @brief TODO
   */
  void StopRttProbes();

  /**
   * @brief TODO
   */
  void SendRttProbe();

  // Directory for all log and pcap traces
  std::string m_outputDirectory;

  // Sub-directory for this experiment's log and pcap traces
  std::string m_traceDirectory;

  // Number of bursts to simulate
  uint32_t m_numBursts;

  // Which burst is currently running
  uint32_t m_burstCount;

  // For each burst, the number of bytes to request from each sender
  uint32_t m_bytesPerSender;

  // The number of total bytes received from all workers in the current burst
  uint32_t m_totalBytesSoFar;

  // TCP port for all applications
  uint16_t m_port;

  // TypeId of the protocol used
  TypeId m_tid;

  // List of associated sockets
  std::vector<Ptr<Socket>> m_sockets;

  // List of addresses for associated senders
  std::vector<Ipv4Address> m_senders;

  // Max random jitter in microseconds
  uint32_t m_requestJitterUs;

  // Start time of the current burst
  Time m_currentBurstStartTimeSec;

  // List of all burst durations
  std::vector<Time> m_burstDurationsSec;

  // RWND tuning strategy to use [none, static, bdp+connections]
  std::string m_rwndStrategy;

  // If m_rwndStrategy=static, then use this static RWND value
  uint32_t m_staticRwndBytes;

  // TODO
  uint32_t m_bandwidthMbps;

  // Assumes that all senders have the same RTT.
  Time m_physicalRtt;
  Time m_minRtt;

  // TODO
  bool m_probingRtt;

  // TCP congestion control algorithm
  TypeId m_cca;

  // Log streams
  std::ofstream m_burstTimesOut;
  std::ofstream m_cwndOut;
  std::ofstream m_rttOut;
};

}  // namespace ns3

#endif /* INCAST_AGGREGATOR_H */