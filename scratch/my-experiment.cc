#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/per-packet-load-balancer.h"
#include "ns3/ping-helper.h"
#include <fstream>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("PerPacketLoadBalancerExperiment");

// Глобальные переменные для мониторинга CWND
bool firstCwnd = true;
Ptr<OutputStreamWrapper> cWndStream;
uint32_t cWndValue;

// Глобальные переменные для мониторинга RTT
bool firstRtt = true;
Ptr<OutputStreamWrapper> rttStream;
Time rttValue;

// Функция трассировки CWND
static void
CwndTracer (uint32_t oldval, uint32_t newval)
{
  if (firstCwnd)
    {
      *cWndStream->GetStream () << "0.0 " << oldval << std::endl;
      firstCwnd = false;
    }
  *cWndStream->GetStream () << Simulator::Now ().GetSeconds () << " " << newval << std::endl;
  cWndValue = newval;
  // NS_LOG_INFO("CWND: " << newval << " at time " << Simulator::Now ().GetSeconds ());
}

// Функция для подключения трассировки CWND
static void
TraceCwnd (std::string cwnd_tr_file_name)
{
  AsciiTraceHelper ascii;
  cWndStream = ascii.CreateFileStream (cwnd_tr_file_name.c_str ());
  
  // Записываем заголовок файла
  *cWndStream->GetStream () << "#Time CWND" << std::endl;
  
  // Используем более надежный способ подключения через Config::Connect
  // с отложенным выполнением, чтобы убедиться, что сокет создан
  Config::ConnectWithoutContextFailSafe (
    "/NodeList/0/ApplicationList/0/$ns3::BulkSendApplication/Socket/CongestionWindow",
    MakeCallback (&CwndTracer));
  
  Config::ConnectWithoutContextFailSafe (
    "/NodeList/0/$ns3::TcpL4Protocol/SocketList/0/CongestionWindow",
    MakeCallback (&CwndTracer));
    
  NS_LOG_INFO("Попытка подключения трассировки CWND выполнена");
}

// Функция трассировки RTT
static void
RttTracer (Time oldval, Time newval)
{
  if (firstRtt)
    {
      *rttStream->GetStream () << "0.0 " << oldval.GetSeconds () << std::endl;
      firstRtt = false;
    }
  *rttStream->GetStream () << Simulator::Now ().GetSeconds () << " " << newval.GetSeconds () << std::endl;
  rttValue = newval;
  // NS_LOG_INFO("RTT: " << newval.GetSeconds () << "s at time " << Simulator::Now ().GetSeconds ());
}

// Функция для подключения трассировки RTT
static void
TraceRtt (std::string rtt_tr_file_name)
{
  AsciiTraceHelper ascii;
  rttStream = ascii.CreateFileStream (rtt_tr_file_name.c_str ());
  
  // Записываем заголовок файла
  *rttStream->GetStream () << "#Time RTT(s)" << std::endl;
  
  // Подключаемся к RTT трассировке TCP сокета
  Config::ConnectWithoutContextFailSafe (
    "/NodeList/0/$ns3::TcpL4Protocol/SocketList/0/RTT",
    MakeCallback (&RttTracer));
  
  NS_LOG_INFO("Попытка подключения трассировки RTT выполнена");
}



int main (int argc, char *argv[])
{

  Ptr<OutputStreamWrapper> routingStream = Create<OutputStreamWrapper> (&std::cout);
  // Включаем подробное логирование для отладки
  LogComponentEnable ("PerPacketLoadBalancerExperiment", LOG_LEVEL_ALL);
  LogComponentEnable ("PerPacketLoadBalancer", LOG_LEVEL_INFO);      // Все сообщения
  
  // Дополнительные компоненты для полной диагностики
  //LogComponentEnable ("Ipv4StaticRouting", LOG_LEVEL_DEBUG);
//   LogComponentEnable ("Ipv4L3Protocol", LOG_LEVEL_INFO);
//   LogComponentEnable ("TcpSocketBase", LOG_LEVEL_INFO);


  // ==========================================================================
  // НАСТРОЙКА ПАРАМЕТРОВ ЭКСПЕРИМЕНТА
  // ==========================================================================
  Time simulationTime = Seconds (10);  // Общее время симуляции
  uint32_t numPaths = 4;               // Количество параллельных путей от балансировщика к серверу
  uint32_t badPathIndex = 3;           // Индекс "плохого" пути (нумерация с 0)
  Time goodLinkDelay = MilliSeconds (1);       // Нормальная задержка на хороших путях
  Time badLinkDelay = MilliSeconds (50);       // Большая задержка на плохом пути
  
  // Обработка аргументов командной строки для гибкой настройки эксперимента
  CommandLine cmd;
  cmd.AddValue ("simulationTime", "Время симуляции в секундах", simulationTime);
  cmd.AddValue ("numPaths", "Количество параллельных путей", numPaths);
  cmd.Parse (argc, argv);

  // ==========================================================================
  // СОЗДАНИЕ СЕТЕВЫХ УЗЛОВ
  // ==========================================================================
  // Архитектура сети:
  // [Клиент] → [Балансировщик] → [Маршрутизаторы R1-R4] → [Сервер]
  // Балансировщик распределяет пакеты случайно между всеми путями
  // Один из путей (badPathIndex) имеет худшие характеристики
  
  NS_LOG_INFO ("Создание сетевых узлов...");
  NodeContainer clientNode;           // Узел-отправитель данных
  NodeContainer balancerNode;         // Узел с Per-Packet Load Balancer
  NodeContainer serverNode;           // Узел-получатель данных
  NodeContainer routerNodes;          // Промежуточные маршрутизаторы (по одному на каждый путь)
  
  clientNode.Create (1);
  balancerNode.Create (1);
  serverNode.Create (1);
  routerNodes.Create (numPaths);

  // ==========================================================================
  // НАСТРОЙКА СЕТЕВЫХ СОЕДИНЕНИЙ И СТЕКА TCP/IP
  // ==========================================================================
  PointToPointHelper p2p;             // Хелпер для создания point-to-point соединений
  InternetStackHelper internet;       // Хелпер для установки TCP/IP стека
  
  // Устанавливаем стек интернет-протоколов на всех узлах
  internet.Install (clientNode);
  internet.Install (balancerNode);
  internet.Install (serverNode);
  internet.Install (routerNodes);

  // Контейнеры для сетевых устройств (адаптеров)
  std::vector<NetDeviceContainer> balancerToRouterDevices;  // Балансировщик → Маршрутизаторы
  std::vector<NetDeviceContainer> routerToServerDevices;    // Маршрутизаторы → Сервер
  NetDeviceContainer clientToBalancerDevice;                // Клиент → Балансировщик

  // Создание высокоскоростного соединения Клиент → Балансировщик
  // Это соединение не должно быть бутылочным горлышем
  p2p.SetDeviceAttribute ("DataRate", StringValue ("10Gbps"));
  p2p.SetChannelAttribute ("Delay", StringValue ("1ms"));
  clientToBalancerDevice = p2p.Install (clientNode.Get (0), balancerNode.Get (0));

  // Создание соединений Балансировщик → Маршрутизаторы
  // Здесь создаются multiple пути с разными характеристиками
  p2p.SetDeviceAttribute ("DataRate", StringValue ("1Gbps"));
  for (uint32_t i = 0; i < numPaths; i++)
  {
    // Настройка параметров в зависимости от того, "плохой" это путь или нет
    if (i == badPathIndex) {
      // "Плохой" путь: низкая пропускная способность и большая задержка
      p2p.SetDeviceAttribute ("DataRate", StringValue ("500Mbps"));
      p2p.SetChannelAttribute ("Delay", StringValue ("50ms"));
    } else {
      // "Хорошие" пути: нормальная пропускная способность и малая задержка
      p2p.SetDeviceAttribute ("DataRate", StringValue ("1Gbps"));
      p2p.SetChannelAttribute ("Delay", StringValue ("1ms"));
    }
    
    // Создание соединения между балансировщиком и i-м маршрутизатором
    NetDeviceContainer devices = p2p.Install (balancerNode.Get (0), routerNodes.Get (i));
    balancerToRouterDevices.push_back (devices);
  }

  // Создание соединений Маршрутизаторы → Сервер
  // Все эти соединения одинаковые - разница только в предыдущем сегменте
  p2p.SetDeviceAttribute ("DataRate", StringValue ("1Gbps"));
  p2p.SetChannelAttribute ("Delay", StringValue ("1ms"));
  for (uint32_t i = 0; i < numPaths; i++)
  {
    NetDeviceContainer devices = p2p.Install (routerNodes.Get (i), serverNode.Get (0));
    routerToServerDevices.push_back (devices);
  }

  // ==========================================================================
  // ВКЛЮЧЕНИЕ ТРАССИРОВКИ ПАКЕТОВ
  // ==========================================================================
  NS_LOG_INFO ("Включение трассировки пакетов...");
  // Клиент -> Балансировщик
	p2p.EnablePcap("result/client-balancer", clientToBalancerDevice.Get(0)); // клиент
	p2p.EnablePcap("result/balancer-client", clientToBalancerDevice.Get(1)); // балансировщик
	p2p.EnableAscii("result/client-balancer", clientToBalancerDevice.Get(0)); // клиент
	p2p.EnableAscii("result/balancer-client", clientToBalancerDevice.Get(1)); // балансировщик

	// Балансировщик -> Маршрутизаторы
	for (uint32_t i = 0; i < numPaths; i++) {
			// Балансировщик (интерфейс i+2) -> Маршрутизатор i
			p2p.EnablePcap("result/balancer-router-" + std::to_string(i+2) + "-1", 
										balancerToRouterDevices[i].Get(0)); // балансировщик
			p2p.EnableAscii("result/balancer-router-" + std::to_string(i+2) + "-1", 
										balancerToRouterDevices[i].Get(0)); // балансировщик
			
			p2p.EnablePcap("result/balancer-router-" + std::to_string(i+2) + "-2", 
										balancerToRouterDevices[i].Get(1)); // маршрутизатор i
			p2p.EnableAscii("result/balancer-router-" + std::to_string(i+2) + "-2", 
										balancerToRouterDevices[i].Get(1)); // маршрутизатор i
	}

	// Маршрутизаторы -> Сервер
	for (uint32_t i = 0; i < numPaths; i++) {
			// Маршрутизатор i -> Сервер (интерфейс i+1)
			p2p.EnablePcap("result/router-server-1-" + std::to_string(i+1), 
										routerToServerDevices[i].Get(0)); // маршрутизатор i
			
			p2p.EnablePcap("result/router-server-2-" + std::to_string(i+1), 
										routerToServerDevices[i].Get(1)); // сервер
	}


  // ==========================================================================
  // НАСТРОЙКА IP-АДРЕСАЦИИ
  // ==========================================================================
  NS_LOG_INFO ("Настройка IP-адресации...");
  Ipv4AddressHelper ipv4;  // Хелпер для назначения IP-адресов

  // Назначение адресов для соединения Клиент-Балансировщик
  ipv4.SetBase ("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer clientToBalancerInterface = ipv4.Assign (clientToBalancerDevice);

  // Назначение адресов для соединений Балансировщик-Маршрутизаторы
  // Каждое соединение получает свою маленькую подсеть /24
  std::vector<Ipv4InterfaceContainer> balancerToRouterInterfaces;
  for (uint32_t i = 0; i < numPaths; i++)
  {
    std::ostringstream network;
    network << "10.1." << i + 2 << ".0";  // 10.1.2.0, 10.1.3.0, 10.1.4.0, ...
    ipv4.SetBase (network.str ().c_str (), "255.255.255.0");  // /24 подсеть
    Ipv4InterfaceContainer interfaces = ipv4.Assign (balancerToRouterDevices[i]);
    balancerToRouterInterfaces.push_back (interfaces);
  }

  // Назначение адресов для соединений Маршрутизаторы-Сервер
  std::vector<Ipv4InterfaceContainer> routerToServerInterfaces;
  for (uint32_t i = 0; i < numPaths; i++)
  {
    std::ostringstream network;
    network << "10.1." << i + 6 << ".0";  // 10.1.6.0, 10.1.7.0, 10.1.8.0, ...
    ipv4.SetBase (network.str ().c_str (), "255.255.255.0");
    Ipv4InterfaceContainer interfaces = ipv4.Assign (routerToServerDevices[i]);
    routerToServerInterfaces.push_back (interfaces);
  }

  // ==========================================================================
  // НАЗНАЧЕНИЕ АДРЕСА СЕРВЕРУ
  // ==========================================================================
  NS_LOG_INFO ("Назначение адреса серверу...");
  Ipv4InterfaceContainer serverInterfaces;
  ipv4.SetBase ("10.1.10.0", "255.255.255.0");
  
  // Сервер получает статический адрес 10.1.10.1 на всех интерфейсах
  Ptr<Ipv4> serverIpv4 = serverNode.Get(0)->GetObject<Ipv4>();

  for (uint32_t i = 0; i < numPaths; i++) {
    uint32_t interfaceIndex = serverIpv4->GetInterfaceForDevice(routerToServerDevices[i].Get(1));
    Ipv4InterfaceAddress serverAddress = Ipv4InterfaceAddress(Ipv4Address("10.1.10.1"), Ipv4Mask("255.255.255.0"));
    serverIpv4->AddAddress(interfaceIndex, serverAddress);
    serverIpv4->SetMetric(interfaceIndex, 1);
    serverIpv4->SetUp(interfaceIndex);
  }

  // ==========================================================================
  // НАСТРОЙКА PER-PACKET LOAD BALANCER
  // ==========================================================================
  NS_LOG_INFO ("Настройка Per-Packet Load Balancer...");
  
  // Создание и настройка нашего кастомного балансировщика
  Ptr<PerPacketLoadBalancer> loadBalancer = CreateObject<PerPacketLoadBalancer> ();
  Ptr<Ipv4> balancerIpv4 = balancerNode.Get (0)->GetObject<Ipv4> ();
  
  // Установка балансировщика как основного протокола маршрутизации
  balancerIpv4->SetAttribute("IpForward", BooleanValue(true));
  
  // Используйте ТОЛЬКО балансировщик - без списка!
  balancerIpv4->SetRoutingProtocol(loadBalancer);


  // Добавление multiple маршрутов к одной и той же сети назначения
  // Это ключевой момент: несколько путей к одной сети через разные интерфейсы
  for (uint32_t i = 0; i < numPaths; i++)
  {
    // Шлюзом является адрес маршрутизатора на другом конце соединения
    Ipv4Address gateway = balancerToRouterInterfaces[i].GetAddress (1);
    
    // Добавление маршрута к сети 10.1.10.0/24 через i-й интерфейс
    // Интерфейс i+2 потому что интерфейс 1 занят соединением с клиентом
    loadBalancer->AddNetworkRouteTo (Ipv4Address ("10.1.10.0"), 
                                    Ipv4Mask ("255.255.255.0"), 
                                    gateway, 
                                    i + 2);
    
    NS_LOG_INFO("Добавлен маршрут через интерфейс " << i+2 << " шлюз " << gateway);
  }

	// ДИАГНОСТИКА: проверяем доступность маршрутов
	NS_LOG_INFO("Проверка доступности маршрутов к 10.1.10.1:");
	std::vector<uint32_t> interfaces = loadBalancer->GetRouteInterfacesTo(Ipv4Address("10.1.10.1"));
	NS_LOG_INFO("Найдено интерфейсов для 10.1.10.1: " << interfaces.size());

  // ==========================================================================
  // НАСТРОЙКА СТАТИЧЕСКОЙ МАРШРУТИЗАЦИИ НА МАРШРУТИЗАТОРАХ
  // ==========================================================================
  NS_LOG_INFO ("Настройка статической маршрутизации на маршрутизаторах...");
  for (uint32_t i = 0; i < numPaths; i++)
  {
    Ptr<Ipv4StaticRouting> routerRouting = Ipv4RoutingHelper::GetRouting <Ipv4StaticRouting> (
        routerNodes.Get(i)->GetObject<Ipv4>()->GetRoutingProtocol());
    
    // Получение индекса интерфейса, подключенного к серверу
    uint32_t serverInterfaceIndex = routerNodes.Get(i)->GetObject<Ipv4>()->GetInterfaceForDevice(
        routerToServerDevices[i].Get(0));
    
    // Маршрут от маршрутизатора к серверу - прямое соединение
    routerRouting->AddHostRouteTo(Ipv4Address("10.1.10.1"), 
                                 routerToServerInterfaces[i].GetAddress(1),
                                 serverInterfaceIndex);
    
    // Получение индекса интерфейса, подключенного к балансировщику
    uint32_t balancerInterfaceIndex = routerNodes.Get(i)->GetObject<Ipv4>()->GetInterfaceForDevice(
        balancerToRouterDevices[i].Get(1));
    
    // Маршрут от маршрутизатора к клиенту через балансировщик
    routerRouting->AddHostRouteTo(Ipv4Address("10.1.1.1"),
                                 balancerToRouterInterfaces[i].GetAddress(0),
                                 balancerInterfaceIndex);

    // NS_LOG_INFO ("Таблица маршрутизации роутера:" << i);
    // routerRouting->PrintRoutingTable (routingStream, Time::S);
    }

  // ==========================================================================
  // НАСТРОЙКА СТАТИЧЕСКОЙ МАРШРУТИЗАЦИИ НА СЕРВЕРЕ
  // ==========================================================================
  NS_LOG_INFO ("Настройка статической маршрутизации на сервере...");
  Ptr<Ipv4StaticRouting> serverRouting = Ipv4RoutingHelper::GetRouting <Ipv4StaticRouting> (
      serverNode.Get(0)->GetObject<Ipv4>()->GetRoutingProtocol());
  
  // Добавление маршрутов от сервера к клиенту через все маршрутизаторы
  for (uint32_t i = 0; i < numPaths; i++) {
    uint32_t interfaceIndex = serverIpv4->GetInterfaceForDevice(routerToServerDevices[i].Get(1));
    serverRouting->AddHostRouteTo(Ipv4Address("10.1.1.1"),
                                 routerToServerInterfaces[i].GetAddress(0),
                                 interfaceIndex);
  }

	// ==========================================================================
	// НАСТРОЙКА МАРШРУТИЗАЦИИ НА КЛИЕНТЕ
	// ==========================================================================
	NS_LOG_INFO ("Настройка маршрутизации на клиенте...");

	// Получаем протокол маршрутизации клиента
	Ptr<Ipv4> clientIpv4 = clientNode.Get(0)->GetObject<Ipv4>();
	Ptr<Ipv4StaticRouting> clientRouting = Ipv4RoutingHelper::GetRouting <Ipv4StaticRouting> (
			clientIpv4->GetRoutingProtocol());

	// Добавляем маршрут по умолчанию через балансировщик
	clientRouting->AddHostRouteTo(Ipv4Address("10.1.10.1"),
                             clientToBalancerInterface.GetAddress(1), 1);
	NS_LOG_INFO("Добавлен маршрут по умолчанию через " << clientToBalancerInterface.GetAddress(1));

  // ==========================================================================
  // НАСТРОЙКА ПРИЛОЖЕНИЙ ДЛЯ ГЕНЕРАЦИИ ТРАФИКА
  // ==========================================================================
  NS_LOG_INFO ("Настройка приложений...");
  
  // TCP-сервер (приемник данных) на узле-сервере
  uint16_t serverPort = 5000;
  // Сервер "живет" по адресу 10.1.4.1 - это виртуальный адрес, к которому обращается клиент
  Address serverAddress (InetSocketAddress (Ipv4Address ("10.1.10.1"), serverPort));
  
  // Создание TCP-сервера, который будет принимать входящие соединения
  PacketSinkHelper packetSinkHelper ("ns3::TcpSocketFactory", serverAddress);
  ApplicationContainer serverApp = packetSinkHelper.Install (serverNode.Get (0));
  serverApp.Start (Seconds (0.0));     // Сервер запускается сразу
  serverApp.Stop (simulationTime);     // Работает до конца симуляции

  // TCP-клиент (отправитель данных) на узле-клиенте
  BulkSendHelper bulkSend ("ns3::TcpSocketFactory", serverAddress);
  bulkSend.SetAttribute ("MaxBytes", UintegerValue (0));      // Бесконечная передача
  bulkSend.SetAttribute ("SendSize", UintegerValue (1460));   // Размер TCP-сегмента
  
  ApplicationContainer clientApp = bulkSend.Install (clientNode.Get (0));
  clientApp.Start (Seconds (1.0));     // Клиент начинает через 1 секунду
  clientApp.Stop (simulationTime - Seconds (1));  // Заканчивает за 1 секунду до конца

  // ==========================================================================
  // НАСТРОЙКА TCP CUBIC
  // ==========================================================================
  NS_LOG_INFO ("Настройка TCP Cubic...");
  Config::SetDefault ("ns3::TcpL4Protocol::SocketType", StringValue ("ns3::TcpCubic"));
  Config::SetDefault ("ns3::TcpSocketBase::Sack", BooleanValue (false));
  Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue (1440));
  Config::SetDefault ("ns3::TcpSocket::InitialCwnd", UintegerValue (10));

  // ==========================================================================
  // МОНИТОРИНГ CWND
  // ==========================================================================
  NS_LOG_INFO ("Настройка мониторинга CWND...");

  std::string dir = "result/data/";
  std::string dirToSave = "mkdir -p " + dir;
  system (dirToSave.c_str ());
  
  // Мониторинг CWND для первого клиента - запускаем после установки соединения
  // Используем более позднее время для запуска, чтобы убедиться, что соединение установлено
  Simulator::Schedule (Seconds (1.5), &TraceCwnd, dir + "cwnd.data");
  
  NS_LOG_INFO("Мониторинг CWND активирован");
  
  // Мониторинг RTT
  Simulator::Schedule (Seconds (1.5), &TraceRtt, dir + "rtt.data");
  
  NS_LOG_INFO("Мониторинг RTT активирован");

  // ==========================================================================
  // ВЫВОД ТАБЛИЦ МАРШРУТИЗАЦИИ ДЛЯ ДЕБАГА
  // ==========================================================================


  // ДИАГНОСТИКА МАРШРУТИЗАЦИИ
	NS_LOG_INFO("=== ДИАГНОСТИКА МАРШРУТИЗАЦИИ ===");


	// Безопасная проверка маршрутизации
	NS_LOG_INFO("Маршруты клиента:");
	clientIpv4 = clientNode.Get(0)->GetObject<Ipv4>();
	if (clientIpv4) {
			Ptr<Ipv4RoutingProtocol> clientRouting = clientIpv4->GetRoutingProtocol();
			if (clientRouting) {
					clientRouting->PrintRoutingTable(routingStream, Time::S);
			} else {
					NS_LOG_ERROR("На клиенте нет протокола маршрутизации");
			}
	}

  NS_LOG_INFO ("Таблица маршрутизации балансировщика:");
  loadBalancer->PrintRoutingTable (routingStream, Time::S);

	NS_LOG_INFO("Маршруты сервера:");
	serverIpv4 = serverNode.Get(0)->GetObject<Ipv4>();
	if (serverIpv4) {
			Ptr<Ipv4RoutingProtocol> serverRouting = serverIpv4->GetRoutingProtocol();
			if (serverRouting) {
					serverRouting->PrintRoutingTable(routingStream, Time::S);
			} else {
					NS_LOG_ERROR("На сервере нет протокола маршрутизации");
			}
	}

	// Проверяем маршрутизаторы
	for (uint32_t i = 0; i < numPaths; i++) {
			NS_LOG_INFO("Маршруты маршрутизатора " << i << ":");
			Ptr<Ipv4> routerIpv4 = routerNodes.Get(i)->GetObject<Ipv4>();
			if (routerIpv4) {
					Ptr<Ipv4RoutingProtocol> routerRouting = routerIpv4->GetRoutingProtocol();
					if (routerRouting) {
							routerRouting->PrintRoutingTable(routingStream, Time::S);
					} else {
							NS_LOG_ERROR("На маршрутизаторе " << i << " нет протокола маршрутизации");
					}
			}
	}

	// ==========================================================================
	// ПРОВЕРКА МАРШРУТИЗАЦИИ ПЕРЕД ЗАПУСКОМ
	// ==========================================================================
	NS_LOG_INFO("=== FINAL ROUTING CHECK ===");

	// Проверяем, что балансировщик видит multiple маршруты
	std::vector<uint32_t> finalInterfaces = loadBalancer->GetRouteInterfacesTo(Ipv4Address("10.1.10.1"));
	NS_LOG_INFO("Final route check - interfaces to 10.1.10.1: " << finalInterfaces.size());
	for (uint32_t i = 0; i < finalInterfaces.size(); i++) {
			NS_LOG_INFO("  Interface " << finalInterfaces[i] << 
									" -> Gateway: " << loadBalancer->GetGatewayForInterface(finalInterfaces[i], Ipv4Address("10.1.10.1")));
	}

	// Проверяем, что клиент может достичь сервера
	clientIpv4 = clientNode.Get(0)->GetObject<Ipv4>();
	Ptr<Ipv4Route> testRoute;
	Socket::SocketErrno sockerr;
	Ipv4Header testHeader;
	testHeader.SetDestination(Ipv4Address("10.1.10.1"));
	testHeader.SetSource(Ipv4Address("10.1.1.1"));

	testRoute = clientIpv4->GetRoutingProtocol()->RouteOutput(
			Create<Packet>(), testHeader, nullptr, sockerr);

	if (testRoute) {
			NS_LOG_INFO("Client can route to server: " << testRoute->GetGateway() << 
									" via interface " << testRoute->GetOutputDevice()->GetIfIndex());
	} else {
			NS_LOG_ERROR("Client cannot route to server!");
	}
  

  // ==========================================================================
  // НАСТРОЙКА СИСТЕМЫ МОНИТОРИНГА ДЛЯ СБОРА СТАТИСТИКИ
  // ==========================================================================
  NS_LOG_INFO ("Настройка мониторинга...");
  
  FlowMonitorHelper flowMonitor;
  Ptr<FlowMonitor> monitor = flowMonitor.InstallAll ();  // Мониторинг на всех узлах

  // ==========================================================================
  // ЗАПУСК СИМУЛЯЦИИ
  // ==========================================================================
  NS_LOG_INFO ("Запуск симуляции...");
  Simulator::Stop (simulationTime);  // Установка времени остановки симуляции
  Simulator::Run ();                 // Запуск основного цикла симуляции

  // ==========================================================================
  // АНАЛИЗ РЕЗУЛЬТАТОВ СИМУЛЯЦИИ
  // ==========================================================================
  NS_LOG_INFO ("Анализ результатов...");
  
  // Проверка на наличие потерянных пакетов
  monitor->CheckForLostPackets ();
  
  // Получение классификатора для анализа потоков
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowMonitor.GetClassifier ());
  FlowMonitor::FlowStatsContainer stats = monitor->GetFlowStats ();
  
  // Итерация по всем зафиксированным потокам и вывод статистики
  if (stats.empty()) {
    NS_LOG_INFO("Нет зафиксированных потоков - пакеты не доходят до сервера");
  } else {
    for (auto iter = stats.begin (); iter != stats.end (); ++iter)
    {
      Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow (iter->first);
      
      NS_LOG_INFO ("Поток " << iter->first << " (" << t.sourceAddress << " -> " << t.destinationAddress << ")");
      NS_LOG_INFO ("  Передано байт: " << iter->second.txBytes);
      NS_LOG_INFO ("  Получено байт: " << iter->second.rxBytes);
      
      // Расчет пропускной способности (исключаем первую секунду - время установки соединения)
      if (iter->second.rxBytes > 0) {
        double throughput = iter->second.rxBytes * 8.0 / (simulationTime.GetSeconds () - 1) / 1e6;
        NS_LOG_INFO ("  Пропускная способность: " << throughput << " Mbps");
      }
      
      // Расчет средней задержки доставки пакетов
      if (iter->second.rxPackets > 0) {
        NS_LOG_INFO ("  Средняя задержка: " << iter->second.delaySum / iter->second.rxPackets);
      }
      
      NS_LOG_INFO ("  Потеряно пакетов: " << iter->second.lostPackets);
    }
  }

  Simulator::Destroy ();  // Очистка всех ресурсов симуляции
  
  return 0;
}