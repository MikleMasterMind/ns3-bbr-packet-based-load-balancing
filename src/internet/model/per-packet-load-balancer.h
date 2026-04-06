#ifndef PER_PACKET_LOAD_BALANCER_H
#define PER_PACKET_LOAD_BALANCER_H

#include "ns3/ipv4-static-routing.h"
#include "ns3/ipv4-route.h"
#include "ns3/ipv4-header.h"
#include "ns3/packet.h"
#include "ns3/random-variable-stream.h"
#include <vector>

namespace ns3 {

class PerPacketLoadBalancer : public Ipv4StaticRouting
{
public:
  static TypeId GetTypeId (void);
  
  PerPacketLoadBalancer ();
  virtual ~PerPacketLoadBalancer ();
  virtual std::string GetProtocolName (void) const;

  bool RouteInput(Ptr<const Packet> p,
                    const Ipv4Header& header,
                    Ptr<const NetDevice> idev,
                    const UnicastForwardCallback& ucb,
                    const MulticastForwardCallback& mcb,
                    const LocalDeliverCallback& lcb,
                    const ErrorCallback& ecb) override;

  Ptr<Ipv4Route> RouteOutput (Ptr<Packet> p,
                                   const Ipv4Header& header,
                                   Ptr<NetDevice> oif,
                                   Socket::SocketErrno& sockerr) override;


  void PrintRoutingTable (Ptr<OutputStreamWrapper> stream, Time::Unit unit) const;                

  uint32_t m_currentInterfaceIndex; // Текущий индекс для round robin балансировки
  uint32_t m_totalRoutes;          // Общее количество маршрутов для балансировки
  
  // Вспомогательный метод для получения интерфейсов маршрутов
  std::vector<uint32_t> GetRouteInterfacesTo (Ipv4Address dest);
  
  // Вспомогательный метод для получения шлюза для интерфейса
  Ipv4Address GetGatewayForInterface (uint32_t interface, Ipv4Address dest);

};
} // namespace ns3

#endif /* PER_PACKET_LOAD_BALANCER_H */