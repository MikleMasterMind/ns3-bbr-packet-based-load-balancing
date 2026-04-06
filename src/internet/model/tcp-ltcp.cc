#include "tcp-ltcp.h"
#include "ns3/log.h"
#include "ns3/tcp-congestion-ops.h"
#include "ns3/tcp-recovery-ops.h"
#include "ns3/tcp-tx-buffer.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("LazyTcpSocket");
NS_OBJECT_ENSURE_REGISTERED (LazyTcpSocket);

TypeId
LazyTcpSocket::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::LazyTcpSocket")
    .SetParent<TcpSocketBase> ()
    .SetGroupName ("Internet")
    .AddConstructor<LazyTcpSocket> ()
  ;
  return tid;
}

LazyTcpSocket::LazyTcpSocket (void)
  : TcpSocketBase ()
{
  NS_LOG_FUNCTION (this);
  m_sackEnabled = true;  // SACK is required for accurate loss detection
}

LazyTcpSocket::LazyTcpSocket (const LazyTcpSocket& sock)
  : TcpSocketBase (sock)
{
  NS_LOG_FUNCTION (this);
  m_sackEnabled = true;
}

Ptr<TcpSocketBase>
LazyTcpSocket::Fork (void)
{
  return CopyObject<LazyTcpSocket> (this);
}

void
LazyTcpSocket::DupAck (uint32_t currentDelivered)
{
  NS_LOG_FUNCTION (this << currentDelivered);

  // Dynamic threshold: max(3, cwnd_in_packets)
  uint32_t cwndPackets = m_tcb->m_cWnd / m_tcb->m_segmentSize;
  uint32_t dupThresh = std::max (3U, cwndPackets);
  NS_LOG_INFO ("Dynamic dupack threshold = " << dupThresh
              << ", current dupack count = " << m_dupAckCount);

  // Main body copied from TcpSocketBase::DupAck, but using dupThresh instead of m_retxThresh
  if (m_tcb->m_congState == TcpSocketState::CA_LOSS)
  {
    return;
  }

  if (m_tcb->m_congState != TcpSocketState::CA_RECOVERY)
  {
    ++m_dupAckCount;
  }

  if (m_tcb->m_congState == TcpSocketState::CA_OPEN)
  {
    NS_ASSERT_MSG (m_dupAckCount == 1,
                   "From OPEN->DISORDER but with " << m_dupAckCount << " dup ACKs");

    m_congestionControl->CongestionStateSet (m_tcb, TcpSocketState::CA_DISORDER);
    m_tcb->m_congState = TcpSocketState::CA_DISORDER;
    // No CwndEvent here (original code does not have it)
    NS_LOG_DEBUG ("CA_OPEN -> CA_DISORDER");
  }

  if (m_tcb->m_congState == TcpSocketState::CA_RECOVERY)
  {
    if (!m_sackEnabled)
    {
      m_txBuffer->AddRenoSack ();
    }
    if (!m_congestionControl->HasCongControl ())
    {
      m_recoveryOps->DoRecovery (m_tcb, currentDelivered, true);
      NS_LOG_INFO (m_dupAckCount << " Dupack received in fast recovery mode."
                                   "Increase cwnd to "
                                << m_tcb->m_cWnd);
    }
  }
  else if (m_tcb->m_congState == TcpSocketState::CA_DISORDER)
  {
    NS_ASSERT ((m_dupAckCount <= dupThresh) || m_recoverActive);

    if ((m_dupAckCount == dupThresh) &&
        ((m_highRxAckMark >= m_recover) || (!m_recoverActive)))
    {
      EnterRecovery (currentDelivered);
      NS_ASSERT (m_tcb->m_congState == TcpSocketState::CA_RECOVERY);
    }
    else if (m_txBuffer->IsLost (m_highRxAckMark))
    {
      EnterRecovery (currentDelivered);
      NS_ASSERT (m_tcb->m_congState == TcpSocketState::CA_RECOVERY);
    }
    else
    {
      if (!m_sackEnabled && m_limitedTx)
      {
        m_txBuffer->AddRenoSack ();
      }
    }
  }
}

void
LazyTcpSocket::EnterRecovery (uint32_t currentDelivered)
{
  NS_LOG_FUNCTION (this);
  NS_ASSERT (m_tcb->m_congState != TcpSocketState::CA_RECOVERY);

  NS_LOG_DEBUG (TcpSocketState::TcpCongStateName[m_tcb->m_congState] << " -> CA_RECOVERY");

  bool successive = IsSuccessiveLoss ();

  // Mark head as lost if needed
  if (!m_sackEnabled)
  {
    m_txBuffer->AddRenoSack ();
    m_txBuffer->MarkHeadAsLost ();
  }
  else
  {
    if (!m_txBuffer->IsLost (m_txBuffer->HeadSequence ()))
    {
      m_txBuffer->MarkHeadAsLost ();
    }
  }

  // Set recovery point and state
  m_recover = m_tcb->m_highTxMark;
  m_recoverActive = true;

  m_congestionControl->CongestionStateSet (m_tcb, TcpSocketState::CA_RECOVERY);
  m_tcb->m_congState = TcpSocketState::CA_RECOVERY;

  uint32_t bytesInFlight = m_sackEnabled ? BytesInFlight () : BytesInFlight () + m_tcb->m_segmentSize;

  if (successive)
  {
    // Standard halving for successive loss
    m_tcb->m_ssThresh = m_congestionControl->GetSsThresh (m_tcb, bytesInFlight);
    if (!m_congestionControl->HasCongControl ())
    {
      m_recoveryOps->EnterRecovery (m_tcb, m_dupAckCount, UnAckDataCount (), currentDelivered);
    }
    NS_LOG_INFO (m_dupAckCount << " dupack. Enter fast recovery mode (successive loss)."
                               << "Reset cwnd to " << m_tcb->m_cWnd << ", ssthresh to "
                               << m_tcb->m_ssThresh << " at fast recovery seqnum " << m_recover
                               << " calculated in flight: " << bytesInFlight);
  }
  else
  {
    // Discrete loss: keep cwnd and ssthresh unchanged
    uint32_t oldCwnd = m_tcb->m_cWnd;
    uint32_t oldSsThresh = m_tcb->m_ssThresh;
    if (!m_congestionControl->HasCongControl ())
    {
      m_recoveryOps->EnterRecovery (m_tcb, m_dupAckCount, UnAckDataCount (), currentDelivered);
    }
    // Restore original values
    m_tcb->m_cWnd = oldCwnd;
    m_tcb->m_ssThresh = oldSsThresh;
    NS_LOG_INFO (m_dupAckCount << " dupack. Enter fast recovery mode (discrete loss)."
                               << "Maintaining cwnd at " << m_tcb->m_cWnd << ", ssthresh at "
                               << m_tcb->m_ssThresh);
  }

  // Retransmit the lost segment
  uint32_t sz = SendDataPacket (m_highRxAckMark, m_tcb->m_segmentSize, true);
  NS_ASSERT_MSG (sz > 0, "SendDataPacket returned zero, indicating zero bytes were sent");
}

bool
LazyTcpSocket::IsSuccessiveLoss (void)
{
  NS_LOG_FUNCTION (this);
  // The lost packet is the one at m_highRxAckMark
  SequenceNumber32 lostSeq = m_highRxAckMark;
  SequenceNumber32 nextSeq = lostSeq + m_tcb->m_segmentSize;

  if (nextSeq > m_tcb->m_highTxMark)
  {
    NS_LOG_INFO ("No next packet sent, discrete loss");
    return false;
  }

  bool nextLost = m_txBuffer->IsLost (nextSeq);
  NS_LOG_INFO ("Packet " << lostSeq << " is lost, next packet " << nextSeq << " lost: " << nextLost);
  return nextLost;
}

} // namespace ns3