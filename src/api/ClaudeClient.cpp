#include <api/ClaudeClient.hpp>
#include <utils/Logger.hpp>
#include <utils/StringUtils.hpp>
#include <curl/curl.h>
#include <sstream>
#include <stdexcept>
#include <algorithm>

namespace agentcpp::api {

// ── Construction / destruction ────────────────────────────────────────────────
ClaudeClient::ClaudeClient(ClientConfig cfg) : cfg_(std::move(cfg)) {
    curl_global_init(CURL_GLOBAL_ALL);
}

ClaudeClient::~ClaudeClient() {
    curl_global_cleanup();
}

// Note: curlWriteCallback / curlHeaderCallback declared in the header are not
// used directly — streamRequest() uses capturing-lambda adapters passed via
// WriteCtx userdata. The static declarations satisfy the vtable but the real
// dispatch happens inside the lambda closures below.

// ── Process a single SSE line ─────────────────────────────────────────────────
void ClaudeClient::processLine(const std::string& line, StreamState& state) {
    if (line.empty()) return;
    if (utils::startsWith(line, "event:")) return; // we rely on data type field

    auto ev = parseStreamEvent(line);
    if (!ev) return;

    applyEvent(*ev, state);
    if (state.onEvent) state.onEvent(*ev);
}

void ClaudeClient::applyEvent(const StreamEvent& ev, StreamState& state) {
    std::visit([&](auto&& e) {
        using T = std::decay_t<decltype(e)>;

        if constexpr (std::is_same_v<T, event::MessageStart>) {
            state.response->id    = e.id;
            state.response->model = e.model;
            state.response->usage.input_tokens = e.usage.input_tokens;
        }
        else if constexpr (std::is_same_v<T, event::ContentBlockStart>) {
            // Resize partial_json storage to accommodate this index
            if (e.index >= static_cast<int>(state.partial_json.size())) {
                state.partial_json.resize(e.index + 1);
            }
            // Push placeholder into response content
            while (static_cast<int>(state.response->content.size()) <= e.index) {
                state.response->content.push_back(TextBlock{});
            }
            state.response->content[e.index] = e.block;
        }
        else if constexpr (std::is_same_v<T, event::ContentBlockDelta>) {
            int idx = e.index;
            if (e.delta_type == "text_delta") {
                // Append to existing TextBlock
                if (idx < static_cast<int>(state.response->content.size())) {
                    if (auto* tb = std::get_if<TextBlock>(&state.response->content[idx])) {
                        tb->text += e.text;
                    }
                }
            } else if (e.delta_type == "input_json_delta") {
                if (idx < static_cast<int>(state.partial_json.size())) {
                    state.partial_json[idx] += e.partial_json;
                }
            }
        }
        else if constexpr (std::is_same_v<T, event::ContentBlockStop>) {
            int idx = e.index;
            // Finalise tool_use input JSON
            if (idx < static_cast<int>(state.response->content.size()) &&
                idx < static_cast<int>(state.partial_json.size())) {
                if (auto* tu = std::get_if<ToolUseBlock>(&state.response->content[idx])) {
                    const std::string& pj = state.partial_json[idx];
                    if (!pj.empty()) {
                        try { tu->input = nlohmann::json::parse(pj); }
                        catch (...) { tu->input = nlohmann::json::object(); }
                    }
                }
            }
        }
        else if constexpr (std::is_same_v<T, event::MessageDelta>) {
            state.response->stop_reason = e.stop_reason;
            state.response->usage.output_tokens = e.usage.output_tokens;
        }
        else if constexpr (std::is_same_v<T, event::Error>) {
            state.has_error = true;
            state.error_msg = e.message;
        }
        else {
            // Ping, MessageStop — nothing to do
        }
    }, ev);
}

// ── Main streaming request ────────────────────────────────────────────────────
ApiResponse ClaudeClient::streamRequest(const ApiRequest& req, const StreamCallback& onEvent) {
    CURL* curl = curl_easy_init();
    if (!curl) throw ApiError("Failed to initialise libcurl");

    ApiResponse response;
    StreamState state;
    state.onEvent  = onEvent;
    state.response = &response;

    std::string body = req.toJson().dump();

    // We use a lambda-captured write function stored in StreamState
    // because CURL needs a C function pointer. We store a pointer to state
    // in the userdata and parse lines in a wrapper.

    struct WriteCtx {
        StreamState*   state;
        ClaudeClient*  client;
    } ctx{ &state, this };

    auto writeFunc = [](char* ptr, size_t size, size_t nmemb, void* ud) -> size_t {
        auto* c   = reinterpret_cast<WriteCtx*>(ud);
        size_t n  = size * nmemb;
        c->state->buffer.append(ptr, n);

        std::string& buf = c->state->buffer;
        size_t pos = 0;
        while (true) {
            size_t nl = buf.find('\n', pos);
            if (nl == std::string::npos) break;
            std::string line = buf.substr(pos, nl - pos);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            c->client->processLine(line, *c->state);
            pos = nl + 1;
        }
        buf.erase(0, pos);
        return n;
    };

    auto headerFunc = [](char* ptr, size_t size, size_t nitems, void* ud) -> size_t {
        auto* c = reinterpret_cast<WriteCtx*>(ud);
        std::string h(ptr, size * nitems);
        if (h.rfind("HTTP/", 0) == 0) {
            auto space = h.find(' ');
            if (space != std::string::npos) {
                try { c->state->http_code = std::stoi(h.substr(space + 1, 3)); } catch (...) {}
            }
        }
        return size * nitems;
    };

    // Build URL
    std::string url = cfg_.base_url + "/v1/messages";

    // Build headers
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("x-api-key: " + cfg_.api_key).c_str());
    headers = curl_slist_append(headers, ("anthropic-version: " + cfg_.api_version).c_str());
    headers = curl_slist_append(headers, "content-type: application/json");
    headers = curl_slist_append(headers, "anthropic-beta: tools-2024-04-04");
    if (cfg_.computer_use_beta) {
        headers = curl_slist_append(headers, "anthropic-beta: computer-use-2024-10-22");
    }
    headers = curl_slist_append(headers, "Accept: text/event-stream");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, +writeFunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, +headerFunc);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(cfg_.timeout_seconds));
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        throw ApiError(std::string("CURL error: ") + curl_easy_strerror(res));
    }

    if (state.has_error) {
        throw ApiError("API error: " + state.error_msg, state.http_code);
    }

    if (state.http_code >= 400) {
        throw ApiError("HTTP error " + std::to_string(state.http_code), state.http_code);
    }

    return response;
}

ApiResponse ClaudeClient::request(const ApiRequest& req) {
    ApiRequest copy = req;
    copy.stream = false;
    return streamRequest(copy, nullptr);
}

} // namespace agentcpp::api
