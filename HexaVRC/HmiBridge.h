/**
 * @file HmiBridge.h
 * @brief TCP Network Interface for the Mock Controller.
 * @author HexaKinetica Team
 * @version 1.0
 *
 * This file defines the `HmiBridge` class, which handles the raw TCP socket
 * communication. It uses a dedicated thread for listening and receiving data to
 * avoid blocking the real-time simulation loop.
 */

#ifndef HMIBRIDGE_H
#define HMIBRIDGE_H

#include "../Shared/RdtProtocol.h"
#include "../Shared/nlohmann/json.hpp"
#include <functional>
#include <thread>
#include <mutex>
#include <atomic>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#else
using SOCKET = int;
#endif

/**
 * @brief Manages network communication with the HMI.
 *
 * @details
 * - **Server:** Listens on a specific port (default 30002).
 * - **Threading:** Runs a `serverThreadLoop` to accept connections and `handleClient` threads for reads.
 * - **Queueing:** Incoming messages are stored in a thread-safe queue (`m_incomingQueue`)
 *   and processed synchronously via `poll()` in the main loop.
 */
class HmiBridge {
public:
    HmiBridge();
    ~HmiBridge();

    /**
     * @brief Starts the TCP server.
     * @param port Port to listen on.
     * @return True if successful.
     */
    bool start(int port);

    /**
     * @brief Stops the server and disconnects clients.
     */
    void stop();

    /**
     * @brief Processes all queued incoming messages.
     * @details Should be called once per main loop iteration.
     * Invokes registered callbacks (config, control state, program).
     */
    void poll();

    /**
     * @brief Sends the robot status to all connected clients.
     * @param status The status structure to serialize and broadcast.
     */
    void broadcastStatus(const Rdt::RobotStatus &status);

    // --- Callbacks ---

    /**
     * @brief Register a provider for the current configuration.
     * Used when the HMI requests a config sync.
     */
    void setConfigProvider(std::function<Rdt::ControllerConfig()> provider);

    /**
     * @brief Register a handler for new configuration received from HMI.
     */
    void setConfigUpdateHandler(std::function<void(const Rdt::ControllerConfig&)> handler);

    /**
     * @brief Register a handler for control state updates (jog, mode, e-stop).
     */
    void setControlStateHandler(std::function<void(const Rdt::ControlState&)> handler);

    /**
     * @brief Register a handler for program uploads.
     */
    void setProgramUploadHandler(std::function<void(const Rdt::ProgramData&)> handler);

private:
    void serverThreadLoop();
    void handleClient(SOCKET clientSocket);

    /**
     * @brief Parses raw JSON and routes it to the appropriate handler.
     */
    void processIncomingJson(const nlohmann::json &j);

    SOCKET m_listenSocket;
    std::atomic<bool> m_running;
    std::thread m_serverThread;

    std::vector<std::string> m_incomingQueue;
    std::mutex m_queueMutex;

    // Callbacks
    std::function<Rdt::ControllerConfig()> m_configProvider;
    std::function<void(const Rdt::ControllerConfig&)> m_configUpdater;
    std::function<void(const Rdt::ControlState&)> m_controlHandler;
    std::function<void(const Rdt::ProgramData&)> m_programHandler;

    // Clients
    std::vector<SOCKET> m_clients;
    std::mutex m_clientMutex;
};

#endif // HMIBRIDGE_H
