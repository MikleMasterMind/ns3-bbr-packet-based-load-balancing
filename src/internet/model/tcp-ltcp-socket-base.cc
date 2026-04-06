#include "tcp-ltcp-socket-base.h"
#include "ns3/log.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("TcpLtcpSocketBase");
NS_OBJECT_ENSURE_REGISTERED (TcpLtcpSocketBase);

TypeId
TcpLtcpSocketBase::GetTypeId (void)
{
  static TypeId tid =
    TypeId ("ns3::TcpLtcpSocketBase")
      .SetParent<TcpSocketBase>()
      .SetGroupName ("Internet")
      .AddConstructor<TcpLtcpSocketBase>();
  return tid;
}

TcpLtcpSocketBase::TcpLtcpSocketBase ()
{
}

TcpLtcpSocketBase::~TcpLtcpSocketBase ()
{
}

void
TcpLtcpSocketBase::UpdateRetxThresh ()
{
  uint32_t cwndSeg = m_tcb->m_cWnd / m_tcb->m_segmentSize;

  if (cwndSeg < 3)
    {
      cwndSeg = 3;
    }

  m_retxThresh = cwndSeg;
}

void
TcpLtcpSocketBase::ReceivedAck (Ptr<Packet> packet,
                                const TcpHeader& tcpHeader)
{
  UpdateRetxThresh();

  TcpSocketBase::ReceivedAck(packet, tcpHeader);
}

}