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

  // Переопределяем основной метод маршрутизации для исходящих пакетов
  virtual Ptr<Ipv4Route> RouteOutput (Ptr<Packet> p, 
                                     const Ipv4Header &header,
                                     Ptr<NetDevice> oif,
                                     Socket::SocketErrno &sockerr) override;

private:
  Ptr<UniformRandomVariable> m_rand; // Генератор случайных чисел
  
  // Вспомогательный метод для получения интерфейсов маршрутов
  std::vector<uint32_t> GetRouteInterfacesTo (Ipv4Address dest);
  
  // Вспомогательный метод для получения шлюза для интерфейса
  Ipv4Address GetGatewayForInterface (uint32_t interface, Ipv4Address dest);
};

} // namespace ns3

#endif /* PER_PACKET_LOAD_BALANCER_H */