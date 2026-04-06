#ifndef LAZY_TCP_SOCKET_H
#define LAZY_TCP_SOCKET_H

#include "ns3/tcp-socket-base.h"

namespace ns3 {

/**
 * \ingroup tcp
 *
 * \brief Lazy TCP (LTCP): a TCP variant robust to packet reordering and link degradation.
 *
 * This class implements the Lazy TCP algorithm described in:
 * "Improving datacenter throughput and robustness with Lazy TCP over packet spraying",
 * Jie Zhang et al., Computer Communications 62 (2015) 23–33.
 */
class LazyTcpSocket : public TcpSocketBase
{
public:
  static TypeId GetTypeId (void);

  LazyTcpSocket (void);
  LazyTcpSocket (const LazyTcpSocket& sock);

protected:
  virtual Ptr<TcpSocketBase> Fork (void) override;

  virtual void DupAck (uint32_t currentDelivered) override;
  virtual void EnterRecovery (uint32_t currentDelivered) override;

private:
  /**
   * \brief Check whether the loss detected is successive (consecutive) or discrete.
   * \return true if the packet following the lost one is also lost, false otherwise.
   */
  bool IsSuccessiveLoss (void);
};

} // namespace ns3

#endif // LAZY_TCP_SOCKET_H