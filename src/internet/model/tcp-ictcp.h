#ifndef TCPICTCP_H
#define TCPICTCP_H

#include "ns3/tcp-congestion-ops.h"

namespace ns3 {
 
 /**
 * \ingroup congestionOps
 *
 * \brief An implementation of ICTCP
 *
 * Firstly, it measures the total incoming traffic on the network interface as BWT,
 * compute the available bandwidth as BWA=max(0,αC−BWT), where α∈[0,1] is a parameter to absorb oversubscription.
 * In the paper, α=0.9.
 *
 * Secondly, there is a global time-keeping to divide the time into slots of 2T, and each slot is divided into two subslots of T.
 * The first slot is for measurement of BWA, and second slot is for increasing rwnd.
 * The increase is per-connection, but the total increase across all connections is guided by BWA.
 * Each connection would have its own RTT.
 * The rwnd on each connection can be increased only when now it is in the second subslot and the last increase for this connection is more than 2RTT ago.
 * The size of a subslot is determined by T=∑iwiRTTi where wi is the normalized traffic volume of connection i over all traffic.
 *
 * Then, the window adjustment is as follows:
 * For every RTT on connection i, we sample its current throughput (bytes received over RTT) as bsi.
 * Then a measured throughput is calculated as EWMA:
 * (bmi,new)=max(bsi,βbmi,old+(1−β)bsi).
 *
 * The max is to update the measured throughput as soon as rwnd is increased.
 * Then the expected throughput for this connection is bei=max(bmi,rwndi/RTTi).
 * The max is to ensure bmi≤bei. Now define the throughput difference ratio as dbi=(bei−bmi)/bei, which is between 0 and 1.
 * Then the rwnd is adjusted according to this difference ratio:
 * If dbi≤γ1 or smaller than MSS/rwnd, increase the rwnd if it is in the second subslot and there are enough quota.
 * If dbi>γ2 for three continuous RTT, decrease rwnd by one MSS
 *
 * All other cases, keep the rwnd
 * The paper suggested γ1=0.1 and γ2=0.5 in the experiments.
 * Increase is triggered when the throughput difference is less than one MSS.
 * Decrease is restricted to one MSS per RTT to prevent the sliding window have to shift backward.
 */

  class TcpIctcp : public TcpNewReno
  {
    public:
        /**
   * \brief Get the type ID.
   * \return the object TypeId
   */

    static TypeId GetTypeId (void);
    /**
   * Create an unbound tcp socket.
   */

    TcpIctcp (void);
    /**
   * \brief Copy constructor
   * \param sock the object to copy
   */

    TcpIctcp (const TcpIctcp& sock);
    virtual ~TcpIctcp (void);

    virtual std::string GetName () const;

    virtual void PktsAcked (Ptr<TcpSocketState> tcb, uint32_t segmentsAcked, const Time& rtt);


    /**
   * \brief Enable/disable IcTcp algorithm depending on the congestion state
   *
   * We only start a ICTCP cycle when we are in normal congestion state (CA_OPEN state).
   *
   * \param tcb internal congestion state
   * \param newState new congestion state to which the TCP is going to switch
   */
    virtual void CongestionStateSet (Ptr<TcpSocketState> tcb, const TcpSocketState::TcpCongState_t newState);
    

    /**
   * \brief Adjust cwnd following IcTcp linear increase/decrease algorithm
   *
   * \param tcb internal congestion state
   * \param segmentsAcked count of segments ACKed
   */



    virtual void IncreaseWindow (Ptr<TcpSocketState> tcb, uint32_t segmentsAcked);

    virtual uint32_t GetSsThresh (Ptr<const TcpSocketState> tcb, uint32_t bytesInFlight);

     virtual void RxCongestionAvoidance (Ptr<TcpSocketState> tcb, uint32_t segmentsAcked);
    virtual uint32_t RxSlowStart (Ptr<TcpSocketState> tcb, uint32_t segmentsAcked);

    virtual Ptr<TcpCongestionOps> Fork ();

  protected:
  private:
    /**
   * \brief Enable IcTcp algorithm to start taking IcTcp samples
   *
   * 
   *
   * \param tcb internal congestion state
   */

    void EnableIctcp(Ptr<TcpSocketState> tcb);
    
    /**
   * \brief Stop taking IcTcp samples
   */
    void DisableIctcp();

    // My code, Compute the throughput difference
    double ComputeBandwidth(Ptr<const TcpSocketState> tcb);
    double ComputeThroughputDiff(Ptr<const TcpSocketState> tcb); 
  private:

    // My code
    double m_measure_thru;             //!<Measured throughput
    uint32_t m_count;                  //!<RTT counter, at least three times diff > threshhold, go to case 2.
    //
    Time m_baseRtt;                    //!< Minimum of all RTT measurements seen during connection
    Time m_minRtt;                     //!< Minimum of RTTs measured within last RTT
    uint32_t m_cntRtt;                 //!< Number of RTT measurements during last RTT
    bool m_doingIctcpNow;              //!< If true, do Ictcp for this RTT
    uint32_t m_diff;                   //!< Difference between expected and actual throughput
    bool m_inc;                        //!< If true, cwnd needs to be incremented
    uint32_t m_ackCnt;                 //!< Number of received ACK
    uint32_t m_beta;                   //!< Threshold for congestion detection
    Time m_lastCon;                    //!< Time of the last congestion for the flow
    uint32_t m_dataSent;               //!< Current amount of data sent since last congestion
    double BW;
  };

} // namespace ns3

#endif // TCPICTCP_H
