#include "incast-sender.h"
#include "ns3/boolean.h"
#include "ns3/internet-module.h"
#include "ns3/log.h"
#include "ns3/tcp-congestion-ops.h"
#include "ns3/uinteger.h"

NS_LOG_COMPONENT_DEFINE("IncastSender");

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED(IncastSender);

TypeId IncastSender::GetTypeId() {
  static TypeId tid = TypeId("ns3::IncastSender")
      .SetParent<Application>()
      .AddConstructor<IncastSender>()
      // .AddAttribute(
      //   "NumBursts", 
      //   "Number of bursts to simulate",
      //   UintegerValue(10),
      //   MakeUintegerAccessor(&IncastSender::m_numBursts),
      //   MakeUintegerChecker<uint32_t>()
      // )
      .AddAttribute(
        "TotalBytes", 
        "Number of bytes to send for each burst",
        UintegerValue(1000),
        MakeUintegerAccessor(&IncastSender::m_totalBytes),
        MakeUintegerChecker<uint32_t>()
      )
      .AddAttribute(
        "Port", 
        "TCP port for all applications",
        UintegerValue(5000),
        MakeUintegerAccessor(&IncastSender::m_port),
        MakeUintegerChecker<uint16_t>()
      )
      .AddAttribute(
        "Protocol", 
        "TypeId of the protocol used",
        TypeIdValue(TcpSocketFactory::GetTypeId()),
        MakeTypeIdAccessor(&IncastSender::m_tid), 
        MakeTypeIdChecker()
      )
      .AddAttribute(
        "Aggregator", 
        "Aggregator to send packets to",
        Ipv4AddressValue(),
        MakeIpv4AddressAccessor(&IncastSender::m_aggregator),
        MakeIpv4AddressChecker()
      );

  return tid;
}

IncastSender::IncastSender() : m_socket(nullptr) { 
  NS_LOG_FUNCTION(this); 
}

IncastSender::~IncastSender() { 
  NS_LOG_FUNCTION(this); 
}

void IncastSender::DoDispose() {
  NS_LOG_FUNCTION(this);

  m_socket = nullptr;
  Application::DoDispose();
}

void IncastSender::StartApplication() 
{
  NS_LOG_FUNCTION(this);

  // m_sentBytes = 0;
  m_socket = Socket::CreateSocket(GetNode(), m_tid);
  m_socket->Bind(InetSocketAddress(Ipv4Address::GetAny(), m_port));
  m_socket->Listen();
  m_socket->Send(Create<Packet>(42));
  // m_socket->ShutdownRecv();
  m_socket->SetRecvCallback(
    MakeCallback(&IncastSender::HandleRead, this)
  );
  // m_socket->SetAcceptCallback(
  //   MakeNullCallback<bool, Ptr<Socket>, const Address &>(),
  //   MakeCallback(&IncastSender::HandleAccept, this)
  // );
}

void IncastSender::HandleRead(Ptr<Socket> socket) {
  NS_LOG_FUNCTION(this << socket);

  Ptr<Packet> packet;

  std::cout << "Just about to receive from sender\n";

  while (packet == socket->Recv()) {
    std::cout << "Received!!!\n";
    size_t size = packet->GetSize();

    if (size < 100) {
      size_t sentBytes = 0; 

      while (sentBytes < m_totalBytes && socket->GetTxAvailable()) { 
        Ptr<Packet> packet = Create<Packet>(m_totalBytes - sentBytes);
        int sentBytes = m_socket->Send(packet);

        if (sentBytes > 0) {
          sentBytes += sentBytes;
        } 

        NS_LOG_LOGIC("Sent " << sentBytes << " bytes");
      }
    } else {
      break;
    }
  }

  StopApplication();
}

// void IncastSender::HandleSend(Ptr<Socket> socket, uint32_t n) {
//   NS_LOG_FUNCTION(this);

//   while (m_sentBytes < m_totalBytes && socket->GetTxAvailable()) { 
//     Ptr<Packet> packet = Create<Packet>(std::min(m_totalBytes - m_sentBytes, n));
//     int sentBytes = socket->Send(packet);

//     if (sentBytes > 0) {
//       m_sentBytes += sentBytes;
//     } 

//     NS_LOG_LOGIC("Sent " << sentBytes << " bytes");
//   }

//   if (m_sentBytes == m_totalBytes) {
//     NS_LOG_LOGIC("Closing socket");
//     socket->Close();
//   }
// }

// void IncastSender::HandleAccept(Ptr<Socket> socket, const Address &from) {
//   NS_LOG_FUNCTION(this << socket << from);

//   InetSocketAddress addr = InetSocketAddress::ConvertFrom(from);
//   NS_LOG_LOGIC("Accepting connection from " << addr.GetIpv4() << ":" << addr.GetPort());

//   // Stop listening to sockets to prevent parallel connections
//   // m_socket->Close();
//   m_socket = socket;
//   socket->SetSendCallback(MakeCallback(&IncastSender::HandleSend, this));
// }

void IncastSender::StopApplication() 
{
  NS_LOG_FUNCTION(this);

  if (m_socket) {
    m_socket->Close();
  }
}

} // Namespace ns3