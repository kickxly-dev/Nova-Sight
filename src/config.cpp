#include "config.h"
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

bool Config::load(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return false;

    try {
        json root = json::parse(f);

        if (root.contains("groq_api_key"))
            m_groqApiKey = root["groq_api_key"].get<std::string>();
        if (root.contains("groq_model"))
            m_groqModel = root["groq_model"].get<std::string>();

        for (const auto& g : root["games"]) {
            GameConfig game;
            game.name    = g["name"].get<std::string>();
            game.process = g["process"].get<std::string>();

            for (const auto& r : g["regions"]) {
                OcrRegion region;
                region.label = r["label"].get<std::string>();
                region.x     = r["x"].get<int>();
                region.y     = r["y"].get<int>();
                region.w     = r["w"].get<int>();
                region.h     = r["h"].get<int>();
                game.regions.push_back(region);
            }
            m_games.push_back(std::move(game));
        }
        return true;
    } catch (...) {
        return false;
    }
}

const GameConfig* Config::findGame(const std::string& processName) const {
    for (const auto& g : m_games) {
        if (g.process == processName) return &g;
    }
    return nullptr;
}
