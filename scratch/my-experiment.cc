  #include "ns3/core-module.h"
  #include "ns3/network-module.h"
  #include "ns3/internet-module.h"
  #include "ns3/point-to-point-module.h"
  #include "ns3/applications-module.h"
  #include "ns3/flow-monitor-module.h"
  #include "ns3/per-packet-load-balancer.h"
  #include "ns3/ping-helper.h"
  #include "ns3/tcp-socket-base.h"
  #include "ns3/config.h"
  #include "ns3/user-constants.h"


  #include <algorithm>
  #include <fstream>
  #include <map>
  #include <sstream>
  #include <vector>

  using namespace ns3;

  NS_LOG_COMPONENT_DEFINE ("PerPacketLoadBalancerExperiment");



// =======================
// CWND / RTT tracing
// =======================
static Ptr<OutputStreamWrapper> g_cWndStream;
static bool g_firstCwnd = true;

static void
CwndTracer(std::string context, uint32_t oldval, uint32_t newval)
{
    if (g_firstCwnd)
    {
        *g_cWndStream->GetStream() << "#Time(s) Cwnd(bytes) Context\n";
        g_firstCwnd = false;
    }
    *g_cWndStream->GetStream() << Simulator::Now().GetSeconds() << " " << newval << " " << context
                               << "\n";
}

static Ptr<OutputStreamWrapper> g_rttStream;
static bool g_firstRtt = true;

static void
RttTracer(std::string context, Time oldval, Time newval)
{
    if (g_firstRtt)
    {
        *g_rttStream->GetStream() << "#Time(s) Rtt(s) Context\n";
        g_firstRtt = false;
    }
    *g_rttStream->GetStream() << Simulator::Now().GetSeconds() << " " << newval.GetSeconds() << " "
                              << context << "\n";
}

// =======================
// Throughput tracing (sink)
// =======================
static Ptr<OutputStreamWrapper> g_throughputStream;
static uint64_t g_lastTotalRx = 0;
static Time g_lastTime = Seconds(0);
static Ptr<PacketSink> g_sink = 0;

static void
ThroughputSampler()
{
    Time now = Simulator::Now();
    uint64_t totalRx = g_sink->GetTotalRx();

    double interval = (now - g_lastTime).GetSeconds();
    if (interval > 0)
    {
        uint64_t deltaBytes = totalRx - g_lastTotalRx;
        double mbps = (deltaBytes * 8.0) / interval / 1e6;

        *g_throughputStream->GetStream() << now.GetSeconds() << " " << mbps << "\n";

        g_lastTotalRx = totalRx;
        g_lastTime = now;
    }

    Simulator::Schedule(MilliSeconds(100), &ThroughputSampler);
}

  // =======================
  // Helper: add service IP to loopback
  // =======================
  static void
  AddServiceIpToLoopback(Ptr<Node> node, Ipv4Address serviceIp)
  {
      Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();

      uint32_t loopIf = UINT32_MAX;
      for (uint32_t i = 0; i < node->GetNDevices(); ++i)
      {
          Ptr<NetDevice> nd = node->GetDevice(i);
          if (DynamicCast<LoopbackNetDevice>(nd))
          {
              loopIf = ipv4->GetInterfaceForDevice(nd);
              break;
          }
      }

      if (loopIf == UINT32_MAX)
      {
          NS_FATAL_ERROR("Loopback interface not found");
      }

      ipv4->AddAddress(loopIf, Ipv4InterfaceAddress(serviceIp, Ipv4Mask("/32")));
      ipv4->SetUp(loopIf);
  }



  int main (int argc, char *argv[])
  {

    Ptr<OutputStreamWrapper> routingStream = Create<OutputStreamWrapper> (&std::cout);
    LogComponentEnable ("PerPacketLoadBalancerExperiment", LOG_LEVEL_INFO);


    // ==========================================================================
    // НАСТРОЙКА ПАРАМЕТРОВ ЭКСПЕРИМЕНТА
    // ==========================================================================
    Time simulationTime = Seconds (10);
    uint32_t numPaths = 4;            
    uint32_t numBadPaths = 1;    
    Time goodLinkDelay = MilliSeconds (1);   
    Time badLinkDelay = MilliSeconds (10);   

    bool lossEnabled = false;
    double lossRate = 0.0;
    
    bool ackFeatureExperiment = false;
    #ifdef TCP_SOCKET_BASE_USE_NEW_DUPACK_LOGIC
    ackFeatureExperiment = true;
    #endif

    bool lossClassification = false;
    #ifdef NS3_IDFEF_SACK_LOSS_CLASSIFICATION
    lossClassification = true;
    #endif


    std::cout << lossClassification << std::endl;
    
    CommandLine cmd;
    cmd.AddValue("simulationTime", "Simulation time in seconds", simulationTime);
    cmd.AddValue("numPaths", "Number of parallel paths", numPaths);
    cmd.AddValue("numBadPaths", "Number of bad paths starting from path 1", numBadPaths);
    cmd.AddValue("lossRate", "Packet loss rate (0.0 - 1.0) for balancer->router links", lossRate);
    cmd.Parse(argc, argv);

    if ((lossRate > 0.0) && (lossRate < 1.0)) {
      lossEnabled = true;
    }

    numBadPaths = std::min(numBadPaths, numPaths);

    // Directories
    std::vector<std::string> resultPathParts = {"result/data"};
    // Добавляем информацию о потерях в путь
    if (lossEnabled)
    {
      std::ostringstream lossStr;
      lossStr << "loss-" << lossRate;
      resultPathParts.push_back(lossStr.str());
    }
    else
    {
      resultPathParts.push_back(std::string("wihtout_loss"));
    }
    if (lossClassification) {
      resultPathParts.push_back("lossClassification");
    }
    else if (ackFeatureExperiment)
    {
        resultPathParts.push_back("dupackfeachure");
    }
    else 
    {
        resultPathParts.push_back("base");
    }
    resultPathParts.push_back(std::to_string(numPaths) + "-numPaths");
    resultPathParts.push_back(std::to_string(numBadPaths) + "-numBadPaths");
    resultPathParts.push_back(std::string(GOOD_DATA_RATE) + "-goodDataRate" + std::string(GOOD_DELAY) + "-goodDelay");
    resultPathParts.push_back(std::string(BAD_DATA_RATE) + "-badDataRate" + std::string(BAD_DELAY) + "-badDelay");
    std::ostringstream dirBuilder;
    for (const auto& part : resultPathParts)
    {
        dirBuilder << part << "/";
    }
    std::string dir = dirBuilder.str();
    system(("mkdir -p " + dir).c_str());

    // ==========================================================================
    // НАСТРОЙКА TCP CUBIC
    // ==========================================================================
    Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue ("ns3::TcpCubic"));
    Config::SetDefault("ns3::TcpSocketBase::Sack", BooleanValue(true));

    Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue (1440));
    Config::SetDefault ("ns3::TcpSocket::InitialCwnd", UintegerValue (10));
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(1 << 20)); // 1 МБ
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(1 << 20));

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

    // --- Настройка модели потерь (если включена) ---
    Ptr<RateErrorModel> em = nullptr;
    if (lossEnabled && lossRate > 0.0)
    {
        em = CreateObject<RateErrorModel>();
        em->SetAttribute("ErrorRate", DoubleValue(lossRate));
        em->SetAttribute("ErrorUnit", StringValue("ERROR_UNIT_PACKET"));
        NS_LOG_INFO("Создана модель потерь с вероятностью " << lossRate);
    }

    // Создание высокоскоростного соединения Клиент → Балансировщик
    p2p.SetDeviceAttribute ("DataRate", StringValue ("10Gbps"));
    p2p.SetChannelAttribute ("Delay", StringValue ("1ms"));
    clientToBalancerDevice = p2p.Install (clientNode.Get (0), balancerNode.Get (0));

    // Создание соединений Балансировщик → Маршрутизаторы
    for (uint32_t i = 0; i < numPaths; i++)
    {
      if (i < numBadPaths) {
        p2p.SetDeviceAttribute ("DataRate", StringValue ("1Gbps"));
        p2p.SetChannelAttribute ("Delay", StringValue ("3ms"));
      } else {
        p2p.SetDeviceAttribute ("DataRate", StringValue ("1Gbps"));
        p2p.SetChannelAttribute ("Delay", StringValue ("1ms"));
      }

      // --- Назначение модели потерь на устройства ---
      if (em)
      {
          p2p.SetDeviceAttribute ("ReceiveErrorModel", PointerValue(em));
      }
        
      
      NetDeviceContainer devices = p2p.Install (balancerNode.Get (0), routerNodes.Get (i));
      balancerToRouterDevices.push_back (devices);

      // Сбрасываем ReceiveErrorModel, чтобы не применять к следующим линкам
      if (em)
      {
          p2p.SetDeviceAttribute ("ReceiveErrorModel", PointerValue(CreateObject<RateErrorModel>()));
      }
    }

    // Создание соединений Маршрутизаторы → Сервер
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

    // Балансировщик -> Маршрутизаторы
    for (uint32_t i = 0; i < numPaths; i++) {
        // Балансировщик (интерфейс i+2) -> Маршрутизатор i
        p2p.EnablePcap("result/balancer-router-" + std::to_string(i+2) + "-1", 
                      balancerToRouterDevices[i].Get(0)); // балансировщик
        
        p2p.EnablePcap("result/balancer-router-" + std::to_string(i+2) + "-2", 
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

    // ==========================
    // IP addressing
    // ==========================
    Ipv4AddressHelper ipv4;


    // Reserve a dedicated /24 for each point-to-point link inside 10.0.0.0/8.
    // This gives enough unique subnets for 1 client-balancer link + 2*numPaths path links.
    auto makeSubnetBase = [](uint32_t subnetIndex) {
        NS_ABORT_MSG_IF(subnetIndex >= (1u << 16), "Subnet index exceeds 10.0.0.0/8 plan capacity");

        std::ostringstream network;
        network << "10." << ((subnetIndex / 256) % 256) << "." << (subnetIndex % 256) << ".0";
        return network.str();
    };

    uint32_t subnetIndex = 0;

    // Client-Balancer: 10.0.0.0/24
    ipv4.SetBase(makeSubnetBase(subnetIndex++).c_str(), "255.255.255.0");
    Ipv4InterfaceContainer clientToBalancerInterface = ipv4.Assign(clientToBalancerDevice);

    // Balancer-Router i: unique /24 per path
    std::vector<Ipv4InterfaceContainer> balancerToRouterInterfaces;
    for (uint32_t i = 0; i < numPaths; i++)
    {
        ipv4.SetBase(makeSubnetBase(subnetIndex++).c_str(), "255.255.255.0");
        balancerToRouterInterfaces.push_back(ipv4.Assign(balancerToRouterDevices[i]));
    }

    // Router i - Server: another unique /24 per path
    std::vector<Ipv4InterfaceContainer> routerToServerInterfaces;
    for (uint32_t i = 0; i < numPaths; i++)
    {
        ipv4.SetBase(makeSubnetBase(subnetIndex++).c_str(), "255.255.255.0");
        routerToServerInterfaces.push_back(ipv4.Assign(routerToServerDevices[i]));
    }

    // Add service IP on server loopback from a dedicated subnet outside link ranges
    Ipv4Address serviceIp("10.255.255.1");
    AddServiceIpToLoopback(serverNode.Get(0), serviceIp);
    
    
    // Сервер получает статический адрес 10.1.10.1 на всех интерфейсах
    Ptr<Ipv4> serverIpv4 = serverNode.Get(0)->GetObject<Ipv4>();

    for (uint32_t i = 0; i < numPaths; i++) {
      uint32_t interfaceIndex = serverIpv4->GetInterfaceForDevice(routerToServerDevices[i].Get(1));
      Ipv4InterfaceAddress serverAddress = Ipv4InterfaceAddress(Ipv4Address("10.1.10.1"), Ipv4Mask("255.255.255.0"));
      serverIpv4->AddAddress(interfaceIndex, serverAddress);
      serverIpv4->SetMetric(interfaceIndex, 1);
      serverIpv4->SetUp(interfaceIndex);
    }

    // ==========================
    // Routing
    // ==========================

    // 1) Balancer uses PerPacketLoadBalancer for FORWARDED traffic (RouteInput)
    Ptr<PerPacketLoadBalancer> balancerLb = CreateObject<PerPacketLoadBalancer>();
    Ptr<Ipv4> balancerIpv4 = balancerNode.Get(0)->GetObject<Ipv4>();
    balancerIpv4->SetAttribute("IpForward", BooleanValue(true));
    balancerIpv4->SetRoutingProtocol(balancerLb);

    // Add multiple host routes to service IP through different routers/interfaces
    for (uint32_t i = 0; i < numPaths; i++)
    {
        Ipv4Address routerGw = balancerToRouterInterfaces[i].GetAddress(1); // router side
        uint32_t outIf = i + 2; // (0 loopback, 1 client link, then path links)

        balancerLb->AddHostRouteTo(serviceIp, routerGw, outIf);
        NS_LOG_INFO("Balancer route: " << serviceIp << " via if=" << outIf << " gw=" << routerGw);
    }

    // 2) Routers: static routing (normal)
    Ipv4StaticRoutingHelper staticHelper;

    for (uint32_t i = 0; i < numPaths; i++)
    {
        Ptr<Ipv4> rIpv4 = routerNodes.Get(i)->GetObject<Ipv4>();
        Ptr<Ipv4StaticRouting> rStatic = staticHelper.GetStaticRouting(rIpv4);

        // Route to service IP (hosted on server loopback) via server interface address
        uint32_t ifToServer = rIpv4->GetInterfaceForDevice(routerToServerDevices[i].Get(0));
        Ipv4Address serverNextHop =
            routerToServerInterfaces[i].GetAddress(1); // server address on that link
        rStatic->AddHostRouteTo(serviceIp, serverNextHop, ifToServer);

        // Route back to client via balancer on balancer-router link
        uint32_t ifToBalancer = rIpv4->GetInterfaceForDevice(balancerToRouterDevices[i].Get(1));
        Ipv4Address balancerNextHop =
            balancerToRouterInterfaces[i].GetAddress(0); // balancer address on that link
        rStatic->AddHostRouteTo(clientToBalancerInterface.GetAddress(0), balancerNextHop, ifToBalancer);
    }

    // 3) Server: optionally use PerPacketLoadBalancer so ACKs are sprayed (RouteOutput)
    serverIpv4 = serverNode.Get(0)->GetObject<Ipv4>();
    if (ackFeatureExperiment)
    {
        Ptr<PerPacketLoadBalancer> serverLb = CreateObject<PerPacketLoadBalancer>();
        serverIpv4->SetRoutingProtocol(serverLb);

        // Multiple routes to client host via each router interface (used by RouteOutput round-robin)
        for (uint32_t i = 0; i < numPaths; i++)
        {
            uint32_t ifToRouter = serverIpv4->GetInterfaceForDevice(routerToServerDevices[i].Get(1));
            Ipv4Address routerNextHop =
                routerToServerInterfaces[i].GetAddress(0); // router address on that link
            serverLb->AddHostRouteTo(clientToBalancerInterface.GetAddress(0), routerNextHop, ifToRouter);
        }
    }
    else
    {
        Ptr<Ipv4StaticRouting> serverStatic = staticHelper.GetStaticRouting(serverIpv4);
        uint32_t ifToRouter = serverIpv4->GetInterfaceForDevice(routerToServerDevices[0].Get(1));
        Ipv4Address routerNextHop = routerToServerInterfaces[0].GetAddress(0);
        serverStatic->AddHostRouteTo(clientToBalancerInterface.GetAddress(0), routerNextHop, ifToRouter);
    }

    // 4) Client: static route to service IP via balancer
    Ptr<Ipv4> clientIpv4 = clientNode.Get(0)->GetObject<Ipv4>();
    Ptr<Ipv4StaticRouting> cStatic = staticHelper.GetStaticRouting(clientIpv4);
    // Client interface to balancer is typically 1 (0 loopback), but safer:
    uint32_t ifToBalancer = clientIpv4->GetInterfaceForDevice(clientToBalancerDevice.Get(0));
    cStatic->AddHostRouteTo(serviceIp, clientToBalancerInterface.GetAddress(1), ifToBalancer);



    

    // ==========================================================================
    // НАСТРОЙКА ПРИЛОЖЕНИЙ ДЛЯ ГЕНЕРАЦИИ ТРАФИКА
    // ==========================================================================
    NS_LOG_INFO ("Настройка приложений...");
    
    // TCP-сервер (приемник данных) на узле-сервере
    uint16_t serverPort = 5000;
    // Сервер "живет" по адресу 10.1.10.1 - это виртуальный адрес, к которому обращается клиент
    Address serverAddress(InetSocketAddress(serviceIp, serverPort));
    
    // Создание TCP-сервера, который будет принимать входящие соединения
    PacketSinkHelper packetSinkHelper ("ns3::TcpSocketFactory", serverAddress);
    ApplicationContainer serverApp = packetSinkHelper.Install (serverNode.Get (0));
    serverApp.Start (Seconds (0.0));     // Сервер запускается сразу
    serverApp.Stop (simulationTime);     // Работает до конца симуляции

    // TCP-клиент (отправитель данных) на узле-клиенте
    BulkSendHelper bulkSend ("ns3::TcpSocketFactory", serverAddress);
    bulkSend.SetAttribute ("MaxBytes", UintegerValue (0));      // Бесконечная передача
    bulkSend.SetAttribute ("SendSize", UintegerValue (1440));   // Размер TCP-сегмента
    
    ApplicationContainer clientApp = bulkSend.Install (clientNode.Get (0));
    clientApp.Start (Seconds (1.0));     // Клиент начинает через 1 секунду
    clientApp.Stop (simulationTime - Seconds (1));  // Заканчивает за 1 секунду до конца

    g_sink = StaticCast<PacketSink>(serverApp.Get(0));

    // ==========================
    // Tracing: CWND / RTT (wildcard)
    // ==========================
    AsciiTraceHelper ascii;
    g_cWndStream = ascii.CreateFileStream((dir + "cwnd.data").c_str());
    g_rttStream = ascii.CreateFileStream((dir + "rtt.data").c_str());

    // Connect after sockets are likely created
    Simulator::Schedule(Seconds(1.2), []() {
        Config::Connect("/NodeList/*/$ns3::TcpL4Protocol/SocketList/*/CongestionWindow",
                        MakeCallback(&CwndTracer));
        Config::Connect("/NodeList/*/$ns3::TcpL4Protocol/SocketList/*/RTT",
                        MakeCallback(&RttTracer));
    });

    // ==========================
    // Tracing: Throughput time series
    // ==========================
    g_throughputStream = ascii.CreateFileStream((dir + "throughput.data").c_str());
    *g_throughputStream->GetStream() << "#Time(s) Throughput(Mbps)\n";
    Simulator::Schedule(Seconds(1.5), &ThroughputSampler);


    // ==========================================================================
    // ВЫВОД ТАБЛИЦ МАРШРУТИЗАЦИИ ДЛЯ ДЕБАГА
    // ==========================================================================



    NS_LOG_INFO ("Таблица маршрутизации балансировщика:");
    balancerNode.Get(0)->GetObject<Ipv4>()->GetRoutingProtocol()->PrintRoutingTable (routingStream, Time::S);

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
    // НАСТРОЙКА СИСТЕМЫ МОНИТОРИНГА ДЛЯ СБОРА СТАТИСТИКИ
    // ==========================================================================
    NS_LOG_INFO ("Настройка мониторинга...");
    
    FlowMonitorHelper flowMonitor;
    Ptr<FlowMonitor> monitor = flowMonitor.InstallAll ();  // Мониторинг на всех узлах

    // // ==========================================================================
    // // МОНИТОРИНГ CWND - ДОБАВЬ СЮДА
    // // ==========================================================================
    // NS_LOG_INFO ("Настройка мониторинга CWND...");

    // /// ДОБАВЬ ЭТО ДЛЯ МОНИТОРИНГА CWND:
    // std::string dir = "results/data/";
    // std::string dirToSave = "mkdir -p " + dir;
    // system (dirToSave.c_str ());

    // Simulator::Schedule (Seconds (1 + 0.1), &TraceCwnd, dir + "cwnd.data");
    // NS_LOG_INFO("Мониторинг CWND активирован");


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

    Simulator::Destroy ();
    NS_LOG_INFO ("Симуляция завершена.");
    NS_LOG_INFO ("Saved to:" + dir);
    return 0;
  }