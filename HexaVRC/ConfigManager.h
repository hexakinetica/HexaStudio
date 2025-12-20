#ifndef CONFIGMANAGER_H
#define CONFIGMANAGER_H

#include "../shared/RdtProtocol.h"
#include <string>

class ConfigManager
{
public:
    ConfigManager(const std::string& filename = "mock_config.json");

    // Load from disk. If missing, creates default and saves it.
    Rdt::ControllerConfig load();

    // Save to disk.
    bool save(const Rdt::ControllerConfig& config);

    // Ensure fixed slots (10 tools, 10 bases)
    static void validateSlots(Rdt::ControllerConfig& cfg);

private:
    std::string m_filename;
    Rdt::ControllerConfig createDefault();
};

#endif // CONFIGMANAGER_H
