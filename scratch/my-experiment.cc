#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/traffic-control-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("RoundRobinBalancer");

class RoundRobinBalancer : public Application {
public:
    RoundRobinBalancer() : m_nextChannel(0) {}

    static TypeId GetTypeId() {
        static TypeId tid = TypeId("RoundRobinBalancer")
            .SetParent<Application>()
            .SetGroupName("LoadBalancing")
            .AddConstructor<RoundRobinBalancer>();
        return tid;
    }

    void Setup(Ptr<Node> balancerNode, Ptr<Node> serverNode, 
               std::vector<NetDeviceContainer> channels,
               Ipv4InterfaceContainer clientBalancerIfaces) {
        m_balancerNode = balancerNode;
        m_serverNode = serverNode;
        m_channels = channels;
        m_clientBalancerIfaces = clientBalancerIfaces;
    }

protected:
    virtual void StartApplication() override {
        NS_LOG_INFO("Starting RoundRobinBalancer on node " << m_balancerNode->GetId());
        SetupPacketHandlers();
        NS_LOG_INFO("Balancer started with " << m_channels.size() << " channels");
    }

    virtual void StopApplication() override {
        for (auto& device : m_clientDevices) {
            device->SetPromiscReceiveCallback(MakeNullCallback<bool, Ptr<NetDevice>, Ptr<const Packet>, uint16_t, const Address&, const Address&, NetDevice::PacketType>());
        }
        for (auto& device : m_serverDevices) {
            device->SetPromiscReceiveCallback(MakeNullCallback<bool, Ptr<NetDevice>, Ptr<const Packet>, uint16_t, const Address&, const Address&, NetDevice::PacketType>());
        }
        NS_LOG_INFO("Stopping RoundRobinBalancer");
    }

private:
    Ptr<Node> m_balancerNode, m_serverNode;
    std::vector<NetDeviceContainer> m_channels;
    Ipv4InterfaceContainer m_clientBalancerIfaces;
    uint32_t m_nextChannel;
    
    std::vector<Ptr<NetDevice>> m_clientDevices;
    std::vector<Ptr<NetDevice>> m_serverDevices;

    void SetupPacketHandlers() {
        // Устройство подключенное к клиенту (первое устройство балансировщика)
        Ptr<NetDevice> clientDevice = m_balancerNode->GetDevice(0);
        m_clientDevices.push_back(clientDevice);
        
        // Включаем promiscuous режим для перехвата всех пакетов от клиента
        clientDevice->SetPromiscReceiveCallback(MakeCallback(&RoundRobinBalancer::HandlePacketFromClient, this));
        
        // Устройства подключенные к серверу (последующие устройства балансировщика)
        for (uint32_t i = 0; i < m_channels.size(); ++i) {
            Ptr<NetDevice> serverDevice = m_balancerNode->GetDevice(1 + i);
            m_serverDevices.push_back(serverDevice);
            
            // Включаем promiscuous режим для перехвата пакетов от сервера
            serverDevice->SetPromiscReceiveCallback(MakeCallback(&RoundRobinBalancer::HandlePacketFromServer, this));
        }
        
        NS_LOG_INFO("Packet handlers setup complete");
    }

    // Обработчик пакетов, приходящих от клиента к балансировщику
    bool HandlePacketFromClient(Ptr<NetDevice> device, Ptr<const Packet> packet, 
                               uint16_t protocol, const Address& from, 
                               const Address& to, NetDevice::PacketType packetType) {
        NS_LOG_INFO("Packet from client, size: " << packet->GetSize());
        
        // Получаем следующий канал по алгоритму round-robin
        uint32_t channelId = m_nextChannel;
        m_nextChannel = (m_nextChannel + 1) % m_channels.size();
        
        NS_LOG_INFO("Round-robin: selected channel " << channelId << " for packet");
        
        // Пересылаем пакет через выбранный канал
        ForwardToServer(packet, channelId);
        return false; // false означает, что пакет продолжит обычную обработку
    }

    // Обработчик пакетов, приходящих от сервера к балансировщику
    bool HandlePacketFromServer(Ptr<NetDevice> device, Ptr<const Packet> packet, 
                               uint16_t protocol, const Address& from, 
                               const Address& to, NetDevice::PacketType packetType) {
        NS_LOG_INFO("Packet from server, size: " << packet->GetSize());
        
        // Определяем с какого серверного канала пришел пакет
        uint32_t channelId = 0;
        for (uint32_t i = 0; i < m_serverDevices.size(); ++i) {
            if (m_serverDevices[i] == device) {
                channelId = i;
                break;
            }
        }
        
        NS_LOG_INFO("Forwarding server response from channel " << channelId << " to client");
        ForwardToClient(packet, channelId);
        return false; // false означает, что пакет продолжит обычную обработку
    }

    // Пересылка пакета от балансировщика к серверу через указанный канал
    void ForwardToServer(Ptr<const Packet> packet, uint32_t channelId) {
        if (channelId >= m_serverDevices.size()) {
            NS_LOG_ERROR("Invalid channel ID: " << channelId);
            return;
        }

        NS_LOG_INFO("Forwarding to server via channel " << channelId);
        
        // Отправляем пакет через выбранное сетевое устройство
        Ptr<NetDevice> serverDevice = m_serverDevices[channelId];
        serverDevice->Send(packet->Copy(), serverDevice->GetBroadcast(), 0x0800);
        
        NS_LOG_INFO("Packet forwarded to server via channel " << channelId);
    }

    // Пересылка пакета от балансировщика к клиенту
    void ForwardToClient(Ptr<const Packet> packet, uint32_t channelId) {
        NS_LOG_INFO("Forwarding to client from channel " << channelId);
        
        // Отправляем пакет клиенту через устройство подключенное к клиенту
        Ptr<NetDevice> clientDevice = m_clientDevices[0];
        clientDevice->Send(packet->Copy(), clientDevice->GetBroadcast(), 0x0800);
        
        NS_LOG_INFO("Packet forwarded to client");
    }
};

int main(int argc, char *argv[]) {
    LogComponentEnable("RoundRobinBalancer", LOG_LEVEL_INFO);

    /*
     * КОНФИГУРАЦИЯ СЕТИ:
     * 
     * Топология сети:
     * 
     * Клиент (Node 0) ---(1 канал)--- Балансировщик (Node 1) ---(2 канала)--- Сервер (Node 2)
     *                   10.1.1.0/24                     10.1.2.0/24
     *                                                  10.1.3.0/24
     * 
     * Направления передачи пакетов:
     * - Клиент -> Балансировщик: по одному каналу 10.1.1.0/24
     * - Балансировщик -> Сервер: по двум каналам round-robin (10.1.2.0/24 и 10.1.3.0/24)
     *   Каждый пакет отправляется по следующему каналу в циклическом порядке
     * - Сервер -> Балансировщик: ответные пакеты возвращаются по тому же каналу, что и запрос
     * - Балансировщик -> Клиент: ответы пересылаются обратно клиенту
     * 
     * Стратегия балансировки:
     * - Round-robin на уровне пакетов (per-packet)
     * - Каждый пакет независимо распределяется по каналам
     * - Нет привязки потоков к конкретным каналам
     */

    double simulationTime = 10.0;
    uint32_t numChannels = 2;

    // Создаем три узла: клиент, балансировщик и сервер
    NodeContainer nodes;
    nodes.Create(3);

    // Настраиваем point-to-point каналы
    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("1Mbps"));
    p2p.SetChannelAttribute("Delay", StringValue("2ms"));

    // Создаем два канала между балансировщиком и сервером
    std::vector<NetDeviceContainer> channels;
    for (uint32_t i = 0; i < numChannels; ++i) {
        channels.push_back(p2p.Install(NodeContainer(nodes.Get(1), nodes.Get(2))));
        NS_LOG_INFO("Created channel " << i << " between balancer and server");
    }

    // Создаем один канал между клиентом и балансировщиком
    NetDeviceContainer clientToBalancer = p2p.Install(NodeContainer(nodes.Get(0), nodes.Get(1)));

    // Устанавливаем TCP/IP стек на все узлы
    InternetStackHelper stack;
    stack.Install(nodes);

    // Назначаем IP-адреса интерфейсам
    Ipv4AddressHelper address;
    
    // Клиент и балансировщик: 10.1.1.0/24
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer clientBalancerInterfaces = address.Assign(clientToBalancer);
    
    // Два канала балансировщик-сервер: 10.1.2.0/24 и 10.1.3.0/24
    for (uint32_t i = 0; i < channels.size(); ++i) {
        std::string base = "10.1." + std::to_string(i + 2) + ".0";
        address.SetBase(base.c_str(), "255.255.255.0");
        address.Assign(channels[i]);
        NS_LOG_INFO("Channel " << i << " IP range: " << base);
    }

    // Включаем автоматическую маршрутизацию
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // Серверное приложение: приемник TCP трафика на порту 5000
    uint16_t port = 5000;
    PacketSinkHelper sinkHelper("ns3::TcpSocketFactory", 
                               InetSocketAddress(Ipv4Address::GetAny(), port));
    ApplicationContainer serverApps = sinkHelper.Install(nodes.Get(2));
    serverApps.Start(Seconds(0.0));
    serverApps.Stop(Seconds(simulationTime));

    // Клиентское приложение: отправитель TCP трафика к балансировщику
    BulkSendHelper clientHelper("ns3::TcpSocketFactory", 
                              InetSocketAddress(clientBalancerInterfaces.GetAddress(1), port));
    clientHelper.SetAttribute("MaxBytes", UintegerValue(10000));
    
    ApplicationContainer clientApps = clientHelper.Install(nodes.Get(0));
    clientApps.Start(Seconds(1.0));
    clientApps.Stop(Seconds(simulationTime - 1));

    // Устанавливаем приложение балансировщика на узел балансировщика
    Ptr<RoundRobinBalancer> balancer = CreateObject<RoundRobinBalancer>();
    balancer->Setup(nodes.Get(1), nodes.Get(2), channels, clientBalancerInterfaces);
    nodes.Get(1)->AddApplication(balancer);
    balancer->SetStartTime(Seconds(0.5));
    balancer->SetStopTime(Seconds(simulationTime));

    NS_LOG_INFO("Network Configuration:");
    NS_LOG_INFO("Client IP: " << clientBalancerInterfaces.GetAddress(0));
    NS_LOG_INFO("Balancer IP: " << clientBalancerInterfaces.GetAddress(1));
    NS_LOG_INFO("Starting simulation for " << simulationTime << " seconds...");
    
    Simulator::Stop(Seconds(simulationTime));
    Simulator::Run();
    
    // Вывод статистики после завершения симуляции
    Ptr<PacketSink> sink = DynamicCast<PacketSink>(serverApps.Get(0));
    std::cout << "=== SIMULATION RESULTS ===" << std::endl;
    std::cout << "Total bytes received by server: " << sink->GetTotalRx() << std::endl;
    if (sink->GetTotalRx() > 0) {
        std::cout << "Average throughput: " << (sink->GetTotalRx() * 8.0) / (simulationTime - 1.0) / 1000.0 << " Kbps" << std::endl;
    }
    
    Simulator::Destroy();
    std::cout << "Simulation completed!" << std::endl;

    return 0;
}