#include "ns3/internet-module.h"
// #include "ns3/log.h"
// #include "ns3/pointer.h"
// #include "ns3/string.h"
// #include "ns3/tcp-congestion-ops.h"
// #include "ns3/uinteger.h"

#include <fstream>
#include <iomanip>
#include <iostream>

NS_LOG_COMPONENT_DEFINE("BackgroundSender");

namespace ns3 {
NS_OBJECT_ENSURE_REGISTERED(BackgroundSender);

// TODO: move to base class + double check senddata specificity 
// TODO: modify the aggregator's first message (m_sockets => m_burstSockets)
// TODO: check uses of m_senders 

void
BurstSender::HandleRead(Ptr<Socket> socket) {
  NS_LOG_FUNCTION(this << socket);

  Ptr<Packet> packet;

  while ((packet = socket->Recv())) {
    size_t size = packet->GetSize();

    if (size == sizeof(uint32_t) || size == 1 + sizeof(uint32_t)) {
      bool containsRttProbe = (size == 1 + sizeof(uint32_t));
      uint32_t requestedBytes = ParseRequestedBytes(packet, containsRttProbe);
      NS_LOG_LOGIC(
          m_logPrefix << "Received request for " << requestedBytes << " bytes");

      // Add jitter to the first packet of the response
      Time jitter;

      if (m_responseJitterUs > 0) {
        jitter = MicroSeconds(rand() % m_responseJitterUs);
      }

      Simulator::Schedule(
          jitter, &BurstSender::SendData, this, socket, requestedBytes);
    } else if (size == 1) {
      // This is an RTT probe. Do nothing.
      NS_LOG_LOGIC(m_logPrefix << "Received RTT probe");
    } else {
      // Could be coalesced RTT probes: If multiple RTT probes are lost, they
      // may accumulate before being retransmited.
      NS_LOG_WARN(m_logPrefix << "Strange size received: " << size);
    }
  }
}

void
BurstSender::SendData(Ptr<Socket> socket, uint32_t totalBytes) {
  NS_LOG_FUNCTION(
      this << " socket: " << socket << " totalBytes: " << totalBytes);

  // TODO: Update start time to be when the first packet is actually sent to
  // make graphs meaningful for scheduled RWND strategy.

  size_t sentBytes = 0;

  while (sentBytes < totalBytes && socket->GetTxAvailable()) {
    // TODO: while m_currentburstcount < m_numBursts (add the latter)
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
