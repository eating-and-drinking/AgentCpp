#include <memory/providers/HttpEmbedder.hpp>

#include <utils/Logger.hpp>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

namespace agentcpp::memory {

namespace {

size_t writeCb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* s = static_cast<std::string*>(userdata);
    s->append(ptr, size * nmemb);
    return size * nmemb;
}

} // namespace

HttpEmbedder::HttpEmbedder(HttpEmbedderConfig cfg)
    : cfg_(std::move(cfg)) {
    name_ = "http/" + cfg_.model;
}

bool HttpEmbedder::available() const {
    return !cfg_.base_url.empty() && !cfg_.api_key.empty();
}

std::vector<std::vector<float>>
HttpEmbedder::embed(const std::vector<std::string>& texts) {
    if (!available() || texts.empty()) return {};

    using json = nlohmann::json;
    json body = {{"model", cfg_.model}, {"input", texts}};
    std::string body_str = body.dump();

    std::string url = cfg_.base_url;
    if (!url.empty() && url.back() == '/') url.pop_back();
    url += "/v1/embeddings";

    CURL* h = curl_easy_init();
    if (!h) return {};

    std::string resp_body;
    struct curl_slist* hdrs = nullptr;
    std::string auth_hdr = "Authorization: Bearer " + cfg_.api_key;
    hdrs = curl_slist_append(hdrs, "Content-Type: application/json");
    hdrs = curl_slist_append(hdrs, auth_hdr.c_str());

    curl_easy_setopt(h, CURLOPT_URL,             url.c_str());
    curl_easy_setopt(h, CURLOPT_HTTPHEADER,      hdrs);
    curl_easy_setopt(h, CURLOPT_POST,            1L);
    curl_easy_setopt(h, CURLOPT_POSTFIELDS,      body_str.c_str());
    curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE,   static_cast<long>(body_str.size()));
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION,   writeCb);
    curl_easy_setopt(h, CURLOPT_WRITEDATA,       &resp_body);
    curl_easy_setopt(h, CURLOPT_TIMEOUT,         static_cast<long>(cfg_.timeout_sec));
    curl_easy_setopt(h, CURLOPT_NOSIGNAL,        1L);

    CURLcode rc = curl_easy_perform(h);
    long http_code = 0;
    curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &http_code);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(h);

    if (rc != CURLE_OK) {
        LOG_WARN(std::string("HttpEmbedder: curl error: ") + curl_easy_strerror(rc));
        return {};
    }
    if (http_code < 200 || http_code >= 300) {
        LOG_WARN("HttpEmbedder: HTTP " + std::to_string(http_code) + ": " + resp_body);
        return {};
    }

    json parsed;
    try {
        parsed = json::parse(resp_body);
    } catch (const std::exception& e) {
        LOG_WARN(std::string("HttpEmbedder: JSON parse failed: ") + e.what());
        return {};
    }
    if (!parsed.contains("data") || !parsed["data"].is_array()) {
        LOG_WARN("HttpEmbedder: response missing 'data' array");
        return {};
    }

    std::vector<std::vector<float>> out;
    out.reserve(parsed["data"].size());
    for (const auto& item : parsed["data"]) {
        if (!item.contains("embedding") || !item["embedding"].is_array()) {
            out.emplace_back();
            continue;
        }
        std::vector<float> v;
        v.reserve(item["embedding"].size());
        for (const auto& f : item["embedding"]) {
            if (f.is_number()) v.push_back(f.get<float>());
        }
        out.push_back(std::move(v));
    }
    return out;
}

} // namespace agentcpp::memory
