#pragma once
#include <string>
#include <map>
#include <functional>

// Sends game context to the Groq API and returns a coaching tip.
// requestTip() is non-blocking: work is done on a detached thread and the
// result is delivered via callback (also on that worker thread).
class AiCoach {
public:
    using Callback = std::function<void(const std::string& tip)>;

    AiCoach(const std::string& apiKey, const std::string& model = "llama3-8b-8192");

    void requestTip(const std::string& gameName,
                    const std::map<std::string, std::string>& stats,
                    Callback callback);

private:
    std::string buildPrompt(const std::string& gameName,
                             const std::map<std::string, std::string>& stats) const;
    std::string callGroq(const std::string& prompt);

    std::string m_apiKey;
    std::string m_model;
};
