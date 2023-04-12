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

#include "incast-sender.h"

#include "ns3/application.h"
#include "ns3/inet-socket-address.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4-interface-container.h"
#include "ns3/ptr.h"

#include <unordered_map>
#include <utility>
#include <vector>

namespace ns3 {

const uint16_t MSS = 1448;
const uint16_t MIN_RWND = MSS;
const uint16_t MAX_RWND = 65535;

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
  void SetSenders(
      std::unordered_map<uint32_t, std::pair<Ptr<IncastSender>, Ipv4Address>>
          *senders);

  /**
   * @brief TODO
   */
  std::vector<std::pair<Time, Time>> GetBurstTimes();

  void WriteLogs();

  void SetCurrentBurstCount(uint32_t *currentBurstCount);

  void SetFlowTimesRecord(
      std::vector<std::unordered_map<uint32_t, std::pair<Time, Time>>>
          *flowTimes);

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
  void SendRequest(Ptr<Socket> socket, bool createNewConn);

  /**
   * @brief TODO
   */
  void HandleAccept(Ptr<Socket> socket, const Address &from);

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

  /**
   * @brief Set a socket's advertised window, making sure it's in the valid
   * range.
   */
  void SafelySetRwnd(
      Ptr<TcpSocketBase> tcpSocket, uint32_t rwndBytes, bool allowBelowMinRwnd);

  /**
   * @brief Perform static RWND tuning. To be used on connection setup.
   */
  void StaticRwndTuning(Ptr<TcpSocketBase> tcpSocket);

  /**
   * @brief Perform dynamic RWND tuning. To be used after every read.
   */
  void DynamicRwndTuning(Ptr<TcpSocketBase> tcpSocket);

  /**
   * @brief Perform scheduled RWND tuning. Allow at most
   * m_rwndScheduleMaxTokens concurrent senders.
   */
  void ScheduledRwndTuning(Ptr<TcpSocketBase> tcpSocket, bool socketDone);

  void CloseConnections();

  Ptr<Socket> SetupConnection(Ipv4Address sender, bool scheduleNextBurst);

  /**
   * @brief Look up the node ID of the first sender (lowest ID we know about).
   */
  uint32_t GetFirstSender();

  // Directory for all log and pcap traces
  std::string m_outputDirectory;

  // Sub-directory for this experiment's log and pcap traces
  std::string m_traceDirectory;

  // Number of bursts to simulate
  uint32_t m_numBursts;

  // Pointer to the global record which burst is currently running.
  uint32_t *m_currentBurstCount;

  // For each burst, the number of bytes to request from each sender
  uint32_t m_bytesPerSender;

  // The number of total bytes received from all workers in the current burst
  uint32_t m_totalBytesSoFar;

  // TCP port for all applications
  uint16_t m_port;

  // TypeId of the protocol used
  TypeId m_tid;

  // Map from socket to remote node ID
  std::unordered_map<Ptr<Socket>, uint32_t> m_sockets;

  // Max random jitter in microseconds
  uint32_t m_requestJitterUs;

  // Start time of the current burst
  Time m_currentBurstStartTime;

  // RWND tuning strategy to use [none, static, bdp+connections]
  std::string m_rwndStrategy;

  // If m_rwndStrategy=static, then use this static RWND value
  uint32_t m_staticRwndBytes;

  // The max number of tokens that can be claimed at once. Used to reset
  // m_rwndScheduleAvailableTokens.
  uint32_t m_rwndScheduleMaxTokens;

  // The current number of tokens that can be claimed.
  uint32_t m_rwndScheduleAvailableTokens;

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
  std::vector<std::pair<Time, Time>> m_burstTimesLog;
  std::vector<std::pair<Time, uint32_t>> m_cwndLog;
  std::vector<std::pair<Time, Time>> m_rttLog;

  // Maps sockets to the number of bytes received during the current burst
  std::unordered_map<Ptr<Socket>, uint32_t> m_bytesReceived;

  // Number of senders that have finished the current burst
  uint32_t m_sendersFinished;

  // Pointer to the global record of flow start and end times, which is a
  // vector of bursts, where each entry is a maps from sender node ID to
  // (start time, end time) pair.
  std::vector<std::unordered_map<uint32_t, std::pair<Time, Time>>> *m_flowTimes;

  // Point to the global record of senders, which maps sender node ID to a
  // pair of SenderApp and sender IP address.
  std::unordered_map<uint32_t, std::pair<Ptr<IncastSender>, Ipv4Address>>
      *m_senders;

  // Time to delay the request for the first sender in each burst (in
  // milliseconds). Overrides any jitter at the aggregator node. 0 means no
  // delay, and use jitter instead.
  Time m_firstFlowOffset;
};

}  // namespace ns3

#endif /* INCAST_AGGREGATOR_H */