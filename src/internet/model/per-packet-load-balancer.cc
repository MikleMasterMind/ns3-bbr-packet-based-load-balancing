#include "per-packet-load-balancer.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/ipv4-address.h"
#include "ns3/net-device.h"
#include "ns3/node.h"
#include "ns3/ipv4-routing-table-entry.h"
#include "ns3/uinteger.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("PerPacketLoadBalancer");
NS_OBJECT_ENSURE_REGISTERED (PerPacketLoadBalancer);

TypeId
PerPacketLoadBalancer::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::PerPacketLoadBalancer")
    .SetParent<Ipv4StaticRouting> ()
    .SetGroupName ("Internet")
    .AddConstructor<PerPacketLoadBalancer> ()
    .AddAttribute ("RoundRobinIndex", 
                   "Текущий индекс для round robin балансировки",
                   UintegerValue (0),
                   MakeUintegerAccessor (&PerPacketLoadBalancer::m_currentInterfaceIndex),
                   MakeUintegerChecker<uint32_t> ())
  ;
  return tid;
}

PerPacketLoadBalancer::PerPacketLoadBalancer ()
{
  NS_LOG_FUNCTION (this);
  m_currentInterfaceIndex = 0; // Начинаем с первого интерфейса
  m_totalRoutes = 0;
}

PerPacketLoadBalancer::~PerPacketLoadBalancer ()
{
  NS_LOG_FUNCTION (this);
}

std::vector<uint32_t>
PerPacketLoadBalancer::GetRouteInterfacesTo (Ipv4Address dest)
{
  NS_LOG_FUNCTION (this << dest);
  
  std::vector<uint32_t> interfaces;
  
  // Проходим по всем маршрутам в таблице маршрутизации
  for (uint32_t i = 0; i < GetNRoutes (); i++)
  {
    Ipv4RoutingTableEntry route = GetRoute(i);
    
    // Проверяем, подходит ли маршрут для destination адреса
    // Сравниваем либо точное совпадение адреса, либо совпадение по сети
    if (route.GetDest () == dest || 
        route.GetDest ().CombineMask (route.GetDestNetworkMask ()) == 
          dest.CombineMask (route.GetDestNetworkMask ()))
    {
      interfaces.push_back (route.GetInterface ());
      NS_LOG_DEBUG ("Found route to " << dest << " via interface " << route.GetInterface ());
    }
  }
  
  return interfaces;
}

Ipv4Address
PerPacketLoadBalancer::GetGatewayForInterface (uint32_t interface, Ipv4Address dest)
{
  NS_LOG_FUNCTION (this << interface << dest);
  
  // Ищем маршрут для указанного интерфейса и destination адреса
  for (uint32_t i = 0; i < GetNRoutes (); i++)
  {
    Ipv4RoutingTableEntry route = GetRoute(i);
    if (route.GetInterface () == interface && 
        (route.GetDest () == dest || 
         route.GetDest ().CombineMask (route.GetDestNetworkMask ()) == 
           dest.CombineMask (route.GetDestNetworkMask ())))
    {
      return route.GetGateway ();
    }
  }
  
  return Ipv4Address::GetZero ();
}

Ptr<Ipv4Route>
PerPacketLoadBalancer::RouteOutput (Ptr<Packet> p, 
                                   const Ipv4Header &header,
                                   Ptr<NetDevice> oif,
                                   Socket::SocketErrno &sockerr)
{
  NS_LOG_FUNCTION (this << p << header << oif);
  
  Ipv4Address destAddress = header.GetDestination ();
  
  // Получаем все доступные интерфейсы для destination адреса
  std::vector<uint32_t> interfaces = GetRouteInterfacesTo (destAddress);
  
  // Если нет доступных маршрутов, используем стандартную маршрутизацию
  if (interfaces.empty ())
  {
    NS_LOG_WARN ("No routes found for destination: " << destAddress);
    return Ipv4StaticRouting::RouteOutput (p, header, oif, sockerr);
  }
  
  // Обновляем общее количество маршрутов при первом вызове
  if (m_totalRoutes == 0) {
    m_totalRoutes = interfaces.size();
  }
  
  // Round Robin алгоритм: выбираем следующий интерфейс по кругу
  uint32_t selectedInterface = interfaces[m_currentInterfaceIndex];
  
  // Увеличиваем индекс для следующего пакета, зацикливая при достижении конца
  m_currentInterfaceIndex = (m_currentInterfaceIndex + 1) % m_totalRoutes;
  
  NS_LOG_DEBUG ("Round Robin: selected interface " << selectedInterface 
                << " (index " << m_currentInterfaceIndex << " of " << m_totalRoutes << ")");
  
  // Создаем объект маршрута
  Ptr<Ipv4Route> rtentry = Create<Ipv4Route> ();
  
  // Получаем Ipv4 объект для доступа к сетевым интерфейсам
  Ptr<Ipv4> ipv4 = GetObject<Ipv4> ();
  if (!ipv4)
  {
    NS_LOG_ERROR ("No Ipv4 object found");
    sockerr = Socket::ERROR_NOROUTETOHOST;
    return 0;
  }
  
  // Устанавливаем source адрес из выбранного интерфейса
  uint32_t numAddresses = ipv4->GetNAddresses (selectedInterface);
  if (numAddresses > 0)
  {
    Ipv4InterfaceAddress ifAddr = ipv4->GetAddress (selectedInterface, 0);
    rtentry->SetSource (ifAddr.GetLocal ());
  }
  
  // Получаем шлюз для выбранного интерфейса
  Ipv4Address gateway = GetGatewayForInterface (selectedInterface, destAddress);
  rtentry->SetGateway (gateway);
  
  // Устанавливаем destination адрес и выходное устройство
  rtentry->SetDestination (destAddress);
  rtentry->SetOutputDevice (ipv4->GetNetDevice (selectedInterface));
  
  NS_LOG_DEBUG ("Selected route via interface " << selectedInterface 
                 << " for packet to " << destAddress
                 << " via gateway " << gateway
                 << " (Round Robin index: " << m_currentInterfaceIndex << ")");
  
  sockerr = Socket::ERROR_NOTERROR;
  return rtentry;
}

} // namespace ns3