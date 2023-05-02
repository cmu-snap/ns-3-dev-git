#include "incast-sender.h"

namespace ns3 {

/**
 * @brief
 *
 */
class BurstSender : public IncastSender {
 public:
  /**
   * @brief TODO
   */
  static TypeId GetTypeId();

  /**
   * @brief TODO
   */
  void SetCurrentBurstCount(uint32_t *currentBurstCount);

  /**
   * @brief TODO
   */
  void SetFlowTimesRecord(
      std::vector<std::unordered_map<uint32_t, std::vector<Time>>> *flowTimes);

 protected:
  /**
   * @brief TODO
   */
  void SendData(Ptr<Socket> socket, uint32_t burstBytes) override;

  // Pointer to the global record which burst is currently running.
  uint32_t *m_currentBurstCount;

  // Pointer to the global record of flow start and end times, which is a vector
  // of bursts, where each entry is a maps from sender node ID to (start time,
  // time of first packet, end time).
  std::vector<std::unordered_map<uint32_t, std::vector<Time>>> *m_flowTimes;
};
}  // namespace ns3