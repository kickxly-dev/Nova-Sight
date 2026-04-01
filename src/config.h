#pragma once
#include <string>
#include <vector>

struct OcrRegion {
    std::string label;
    int x, y, w, h;
};

struct GameConfig {
    std::string name;
    std::string process;
    std::vector<OcrRegion> regions;
};

class Config {
public:
    bool load(const std::string& path);
    const GameConfig* findGame(const std::string& processName) const;

    const std::string& getGroqApiKey() const { return m_groqApiKey; }
    const std::string& getGroqModel()  const { return m_groqModel; }

private:
    std::vector<GameConfig> m_games;
    std::string m_groqApiKey;
    std::string m_groqModel{"llama3-8b-8192"};
};
