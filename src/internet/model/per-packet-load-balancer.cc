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
  NS_LOG_INFO("=== PER PACKET LOAD BALANCER CONSTRUCTOR ===");
  m_currentInterfaceIndex = 0; // Начинаем с первого интерфейса
  m_totalRoutes = 0;
}

std::string
PerPacketLoadBalancer::GetProtocolName (void) const
{
  return "PerPacketLoadBalancer";
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
    // Для host routes: точное совпадение адреса
    if (route.GetDest () == dest) 
    {
      interfaces.push_back (route.GetInterface ());
      NS_LOG_DEBUG ("Found host route to " << dest << " via interface " << route.GetInterface ());
    }
    // Для network routes: совпадение по сети
    else if (route.GetDest ().CombineMask (route.GetDestNetworkMask ()) == 
             dest.CombineMask (route.GetDestNetworkMask ()))
    {
      interfaces.push_back (route.GetInterface ());
      NS_LOG_DEBUG ("Found network route to " << dest << " via interface " << route.GetInterface ());
    }
  }
  
  NS_LOG_DEBUG("Total interfaces found for " << dest << ": " << interfaces.size());
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
  

  // std::cout << "UNLUCK" << std::endl;
  return Ipv4Address::GetZero ();
}

bool
PerPacketLoadBalancer::RouteInput (Ptr<const Packet> p, 
                                  const Ipv4Header& header,
                                  Ptr<const NetDevice> idev,
                                  const UnicastForwardCallback& ucb,
                                  const MulticastForwardCallback& mcb,
                                  const LocalDeliverCallback& lcb,
                                  const ErrorCallback& ecb)
{
  NS_LOG_FUNCTION (this << p << header << idev);
  NS_LOG_INFO("=== PER PACKET BALANCER RouteInput CALLED ===");
  NS_LOG_INFO("Packet from " << header.GetSource() << " to " << header.GetDestination());
  NS_LOG_INFO("Input device: " << (idev ? idev->GetIfIndex() : -1));\

  // Получаем Ipv4 объект - ДОЛЖНО БЫТЬ ПЕРВЫМ ДЕЛОМ
  Ptr<Ipv4> ipv4 = m_ipv4;
  if (!ipv4)
  {
    NS_LOG_ERROR("No Ipv4 object found in RouteInput - falling back to static routing");
    // Если нет Ipv4 объекта, используем стандартную маршрутизацию
    return Ipv4StaticRouting::RouteInput(p, header, idev, ucb, mcb, lcb, ecb);
  }

  // Проверяем, не является ли пакет предназначенным локально
  if (header.GetDestination().IsLocalhost())
  {
    NS_LOG_DEBUG("Localhost destination, delivering locally");
    lcb(p, header, header.GetProtocol());
    return true;
  }

  // Проверяем, не является ли пакет широковещательным или многоадресным
  if (header.GetDestination().IsBroadcast() || header.GetDestination().IsMulticast())
  {
    NS_LOG_DEBUG("Broadcast/multicast packet, using standard routing");
    return Ipv4StaticRouting::RouteInput(p, header, idev, ucb, mcb, lcb, ecb);
  }

  // Проверяем все интерфейсы на предмет локального адреса назначения
  for (uint32_t i = 0; i < ipv4->GetNInterfaces(); i++)
  {
    for (uint32_t j = 0; j < ipv4->GetNAddresses(i); j++)
    {
      Ipv4InterfaceAddress addr = ipv4->GetAddress(i, j);
      if (addr.GetLocal() == header.GetDestination())
      {
        NS_LOG_DEBUG("Local delivery for " << header.GetDestination());
        lcb(p, header, header.GetProtocol());
        return true;
      }
    }
  }

  // Пакет нужно форвардить - применяем балансировку
  NS_LOG_DEBUG("Packet needs forwarding, applying load balancing");

  Ipv4Address destAddress = header.GetDestination();

  // Получаем все доступные интерфейсы для destination адреса
  std::vector<uint32_t> interfaces = GetRouteInterfacesTo(destAddress);
  
  NS_LOG_DEBUG("Found " << interfaces.size() << " interfaces for " << destAddress);
  
  // Если есть multiple маршруты, используем балансировку
  if (interfaces.size() > 1)
  {
    NS_LOG_INFO("=== BALANCER ACTIVE in RouteInput for " << destAddress << " ===");
    NS_LOG_INFO("Available interfaces: " << interfaces.size());
    
    // Обновляем общее количество маршрутов если нужно
    if (m_totalRoutes == 0 || m_totalRoutes != interfaces.size()) {
      m_totalRoutes = interfaces.size();
      NS_LOG_DEBUG("Updated total routes to: " << m_totalRoutes);
    }
    
    // Round Robin алгоритм
    uint32_t selectedInterface = interfaces[m_currentInterfaceIndex];
    
    // Увеличиваем индекс для следующего пакета
    m_currentInterfaceIndex = (m_currentInterfaceIndex + 1) % m_totalRoutes;
    
    NS_LOG_DEBUG("Round Robin: selected interface " << selectedInterface 
                  << " (index " << (m_currentInterfaceIndex == 0 ? m_totalRoutes - 1 : m_currentInterfaceIndex - 1) 
                  << " of " << m_totalRoutes << ")");
    
    // Создаем объект маршрута
    Ptr<Ipv4Route> rtentry = Create<Ipv4Route>();
    
    // Устанавливаем source адрес из выбранного интерфейса
    uint32_t numAddresses = ipv4->GetNAddresses(selectedInterface);
    if (numAddresses > 0)
    {
      Ipv4InterfaceAddress ifAddr = ipv4->GetAddress(selectedInterface, 0);
      rtentry->SetSource(ifAddr.GetLocal());
      NS_LOG_DEBUG("Set source address: " << ifAddr.GetLocal());
    }
    else
    {
      NS_LOG_WARN("No IP addresses configured on interface " << selectedInterface);
      ecb(p, header, Socket::ERROR_NOROUTETOHOST);
      return false;
    }
    
    // Получаем шлюз для выбранного интерфейса
    Ipv4Address gateway = GetGatewayForInterface(selectedInterface, destAddress);
    rtentry->SetGateway(gateway);
    
    // Устанавливаем destination адрес и выходное устройство
    rtentry->SetDestination(destAddress);
    Ptr<NetDevice> outputDevice = ipv4->GetNetDevice(selectedInterface);
    if (!outputDevice)
    {
      NS_LOG_ERROR("No net device found for interface " << selectedInterface);
      ecb(p, header, Socket::ERROR_NOROUTETOHOST);
      return false;
    }
    rtentry->SetOutputDevice(outputDevice);
    
    NS_LOG_INFO("Selected forward route via interface " << selectedInterface 
                 << " for packet from " << header.GetSource() << " to " << destAddress
                 << " via gateway " << gateway);
    
    // Вызываем callback для форвардинга пакета
    ucb(rtentry, p, header);
    return true;
  }
  else 
  {
    return Ipv4StaticRouting::RouteInput(p, header, idev, ucb, mcb, lcb, ecb);
  }
}


void
PerPacketLoadBalancer::PrintRoutingTable (Ptr<OutputStreamWrapper> stream, Time::Unit unit) const
{
  NS_LOG_FUNCTION (this << stream);
  
  // Сначала вызываем стандартный вывод таблицы маршрутизации
  Ipv4StaticRouting::PrintRoutingTable (stream, unit);
}

}