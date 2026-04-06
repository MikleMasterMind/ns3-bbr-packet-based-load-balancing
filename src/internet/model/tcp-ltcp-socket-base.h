#include "ns3/tcp-socket-base.h"

namespace ns3 {

class TcpLtcpSocketBase : public TcpSocketBase
{
public:
  static TypeId GetTypeId (void);
  TcpLtcpSocketBase ();
  ~TcpLtcpSocketBase () override;

protected:

  void UpdateRetxThresh ();

  void ReceivedAck (Ptr<Packet> packet,
                    const TcpHeader& tcpHeader) override;
};

}