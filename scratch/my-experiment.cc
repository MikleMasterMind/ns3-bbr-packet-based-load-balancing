#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/flow-monitor-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("PacketRoundRobinBalancer");

// Балансировщик нагрузки с round-robin распределением пакетов
// Работает как NAT: меняет IP адреса в заголовках пакетов
class PacketRoundRobinBalancer : public Application {
public:
    PacketRoundRobinBalancer() : m_nextChannel(0) {}

    static TypeId GetTypeId() {
        static TypeId tid = TypeId("PacketRoundRobinBalancer")
            .SetParent<Application>()
            .SetGroupName("LoadBalancing")
            .AddConstructor<PacketRoundRobinBalancer>();
        return tid;
    }

    // Инициализация балансировщика
    void Setup(Ptr<Node> balancerNode, 
               std::vector<Ptr<NetDevice>> serverDevices,
               Ptr<NetDevice> clientDevice,
               std::vector<Ipv4Address> serverAddresses) {
        m_balancerNode = balancerNode;
        m_serverDevices = serverDevices;
        m_clientDevice = clientDevice;
        m_serverAddresses = serverAddresses;
    }

protected:
    virtual void StartApplication() override {
        NS_LOG_INFO("Starting PacketRoundRobinBalancer on node " << m_balancerNode->GetId());
        SetupPacketHandlers();
        NS_LOG_INFO("Balancer started with " << m_serverDevices.size() << " channels");
    }

    virtual void StopApplication() override {
        if (m_clientDevice) {
            m_clientDevice->SetPromiscReceiveCallback(MakeNullCallback<bool, Ptr<NetDevice>, Ptr<const Packet>, uint16_t, const Address&, const Address&, NetDevice::PacketType>());
        }
        for (auto& device : m_serverDevices) {
            if (device) {
                device->SetPromiscReceiveCallback(MakeNullCallback<bool, Ptr<NetDevice>, Ptr<const Packet>, uint16_t, const Address&, const Address&, NetDevice::PacketType>());
            }
        }
        NS_LOG_INFO("Stopping PacketRoundRobinBalancer");
    }

private:
    Ptr<Node> m_balancerNode;
    std::vector<Ptr<NetDevice>> m_serverDevices;  // Устройства к серверам
    Ptr<NetDevice> m_clientDevice;                // Устройство к клиенту
    std::vector<Ipv4Address> m_serverAddresses;   // Реальные адреса серверов
    uint32_t m_nextChannel;                       // Счетчик для round-robin

    // Настройка перехвата пакетов на сетевых устройствах
    void SetupPacketHandlers() {
        if (!m_clientDevice) {
            NS_LOG_ERROR("Client device is null");
            return;
        }
        
        // Перехват пакетов от клиента (все пакеты, не только адресованные балансировщику)
        m_clientDevice->SetPromiscReceiveCallback(MakeCallback(&PacketRoundRobinBalancer::HandlePacketFromClient, this));
        
        // Перехват пакетов от серверов
        for (auto& serverDevice : m_serverDevices) {
            if (serverDevice) {
                serverDevice->SetPromiscReceiveCallback(MakeCallback(&PacketRoundRobinBalancer::HandlePacketFromServer, this));
            }
        }
        
        NS_LOG_INFO("Packet handlers setup complete");
    }

    // Обработка пакетов, идущих от клиента к балансировщику
    bool HandlePacketFromClient(Ptr<NetDevice> device, Ptr<const Packet> packet, 
                               uint16_t protocol, const Address& from, 
                               const Address& to, NetDevice::PacketType packetType) {
        if (protocol != 0x0800) return false;  // Только IP пакеты
        
        NS_LOG_INFO("CLIENT->BALANCER: Packet size: " << packet->GetSize());
        
        if (m_serverDevices.empty()) {
            NS_LOG_ERROR("No server devices available");
            return false;
        }
        
        // Round-robin алгоритм: циклический выбор канала
        uint32_t channelId = m_nextChannel;
        m_nextChannel = (m_nextChannel + 1) % m_serverDevices.size();
        
        NS_LOG_INFO("Round-robin: forwarding via channel " << channelId);
        
        // Пересылка с изменением IP заголовка
        ForwardToServer(packet, channelId);
        
        return true;  // Пакет поглощен, предотвращаем дальнейшую обработку
    }

    // Обработка пакетов, идущих от сервера к балансировщику
    bool HandlePacketFromServer(Ptr<NetDevice> device, Ptr<const Packet> packet, 
                               uint16_t protocol, const Address& from, 
                               const Address& to, NetDevice::PacketType packetType) {
        if (protocol != 0x0800) return false;  // Только IP пакеты
        
        NS_LOG_INFO("SERVER->BALANCER: Packet size: " << packet->GetSize());
        
        // Пересылка ответа клиенту с изменением IP заголовка
        ForwardToClient(packet);
        
        return true;  // Пакет поглощен
    }

    // Пересылка пакета к серверу с модификацией destination IP
    void ForwardToServer(Ptr<const Packet> packet, uint32_t channelId) {
        if (channelId >= m_serverDevices.size()) {
            NS_LOG_ERROR("Invalid channel ID: " << channelId);
            return;
        }

        Ptr<NetDevice> serverDevice = m_serverDevices[channelId];
        if (!serverDevice) {
            NS_LOG_ERROR("Server device is null for channel " << channelId);
            return;
        }

        // Создаем копию пакета для модификации
        Ptr<Packet> modifiedPacket = packet->Copy();
        
        // Извлекаем IP заголовок
        Ipv4Header ipHeader;
        if (modifiedPacket->RemoveHeader(ipHeader) == 0) {
            NS_LOG_ERROR("Failed to remove IP header");
            return;
        }

        // NAT: меняем адрес назначения на реальный адрес сервера
        // Клиент отправляет на 10.1.1.2, балансировщик меняет на 10.1.2.2 или 10.1.3.2
        Ipv4Address serverAddress = m_serverAddresses[channelId];
        ipHeader.SetDestination(serverAddress);
        
        ipHeader.EnableChecksum();  // Пересчет контрольной суммы
        
        modifiedPacket->AddHeader(ipHeader);
        
        NS_LOG_INFO("Modified packet: " << ipHeader.GetSource() << " -> " << ipHeader.GetDestination());
        
        // Отправка модифицированного пакета серверу
        serverDevice->Send(modifiedPacket, serverDevice->GetBroadcast(), 0x0800);
        
        NS_LOG_INFO("Packet forwarded to server " << serverAddress << " via channel " << channelId);
    }

    // Пересылка пакета клиенту с модификацией source IP
    void ForwardToClient(Ptr<const Packet> packet) {
        if (!m_clientDevice) {
            NS_LOG_ERROR("Client device is null");
            return;
        }

        // Создаем копию пакета для модификации
        Ptr<Packet> modifiedPacket = packet->Copy();
        
        // Извлекаем IP заголовок
        Ipv4Header ipHeader;
        if (modifiedPacket->RemoveHeader(ipHeader) == 0) {
            NS_LOG_ERROR("Failed to remove IP header");
            return;
        }

        // NAT: меняем адрес источника на адрес балансировщика
        // Клиент должен думать, что ответ пришел от балансировщика (10.1.1.2)
        Ipv4Address balancerAddress = GetBalancerAddress();
        ipHeader.SetSource(balancerAddress);
        
        ipHeader.EnableChecksum();  // Пересчет контрольной суммы
        
        modifiedPacket->AddHeader(ipHeader);
        
        NS_LOG_INFO("Modified response: " << ipHeader.GetSource() << " -> " << ipHeader.GetDestination());
        
        // Отправка модифицированного пакета клиенту
        m_clientDevice->Send(modifiedPacket, m_clientDevice->GetBroadcast(), 0x0800);
        
        NS_LOG_INFO("Packet forwarded to client");
    }

    // Получение IP адреса балансировщика на интерфейсе к клиенту
    Ipv4Address GetBalancerAddress() {
        Ptr<Ipv4> ipv4 = m_balancerNode->GetObject<Ipv4>();
        if (!ipv4) {
            NS_LOG_ERROR("Failed to get Ipv4 object");
            return Ipv4Address();
        }
        
        return ipv4->GetAddress(1, 0).GetLocal();  // Интерфейс 1: клиентская сеть
    }
};

int main(int argc, char *argv[]) {
    LogComponentEnable("PacketRoundRobinBalancer", LOG_LEVEL_INFO);

    /*
     * КОНФИГУРАЦИЯ СЕТИ:
     * 
     * Топология:
     * Node 0 (Клиент) --- Node 1 (Балансировщик) --- Node 2 (Сервер)
     *                           |          |
     *                           |          |
     *                    (канал 0)    (канал 1)
     * 
     * IP АДРЕСА:
     * - Клиент (Node 0): 10.1.1.1
     * - Балансировщик (Node 1): 
     *   * Клиентский интерфейс: 10.1.1.2
     *   * Серверный интерфейс 0: 10.1.2.1  
     *   * Серверный интерфейс 1: 10.1.3.1
     * - Сервер (Node 2):
     *   * Интерфейс канала 0: 10.1.2.2
     *   * Интерфейс канала 1: 10.1.3.2
     * 
     * ЛОГИКА РАБОТЫ:
     * 1. Клиент отправляет пакеты на 10.1.1.2 (балансировщик)
     * 2. Балансировщик round-robin распределяет пакеты по каналам
     * 3. Для каждого пакета меняет destination IP на 10.1.2.2 или 10.1.3.2
     * 4. Сервер обрабатывает пакет и отправляет ответ
     * 5. Балансировщик перехватывает ответ и меняет source IP на 10.1.1.2
     * 6. Клиент получает ответ, думая что он от балансировщика
     */

    double simulationTime = 10.0;
    uint32_t numChannels = 2;

    // Создание сетевых узлов
    NodeContainer nodes;
    nodes.Create(3);  // 0-клиент, 1-балансировщик, 2-сервер

    // Настройка point-to-point каналов
    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("1Mbps"));
    p2p.SetChannelAttribute("Delay", StringValue("2ms"));

    // Создание двух каналов между балансировщиком и сервером
    std::vector<NetDeviceContainer> serverChannels;
    for (uint32_t i = 0; i < numChannels; ++i) {
        serverChannels.push_back(p2p.Install(NodeContainer(nodes.Get(1), nodes.Get(2))));
        NS_LOG_INFO("Created server channel " << i);
    }

    // Создание канала между клиентом и балансировщиком
    NetDeviceContainer clientChannel = p2p.Install(NodeContainer(nodes.Get(0), nodes.Get(1)));

    // Установка TCP/IP стека на все узлы
    InternetStackHelper stack;
    stack.Install(nodes);

    // Назначение IP адресов
    Ipv4AddressHelper address;
    
    // Сеть клиент-балансировщик: 10.1.1.0/24
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer clientBalancerIfaces = address.Assign(clientChannel);
    
    // Сети балансировщик-сервер: 10.1.2.0/24 и 10.1.3.0/24
    std::vector<Ipv4InterfaceContainer> serverIfaces;
    for (uint32_t i = 0; i < serverChannels.size(); ++i) {
        std::string base = "10.1." + std::to_string(i + 2) + ".0";
        address.SetBase(base.c_str(), "255.255.255.0");
        serverIfaces.push_back(address.Assign(serverChannels[i]));
        NS_LOG_INFO("Server channel " << i << ": " << base);
    }

    // Настройка маршрутизации
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // Подготовка устройств для балансировщика
    std::vector<Ptr<NetDevice>> serverDevices;
    std::vector<Ipv4Address> serverAddresses;
    
    // Сбор серверных устройств балансировщика и реальных адресов сервера
    for (uint32_t i = 0; i < serverChannels.size(); ++i) {
        serverDevices.push_back(serverChannels[i].Get(0));  // Устройство балансировщика
        serverAddresses.push_back(serverIfaces[i].GetAddress(1));  // Адрес сервера
        NS_LOG_INFO("Server " << i << " address: " << serverAddresses.back());
    }
    
    // Устройство балансировщика для связи с клиентом
    Ptr<NetDevice> clientDevice = clientChannel.Get(1);

    // Серверное приложение - приемник TCP трафика
    uint16_t port = 5000;
    PacketSinkHelper sinkHelper("ns3::TcpSocketFactory", 
                               InetSocketAddress(Ipv4Address::GetAny(), port));
    ApplicationContainer serverApps = sinkHelper.Install(nodes.Get(2));
    serverApps.Start(Seconds(0.0));
    serverApps.Stop(Seconds(simulationTime));

    // Клиентское приложение - отправитель TCP трафика
    BulkSendHelper clientHelper("ns3::TcpSocketFactory", 
                              InetSocketAddress(clientBalancerIfaces.GetAddress(1), port));
    clientHelper.SetAttribute("MaxBytes", UintegerValue(100000));
    
    ApplicationContainer clientApps = clientHelper.Install(nodes.Get(0));
    clientApps.Start(Seconds(1.0));
    clientApps.Stop(Seconds(simulationTime - 1));

    // Установка и запуск балансировщика
    Ptr<PacketRoundRobinBalancer> balancer = CreateObject<PacketRoundRobinBalancer>();
    balancer->Setup(nodes.Get(1), serverDevices, clientDevice, serverAddresses);
    nodes.Get(1)->AddApplication(balancer);
    balancer->SetStartTime(Seconds(0.5));
    balancer->SetStopTime(Seconds(simulationTime));

    // Включение трассировки для анализа
    AsciiTraceHelper ascii;
    p2p.EnableAsciiAll(ascii.CreateFileStream("trace.tr"));

    // Вывод конфигурации сети
    NS_LOG_INFO("=== NETWORK CONFIGURATION ===");
    NS_LOG_INFO("Client IP: " << clientBalancerIfaces.GetAddress(0));
    NS_LOG_INFO("Balancer IP: " << clientBalancerIfaces.GetAddress(1));
    for (uint32_t i = 0; i < serverAddresses.size(); ++i) {
        NS_LOG_INFO("Real Server " << i << " IP: " << serverAddresses[i]);
    }
    NS_LOG_INFO("Starting simulation for " << simulationTime << " seconds...");
    
    // Запуск симуляции
    Simulator::Stop(Seconds(simulationTime));
    Simulator::Run();
    
    // Сбор и вывод статистики
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