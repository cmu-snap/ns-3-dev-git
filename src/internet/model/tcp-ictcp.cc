#include "tcp-ictcp.h"
#include "ns3/tcp-socket-base.h"
#include "ns3/log.h"

namespace ns3 {

  NS_LOG_COMPONENT_DEFINE ("TcpIctcp");
  NS_OBJECT_ENSURE_REGISTERED (TcpIctcp); 

  TypeId TcpIctcp::GetTypeId (void) {
    static TypeId tid = TypeId ("ns3::TcpIctcp")
      .SetParent<TcpNewReno> ()
      .AddConstructor<TcpIctcp> ()
      .SetGroupName ("Internet")
      .AddAttribute("Count", "Decrease after three times",
                     UintegerValue(0),
                     MakeUintegerAccessor(&TcpIctcp::m_count),
                     MakeUintegerChecker<uint32_t>())
      .AddAttribute("throughput","measured throughtput",
                     DoubleValue(0),
                     MakeDoubleAccessor(&TcpIctcp::m_measure_thru),
                     MakeDoubleChecker<double>());
    return tid;
  }

  TcpIctcp::TcpIctcp (void)
    : TcpNewReno (),
      m_measure_thru (0),   // newly add
      m_count (0), // newly add
      m_baseRtt (Time::Max ()),
      m_minRtt (Time::Max ()),
      m_cntRtt (0),
      m_doingIctcpNow (true),
      m_lastCon (0),
      m_dataSent (0),
      BW(0)
      //m_diff (0)
      //m_ackCnt (0)
  {
    NS_LOG_FUNCTION (this);
  }

  TcpIctcp::TcpIctcp (const TcpIctcp& sock)
    : TcpNewReno (sock),
      m_measure_thru(sock.m_measure_thru), // newly add
      m_count(sock.m_count),
      m_baseRtt (sock.m_baseRtt),
      m_minRtt (sock.m_minRtt),
      m_cntRtt (sock.m_cntRtt),
      m_doingIctcpNow (true),
      m_lastCon (sock.m_lastCon),
      m_dataSent (sock.m_dataSent),
      BW(sock.BW)
      //m_diff (0)
      //m_ackCnt (sock.m_ackCnt)
  {
    NS_LOG_FUNCTION (this);
  }

  TcpIctcp::~TcpIctcp (void) {
    NS_LOG_FUNCTION (this);
  }

  Ptr<TcpCongestionOps> TcpIctcp::Fork (void) {
    return CopyObject<TcpIctcp> (this);
  }


//Standard functions
  void TcpIctcp::PktsAcked (Ptr<TcpSocketState> tcb, uint32_t segmentsAcked, const Time& rtt) {
    NS_LOG_FUNCTION (this << tcb << segmentsAcked << rtt);

    if (rtt.IsZero ()) {
        return;
      }

      if (tcb->m_congState == TcpSocketState::CA_OPEN)
    {
      m_dataSent += segmentsAcked * tcb->m_segmentSize;
    }


    m_minRtt = std::min (m_minRtt, rtt);
    NS_LOG_DEBUG ("Updated m_minRtt= " << m_minRtt);


    m_baseRtt = std::min (m_baseRtt, rtt);
    NS_LOG_DEBUG ("Updated m_baseRtt= " << m_baseRtt);

    // Update RTT counter
    m_cntRtt++;
    NS_LOG_DEBUG ("Updated m_cntRtt= " << m_cntRtt);
  }

  void TcpIctcp::EnableIctcp (Ptr<TcpSocketState> tcb) {
    NS_LOG_FUNCTION (this << tcb);
    m_doingIctcpNow = true;
    m_cntRtt = 0;
    m_minRtt = Time::Max ();
  }

  void TcpIctcp::DisableIctcp () {
    NS_LOG_FUNCTION (this);

    m_doingIctcpNow = false;
  }

  void TcpIctcp::CongestionStateSet (Ptr<TcpSocketState> tcb, const TcpSocketState::TcpCongState_t newState) {
    NS_LOG_FUNCTION (this << tcb << newState);
    if (newState == TcpSocketState::CA_OPEN) {
        EnableIctcp (tcb);
        NS_LOG_LOGIC ("TcpIctcp is now on.");
      }
    else {
        DisableIctcp ();
        NS_LOG_LOGIC ("TcpIctcp is turned off.");
      }
  }

//



  // My Code
  /////////////////////////////////////////////////////////////////////////////////////////
  std::string TcpIctcp::GetName () const {
    return "TcpIctcp";
  }

  double TcpIctcp::ComputeBandwidth (Ptr<const TcpSocketState> tcb) {
    // Declare
    double alpha= 0.9;             //a value
    double capacity_link = 100.0;   //capacity link taking it as 1gbps
    //double BW = 0.0;       // expected throughput
    double new_BW= 0.0;        // throughput difference
    //uint32_t curr_cwnd = 0;        // current window

    new_BW = std::max (0.0, (double)(alpha*capacity_link- BW));
    if (new_BW!=0)
           BW=new_BW;
    return BW;
  }

  double TcpIctcp::ComputeThroughputDiff (Ptr<const TcpSocketState> tcb) {
    // Declare
    double beta = 0.75;             // exponential factor (according to paper)
    double curr_thru = 0.0;        // current throughput
    double expec_thru = 0.0;       // expected throughput
    double thru_diff = 0.0;        // throughput difference
    //uint32_t curr_cwnd = 0;        // current window

    // Compute the throughput difference based on paper
    // Compute the current cwnd
    //curr_cwnd = tcb->GetCwndInSegments();
    //curr_thru = m_baseRtt.GetSeconds()*curr_cwnd;
    //m_throughput
    curr_thru= static_cast<uint32_t> (m_dataSent/ (Simulator::Now ().GetSeconds () - m_lastCon.GetSeconds ()));

    // Get meausred throughput
    m_measure_thru = std::max (curr_thru, beta*m_measure_thru + (1 - beta)*curr_thru);
    // Get expected throughput
    expec_thru = std::max (m_measure_thru, (double)(tcb->m_rWnd.Get()/m_baseRtt.GetSeconds()));
    // Compute throughput difference
    thru_diff = (expec_thru - m_measure_thru)/expec_thru;

    return thru_diff;
  }

  void TcpIctcp::IncreaseWindow (Ptr<TcpSocketState> tcb, uint32_t segmentsAcked) {
    NS_LOG_FUNCTION (this << tcb << segmentsAcked); 

    // Get throughput difference
    double thru_diff = ComputeThroughputDiff(tcb);
    double threshhold1 = 0.1;

    // Case 1: Increase the receive window, there is enough quota
    if (thru_diff <= threshhold1 || thru_diff <= tcb->m_segmentSize / tcb->m_rWnd) { 
      if (tcb->m_rWnd < tcb->m_ssThresh) {
      // Slow start mode. ICTCP employs same slow start algorithm as NewReno's.
        NS_LOG_LOGIC ("We are in slow start, behave like NewReno.");
        segmentsAcked = RxSlowStart (tcb, segmentsAcked);
      }
      // Otherwise: Keep the window
      else { 
      // Congestion avoidance mode
        NS_LOG_LOGIC ("We are in congestion avoidance, execute ICTCP additive "
                      "increase algo.");
        
        RxCongestionAvoidance (tcb, segmentsAcked);
      }
      // if thru_diff
      NS_LOG_LOGIC ("Stage1");
      TcpNewReno::IncreaseWindow (tcb, segmentsAcked);
    }
    // Reset cntRtt & minRtt every RTT
    m_cntRtt = 0;
    m_minRtt = Time::Max ();
  }

  uint32_t TcpIctcp::GetSsThresh (Ptr<const TcpSocketState> tcb, uint32_t bytesInFlight) {
    
    NS_LOG_FUNCTION (this << tcb << bytesInFlight);
      double threshhold2 = 0.5;
      double thru_diff = ComputeThroughputDiff(tcb);
        m_lastCon = Simulator::Now ();
      m_dataSent = 0;

      // Case 2: Decrease the receive window, thru_diff > threshhold2 when 3 continued RTT
      if (thru_diff > threshhold2 && m_count >= 2) {
        m_count=0;
        return tcb->m_rWnd.Get() - tcb->m_segmentSize;
      }
      // Otherwise: Keep the window 
      else if (thru_diff > threshhold2) { 
        m_count++;
        return tcb->m_rWnd.Get();
      }
      // Otherwise: Keep the window 
      else {
        return tcb->m_rWnd.Get();
      }
  }

  void TcpIctcp::RxCongestionAvoidance (Ptr<TcpSocketState> tcb, uint32_t segmentsAcked) {
    NS_LOG_FUNCTION (this << tcb << segmentsAcked);

    if (segmentsAcked > 0) {
        double adder = static_cast<double> (tcb->m_segmentSize * tcb->m_segmentSize) / tcb->m_rWnd.Get ();
        adder = std::max (1.0, adder);
        tcb->m_rWnd += static_cast<uint32_t> (adder);
        NS_LOG_INFO ("In RxCongAvoid, updated to rwnd " << tcb->m_rWnd <<
                     " ssthresh " << tcb->m_ssThresh);
      }
  }

  uint32_t TcpIctcp::RxSlowStart (Ptr<TcpSocketState> tcb, uint32_t segmentsAcked) {
    NS_LOG_FUNCTION (this << tcb << segmentsAcked);

    if (segmentsAcked >= 1) {
        tcb->m_rWnd += tcb->m_segmentSize;
        NS_LOG_INFO ("In RxSlowStart, updated to rwnd " << tcb->m_rWnd << " ssthresh " << tcb->m_ssThresh);
        return segmentsAcked - 1;
      }

    return 0;
  }

} // namespace ns3
