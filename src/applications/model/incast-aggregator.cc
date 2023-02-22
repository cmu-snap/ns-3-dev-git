#include "incast-aggregator.h"
#include "ns3/boolean.h"
#include "ns3/internet-module.h"
#include "ns3/log.h"
#include "ns3/tcp-congestion-ops.h"
#include "ns3/uinteger.h"
#include <unistd.h>

NS_LOG_COMPONENT_DEFINE("IncastAggregator");

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED(IncastAggregator);

TypeId IncastAggregator::GetTypeId() {
  static TypeId tid = TypeId("ns3::IncastAggregator")
    .SetParent<Application>()
    .AddConstructor<IncastAggregator>()
    .AddAttribute(
      "NumBursts", 
      "Number of bursts to simulate",
      UintegerValue(10),
      MakeUintegerAccessor(&IncastAggregator::m_numBursts),
      MakeUintegerChecker<uint32_t>()
    )
    .AddAttribute(
      "Port", 
      "TCP port for all applications",
      UintegerValue(5000),
      MakeUintegerAccessor(&IncastAggregator::m_port),
      MakeUintegerChecker<uint16_t>()
    )
    .AddAttribute(
      "Protocol", 
      "TypeId of the protocol used",
      TypeIdValue(TcpSocketFactory::GetTypeId()),
      MakeTypeIdAccessor(&IncastAggregator::m_tid), 
      MakeTypeIdChecker()
    );

  return tid;
}

IncastAggregator::IncastAggregator() {
  NS_LOG_FUNCTION(this);
}

IncastAggregator::~IncastAggregator() { 
  NS_LOG_FUNCTION(this); 
}

void IncastAggregator::StartEvent() {
  NS_LOG_FUNCTION(this);
}

void IncastAggregator::DoDispose() {
  NS_LOG_FUNCTION(this);

  m_sockets.clear();
  Application::DoDispose();
}

void IncastAggregator::SetSenders(const std::list<Ipv4Address> &senders) {
  NS_LOG_FUNCTION(this);
  m_senders = senders;
}

void IncastAggregator::StartApplication() 
{
  NS_LOG_FUNCTION(this);

  // Do nothing if incast is running
  if (m_isRunning) {
    return;
  }

  m_isRunning = true;
  m_numClosed = 0;

  for (uint32_t burstCount = 0; burstCount < m_numBursts; ++burstCount) {
    ScheduleStartEvent(burstCount);
  }
}

void IncastAggregator::ScheduleStartEvent(uint32_t burstCount) {
  NS_LOG_FUNCTION(this);

  Time time = Seconds(burstCount);
  NS_LOG_LOGIC("Start at " << time.As(Time::S));
  Simulator::Schedule(time, &IncastAggregator::StartBurst, this);
} 

void IncastAggregator::StartBurst()
{
  NS_LOG_FUNCTION(this);
  for (Ipv4Address sender: m_senders) {
    Ptr<Socket> socket = Socket::CreateSocket(GetNode(), m_tid);
    
    if (socket->GetSocketType() != Socket::NS3_SOCK_STREAM &&
      socket->GetSocketType() != Socket::NS3_SOCK_SEQPACKET) {
      NS_FATAL_ERROR("Only NS_SOCK_STREAM or NS_SOCK_SEQPACKET sockets are allowed.");
    }

    // Bind, connect, and wait for data
    NS_LOG_LOGIC("Connect to " << sender);
    socket->Bind();
    socket->Connect(InetSocketAddress(sender, m_port));
    socket->ShutdownSend();
    socket->SetRecvCallback(
      MakeCallback(&IncastAggregator::HandleRead, this)
    );
    socket->SetCloseCallbacks(
      MakeCallback(&IncastAggregator::HandleClose, this),
      MakeCallback(&IncastAggregator::HandleClose, this)
    );
    m_sockets.push_back(socket);
  }
}

void IncastAggregator::HandleRead(Ptr<Socket> socket) {
  NS_LOG_FUNCTION(this << socket);

  Ptr<Packet> packet;
  uint32_t byteCount = 0;

  while (packet == socket->Recv()) {
    byteCount += packet->GetSize();
  };

  NS_LOG_LOGIC("received " << byteCount << " bytes");  

  // auto it = std::find(m_runningSockets.begin(), m_runningSockets.end(), socket);
  // std::list<uint32_t>::iterator p = m_byteCounts.begin();

  // if (it == m_runningSockets.end()) {
  //   m_runningSockets.push_back(socket);
  //   p = m_byteCounts.insert(m_byteCounts.end(), byteCount);
  // } else {
  //   std::list<Ptr<Socket>>::iterator q = m_runningSockets.begin();
  //   while (q != it) {
  //     ++q;
  //     ++p;
  //   }
  //   *p += byteCount;
  // }

  // std::list<uint32_t>::iterator minCount;
  // std::list<uint32_t>::iterator maxCount;

  // minCount = std::min_element(m_byteCounts.begin(), m_byteCounts.end());
  // maxCount = std::max_element(m_byteCounts.begin(), m_byteCounts.end());

  // if ((*maxCount - *minCount >= 4096) && (*maxCount - *p <= 2048)) {
  //   socket->SetAttribute("MaxWindowSize", UintegerValue(6000));
  // } else {
  //   socket->SetAttribute("MaxWindowSize", UintegerValue(65535));
  // }
}

void IncastAggregator::HandleClose(Ptr<Socket> socket) {
  NS_LOG_FUNCTION(this << socket);

  ++m_numClosed;
  m_sockets.remove(socket);

  // std::list<uint32_t>::iterator p = m_byteCounts.begin();
  std::list<Ptr<Socket>>::iterator q = m_runningSockets.begin();

  while (*q != socket && q != m_runningSockets.end()) {
    ++q;
    // ++p;
  }

  if (q != m_runningSockets.end()) {
    // m_byteCounts.erase(p);
    m_runningSockets.erase(q);
  }

  if (m_numClosed == m_senders.size()) {
    // Start next round of incast
    m_isRunning = false;
    Simulator::ScheduleNow(&IncastAggregator::RoundFinish, this);
  } else if (!m_suspendedSockets.empty()) {
    // Replace the terminated connection with a suspended one
    Ptr<TcpSocketBase> socket = m_suspendedSockets.front();
    m_suspendedSockets.pop_front();
    // socket->ResumeConnection();
  }
}

void IncastAggregator::RoundFinish() {
  NS_LOG_FUNCTION(this);

  if (!m_roundFinish.IsNull()) {
    m_roundFinish();
  }
}

void IncastAggregator::HandleAccept(Ptr<Socket> socket, const Address &from) {
  NS_LOG_FUNCTION(this << socket << from);

  socket->SetRecvCallback(
    MakeCallback(&IncastAggregator::HandleRead, this)
  );
  socket->SetCloseCallbacks(
    MakeCallback(&IncastAggregator::HandleClose, this),
    MakeCallback(&IncastAggregator::HandleClose, this)
  );
}

void IncastAggregator::StopApplication() 
{
  NS_LOG_FUNCTION(this);

  for (Ptr<Socket> socket: m_sockets) {
    socket->Close();
  }
}

void IncastAggregator::SetRoundFinishCallback(Callback<void> callback) {
  NS_LOG_FUNCTION(this);

  m_roundFinish = callback;
};

} // Namespace ns3