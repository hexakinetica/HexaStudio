#include "HmiBridge.h"
#include "MockJsonHelpers.h"
#include <iostream>
#include <ws2tcpip.h>

using json = nlohmann::json;
using namespace Rdt;

HmiBridge::HmiBridge() : m_running(false), m_listenSocket(INVALID_SOCKET) {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
}

HmiBridge::~HmiBridge() {
    stop();
#ifdef _WIN32
    WSACleanup();
#endif
}

bool HmiBridge::start(int port) {
    m_listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_listenSocket == INVALID_SOCKET) return false;

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);

    if (bind(m_listenSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) return false;
    if (listen(m_listenSocket, SOMAXCONN) == SOCKET_ERROR) return false;

    m_running = true;
    m_serverThread = std::thread(&HmiBridge::serverThreadLoop, this);
    std::cout << "[HMI Bridge] Started on port " << port << std::endl;
    return true;
}

void HmiBridge::stop() {
    m_running = false;
    if (m_listenSocket != INVALID_SOCKET) {
        closesocket(m_listenSocket);
        m_listenSocket = INVALID_SOCKET;
    }
    if (m_serverThread.joinable()) m_serverThread.join();
}

void HmiBridge::serverThreadLoop() {
    while (m_running) {
        SOCKET clientSocket = accept(m_listenSocket, NULL, NULL);
        if (clientSocket == INVALID_SOCKET) continue;

        {
            std::lock_guard<std::mutex> lock(m_clientMutex);
            m_clients.push_back(clientSocket);
        }
        std::cout << "[HMI Bridge] Client connected." << std::endl;
        std::thread([this, clientSocket]() { handleClient(clientSocket); }).detach();
    }
}

void HmiBridge::handleClient(SOCKET clientSocket) {
    char buffer[16384];
    while (m_running) {
        int bytesReceived = recv(clientSocket, buffer, sizeof(buffer), 0);
        if (bytesReceived <= 0) break;

        std::string receivedData(buffer, bytesReceived);
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_incomingQueue.push_back(receivedData);
    }
    closesocket(clientSocket);
    std::lock_guard<std::mutex> lock(m_clientMutex);
    m_clients.erase(std::remove(m_clients.begin(), m_clients.end(), clientSocket), m_clients.end());
}

void HmiBridge::poll() {
    std::vector<std::string> batch;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        batch = std::move(m_incomingQueue);
        m_incomingQueue.clear();
    }

    for (const auto& data : batch) {
        try {
            auto j = json::parse(data);
            processIncomingJson(j);
        } catch (const std::exception& e) {
            // std::cerr << "JSON Error: " << e.what() << std::endl;
        }
    }
}

void HmiBridge::processIncomingJson(const json &j) {
    if (!j.contains("type")) return;
    PacketType type = static_cast<PacketType>(j["type"].get<int>());

    // FIX: Only handle CONTROL_STATE. All other logic (Config/Program) is embedded inside it.
    switch (type) {
    case PacketType::CONTROL_STATE:
        if (j.contains("payload") && m_controlHandler) {
            m_controlHandler(j["payload"].get<ControlState>());
        }
        break;
    default: break;
    }
}

void HmiBridge::broadcastStatus(const RobotStatus &status) {
    json j;
    j["type"] = static_cast<int>(PacketType::STATUS_UPDATE);
    j["payload"] = status;
    std::string data = j.dump();
    data += "\n";

    std::lock_guard<std::mutex> lock(m_clientMutex);
    for (SOCKET s : m_clients) send(s, data.c_str(), (int)data.size(), 0);
}

void HmiBridge::setConfigProvider(std::function<Rdt::ControllerConfig()> provider) { m_configProvider = provider; }
void HmiBridge::setConfigUpdateHandler(std::function<void(const Rdt::ControllerConfig&)> handler) { m_configUpdater = handler; }
void HmiBridge::setControlStateHandler(std::function<void(const Rdt::ControlState&)> handler) { m_controlHandler = handler; }
void HmiBridge::setProgramUploadHandler(std::function<void(const Rdt::ProgramData&)> handler) { m_programHandler = handler; }
