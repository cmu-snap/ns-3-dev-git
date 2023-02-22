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
  void SetSenders(const std::list<Ipv4Address> &n);

  /**
   * @brief TODO
   */
  void SetRoundFinishCallback(Callback<void> cb);

  /**
   * @brief TODO
   */
  void StartEvent();

  /**
   * @brief TODO
   */
  void StopEvent();

protected:
  /**
   * @brief TODO
   */
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
  void ScheduleBurst(uint32_t burstCount);

  /**
   * @brief TODO
   */
  void StartBurst();

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
  void RoundFinish();

  // Number of bursts to simulate
  uint32_t m_numBursts;

  // TCP port for all applications
  uint16_t m_port;    

  // TypeId of the protocol used
  TypeId m_tid;      

  // Number of closed connections     
  uint32_t m_numClosed; 

  // List of associated sockets
  std::list<Ptr<Socket>> m_sockets;   

  // List of addresses for associated senders             
  std::list<Ipv4Address> m_senders;        

  // List of suspended TCP sockets        
  std::list<Ptr<TcpSocketBase>> m_suspendedSockets; 

  // List of running TCP sockets 
  std::list<Ptr<Socket>> m_runningSockets;

  // // Callback for finished round 
  // Callback<void> m_roundFinish; 

  // bool m_isRunning;   
  // std::list<uint32_t> m_byteCounts;
};

} // namespace ns3

#endif /* INCAST_AGGREGATOR_H */