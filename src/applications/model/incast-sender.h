#ifndef INCAST_SENDER_H
#define INCAST_SENDER_H

#include "ns3/application.h"
#include "ns3/ipv4-interface-container.h"
#include "ns3/inet-socket-address.h"

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
  void HandleSend(Ptr<Socket> socket, uint32_t n);  

  /**
   * @brief TODO
   */
  void HandleAccept (Ptr<Socket> socket, const Address& from);

  /**
   * @brief TODO
   */
  void HandleRead (Ptr<Socket> socket);

  // Number of bytes to send for each burst
  uint32_t m_totalBytes;

  // Number of bytes sent 
  uint32_t m_sentBytes;

  // TCP port for all applications
  uint16_t m_port;      

  // TypeId of the protocol used      
  TypeId m_tid;      

  // Address of the associated aggregator
  Ipv4Address m_aggregator;

  // Socket for communication with the aggregator     
  Ptr<Socket> m_socket;  
};

} // namespace ns3

#endif /* INCAST_SENDER_H */