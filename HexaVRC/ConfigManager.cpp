#include "ConfigManager.h"
#include "MockJsonHelpers.h"
#include <fstream>
#include <iostream>
#include <filesystem>

ConfigManager::ConfigManager(const std::string& filename)
    : m_filename(filename)
{
}

Rdt::ControllerConfig ConfigManager::load()
{
    Rdt::ControllerConfig cfg;
    bool loaded = false;

    if (std::filesystem::exists(m_filename)) {
        try {
            std::ifstream file(m_filename);
            if (file.is_open()) {
                json j;
                file >> j;
                cfg = j.get<Rdt::ControllerConfig>();
                loaded = true;
                std::cout << "[Config] Loaded from " << m_filename << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "[Config] Load Error: " << e.what() << ". Using default." << std::endl;
        }
    }

    if (!loaded) {
        cfg = createDefault();
        save(cfg);
    } else {
        // Ensure slots are correct even if loaded from partial file
        validateSlots(cfg);
    }

    return cfg;
}

bool ConfigManager::save(const Rdt::ControllerConfig& config)
{
    // Ensure consistency before saving
    Rdt::ControllerConfig toSave = config;
    validateSlots(toSave);

    try {
        json j = toSave;
        std::ofstream file(m_filename);
        if (file.is_open()) {
            file << j.dump(4);
            std::cout << "[Config] Saved to " << m_filename << std::endl;
            return true;
        }
    } catch (const std::exception& e) {
        std::cerr << "[Config] Save Error: " << e.what() << std::endl;
    }
    return false;
}

void ConfigManager::validateSlots(Rdt::ControllerConfig& cfg)
{
    // Ensure exactly 10 tools
    if (cfg.tools.size() < 10) {
        cfg.tools.resize(10);
    } else if (cfg.tools.size() > 10) {
        cfg.tools.resize(10);
    }

    for (int i = 0; i < 10; ++i) {
        cfg.tools[i].id = i;
        if (cfg.tools[i].name.empty()) {
            cfg.tools[i].name = "Empty Tool " + std::to_string(i);
        }
    }

    // Ensure exactly 10 bases
    if (cfg.bases.size() < 10) {
        cfg.bases.resize(10);
    } else if (cfg.bases.size() > 10) {
        cfg.bases.resize(10);
    }

    for (int i = 0; i < 10; ++i) {
        cfg.bases[i].id = i;
        if (cfg.bases[i].name.empty()) {
            cfg.bases[i].name = "Empty Base " + std::to_string(i);
        }
    }
}

Rdt::ControllerConfig ConfigManager::createDefault()
{
    Rdt::ControllerConfig cfg;
    cfg.ipAddress = "127.0.0.1";

    // Pre-populate slots will be handled by validateSlots
    validateSlots(cfg);

    // Set some defaults
    cfg.tools[0].name = "Flange";
    cfg.tools[1].name = "Gripper";
    cfg.tools[1].offset = {0, 0, 150, 0, 0, 0};

    cfg.bases[0].name = "World";

    // Default Limits
    for(int i=0; i<6; ++i) {
        cfg.axisLimits[i][0] = -170.0;
        cfg.axisLimits[i][1] = 170.0;
    }

    return cfg;
}
