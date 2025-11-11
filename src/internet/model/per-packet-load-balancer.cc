#include "per-packet-load-balancer.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/ipv4-address.h"
#include "ns3/net-device.h"
#include "ns3/node.h"
#include "ns3/ipv4-routing-table-entry.h"

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
  ;
  return tid;
}

PerPacketLoadBalancer::PerPacketLoadBalancer ()
{
  NS_LOG_FUNCTION (this);
  m_rand = CreateObject<UniformRandomVariable> ();
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
  
  for (uint32_t i = 0; i < GetNRoutes (); i++)
  {
    Ipv4RoutingTableEntry route = GetRoute(i);
    
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
  
  std::vector<uint32_t> interfaces = GetRouteInterfacesTo (destAddress);
  
  if (interfaces.empty ())
  {
    NS_LOG_WARN ("No routes found for destination: " << destAddress);
    return Ipv4StaticRouting::RouteOutput (p, header, oif, sockerr);
  }
  
  uint32_t selectedInterfaceIndex = m_rand->GetInteger (0, interfaces.size () - 1);
  uint32_t selectedInterface = interfaces[selectedInterfaceIndex];
  
  Ptr<Ipv4Route> rtentry = Create<Ipv4Route> ();
  
  Ptr<Ipv4> ipv4 = GetObject<Ipv4> ();
  if (!ipv4)
  {
    NS_LOG_ERROR ("No Ipv4 object found");
    sockerr = Socket::ERROR_NOROUTETOHOST;
    return 0;
  }
  
  uint32_t numAddresses = ipv4->GetNAddresses (selectedInterface);
  if (numAddresses > 0)
  {
    Ipv4InterfaceAddress ifAddr = ipv4->GetAddress (selectedInterface, 0);
    rtentry->SetSource (ifAddr.GetLocal ());
  }
  
  Ipv4Address gateway = GetGatewayForInterface (selectedInterface, destAddress);
  rtentry->SetGateway (gateway);
  
  rtentry->SetDestination (destAddress);
  rtentry->SetOutputDevice (ipv4->GetNetDevice (selectedInterface));
  
  NS_LOG_DEBUG ("Selected route via interface " << selectedInterface 
                 << " (" << selectedInterfaceIndex + 1 << " of " << interfaces.size() 
                 << ") for packet to " << destAddress
                 << " via gateway " << gateway);
  
  sockerr = Socket::ERROR_NOTERROR;
  return rtentry;
}

} // namespace ns3