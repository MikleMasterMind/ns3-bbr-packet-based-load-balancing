#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/per-packet-load-balancer.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("PerPacketLoadBalancerExperiment");

int main (int argc, char *argv[])
{
  // Включаем подробное логирование для отладки
  LogComponentEnable ("PerPacketLoadBalancerExperiment", LOG_LEVEL_ALL);

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
  p2p.EnableAsciiAll ("per-packet-balancer");
  p2p.EnablePcapAll ("per-packet-balancer");

  // ==========================================================================
  // НАСТРОЙКА IP-АДРЕСАЦИИ
  // ==========================================================================
  NS_LOG_INFO ("Настройка IP-адресации...");
  Ipv4AddressHelper ipv4;  // Хелпер для назначения IP-адресов

  // Назначение адресов для соединения Клиент-Балансировщик
  ipv4.SetBase ("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer clientToBalancerInterface = ipv4.Assign (clientToBalancerDevice);

  // Назначение адресов для соединений Балансировщик-Маршрутизаторы
  // Каждое соединение получает свою маленькую подсеть /30
  std::vector<Ipv4InterfaceContainer> balancerToRouterInterfaces;
  for (uint32_t i = 0; i < numPaths; i++)
  {
    std::ostringstream network;
    network << "10.1.2." << i * 4;  // 10.1.2.0, 10.1.2.4, 10.1.2.8, ...
    ipv4.SetBase (network.str ().c_str (), "255.255.255.252");  // /30 подсеть (2 usable адреса)
    Ipv4InterfaceContainer interfaces = ipv4.Assign (balancerToRouterDevices[i]);
    balancerToRouterInterfaces.push_back (interfaces);
  }

  // Назначение адресов для соединений Маршрутизаторы-Сервер
  std::vector<Ipv4InterfaceContainer> routerToServerInterfaces;
  for (uint32_t i = 0; i < numPaths; i++)
  {
    std::ostringstream network;
    network << "10.1.3." << i * 4;  // 10.1.3.0, 10.1.3.4, 10.1.3.8, ...
    ipv4.SetBase (network.str ().c_str (), "255.255.255.252");
    Ipv4InterfaceContainer interfaces = ipv4.Assign (routerToServerDevices[i]);
    routerToServerInterfaces.push_back (interfaces);
  }

  // ==========================================================================
  // НАЗНАЧЕНИЕ АДРЕСА СЕРВЕРУ
  // ==========================================================================
  NS_LOG_INFO ("Назначение адреса серверу...");
  Ipv4InterfaceContainer serverInterfaces;
  ipv4.SetBase ("10.1.4.0", "255.255.255.0");
  
  // Сервер получает статический адрес 10.1.4.1 на всех интерфейсах
  Ptr<Ipv4> serverIpv4 = serverNode.Get(0)->GetObject<Ipv4>();
  for (uint32_t i = 0; i < numPaths; i++) {
    uint32_t interfaceIndex = serverIpv4->GetInterfaceForDevice(routerToServerDevices[i].Get(1));
    Ipv4InterfaceAddress serverAddress = Ipv4InterfaceAddress(Ipv4Address("10.1.4.1"), Ipv4Mask("255.255.255.0"));
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
  balancerIpv4->SetRoutingProtocol (loadBalancer);

  // Добавление multiple маршрутов к одной и той же сети назначения
  // Это ключевой момент: несколько путей к одной сети через разные интерфейсы
  for (uint32_t i = 0; i < numPaths; i++)
  {
    // Шлюзом является адрес маршрутизатора на другом конце соединения
    Ipv4Address gateway = balancerToRouterInterfaces[i].GetAddress (1);
    
    // Добавление маршрута к сети 10.1.4.0/24 через i-й интерфейс
    // Интерфейс i+1 потому что интерфейс 0 занят соединением с клиентом
    loadBalancer->AddNetworkRouteTo (Ipv4Address ("10.1.4.0"), 
                                    Ipv4Mask ("255.255.255.0"), 
                                    gateway, 
                                    i + 1);
    
    NS_LOG_INFO("Добавлен маршрут через интерфейс " << i+1 << " шлюз " << gateway);
  }

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
        routerToServerDevices[i].Get(1));
    
    // Маршрут от маршрутизатора к серверу - прямое соединение
    routerRouting->AddHostRouteTo(Ipv4Address("10.1.4.1"), 
                                 routerToServerInterfaces[i].GetAddress(1),
                                 serverInterfaceIndex);
    
    // Получение индекса интерфейса, подключенного к балансировщику
    uint32_t balancerInterfaceIndex = routerNodes.Get(i)->GetObject<Ipv4>()->GetInterfaceForDevice(
        balancerToRouterDevices[i].Get(1));
    
    // Маршрут от маршрутизатора к клиенту через балансировщик
    routerRouting->AddHostRouteTo(Ipv4Address("10.1.1.1"),
                                 balancerToRouterInterfaces[i].GetAddress(0),
                                 balancerInterfaceIndex);
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
  // НАСТРОЙКА ПРИЛОЖЕНИЙ ДЛЯ ГЕНЕРАЦИИ ТРАФИКА
  // ==========================================================================
  NS_LOG_INFO ("Настройка приложений...");
  
  // TCP-сервер (приемник данных) на узле-сервере
  uint16_t serverPort = 5000;
  // Сервер "живет" по адресу 10.1.4.1 - это виртуальный адрес, к которому обращается клиент
  Address serverAddress (InetSocketAddress (Ipv4Address ("10.1.4.1"), serverPort));
  
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
  // НАСТРОЙКА TCP BBR - АЛГОРИТМА УПРАВЛЕНИЯ ПЕРЕГРУЗКОЙ
  // ==========================================================================
  NS_LOG_INFO ("Настройка TCP BBR...");
  
  // Установка BBR (Bottleneck Bandwidth and Round-trip propagation time) как алгоритма перегрузки
  // BBR особенно чувствителен к вариациям RTT, что делает его идеальным для демонстрации проблемы
  Config::SetDefault ("ns3::TcpL4Protocol::SocketType", StringValue ("ns3::TcpBbr"));
  Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue (1460));  // MSS
  Config::SetDefault ("ns3::TcpSocket::InitialCwnd", UintegerValue (10));    // Начальное окно перегрузки

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

  // ==========================================================================
  // ЗАВЕРШЕНИЕ СИМУЛЯЦИИ
  // ==========================================================================
  Simulator::Destroy ();  // Очистка всех ресурсов симуляции
  NS_LOG_INFO ("Симуляция завершена.");
  
  return 0;
}