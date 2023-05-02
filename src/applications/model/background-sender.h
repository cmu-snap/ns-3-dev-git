#include "incast-sender.h"

namespace ns3 {

/**
 * @brief
 *
 */
class BurstSender : public IncastSender {
 protected:
  /**
   * @brief TODO
   */
  void HandleRead(Ptr<Socket> socket) override;

  /**
   * @brief TODO
   */
  void SendData(Ptr<Socket> socket, uint32_t burstBytes) override;
};
}  // namespace ns3