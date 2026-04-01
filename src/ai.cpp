#include "ai.h"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <sstream>

using json = nlohmann::json;

namespace {
size_t writeCallback(char* ptr, size_t size, size_t nmemb, std::string* buf) {
    buf->append(ptr, size * nmemb);
    return size * nmemb;
}
} // namespace

AiCoach::AiCoach(const std::string& apiKey, const std::string& model)
    : m_apiKey(apiKey), m_model(model) {}

void AiCoach::requestTip(const std::string& gameName,
                          const std::map<std::string, std::string>& stats,
                          Callback callback) {
    std::string prompt = buildPrompt(gameName, stats);
    std::thread([this, prompt, cb = std::move(callback)]() mutable {
        std::string tip = callGroq(prompt);
        if (cb) cb(tip);
    }).detach();
}

std::string AiCoach::buildPrompt(const std::string& gameName,
                                  const std::map<std::string, std::string>& stats) const {
    std::ostringstream oss;
    oss << "You are a concise esports coach. Game: " << gameName << ". Current stats: ";
    for (const auto& [k, v] : stats)
        oss << k << "=" << v << " ";
    oss << ". Give ONE short actionable coaching tip (max 15 words). No preamble.";
    return oss.str();
}

std::string AiCoach::callGroq(const std::string& prompt) {
    CURL* curl = curl_easy_init();
    if (!curl) return "AI unavailable.";

    json body = {
        {"model",       m_model},
        {"messages",    {{{"role", "user"}, {"content", prompt}}}},
        {"max_tokens",  60},
        {"temperature", 0.7}
    };
    std::string bodyStr = body.dump();
    std::string response;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    std::string authHeader = "Authorization: Bearer " + m_apiKey;
    headers = curl_slist_append(headers, authHeader.c_str());

    curl_easy_setopt(curl, CURLOPT_URL,           "https://api.groq.com/openai/v1/chat/completions");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER,    headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS,    bodyStr.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(bodyStr.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,     &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,       10L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) return "Network error.";

    try {
        json resp = json::parse(response);
        std::string tip = resp["choices"][0]["message"]["content"].get<std::string>();
        // Trim leading/trailing whitespace
        size_t s = tip.find_first_not_of(" \n\r\t");
        size_t e = tip.find_last_not_of(" \n\r\t");
        return (s == std::string::npos) ? "" : tip.substr(s, e - s + 1);
    } catch (...) {
        return "Could not parse AI response.";
    }
}
