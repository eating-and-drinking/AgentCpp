#pragma once
#include "Types.hpp"
#include <string>
#include <vector>
#include <functional>
#include <stdexcept>

namespace agentcpp::api {

// Exception thrown when the API returns an error
struct ApiError : std::runtime_error {
    int         http_code;
    std::string error_type;
    explicit ApiError(std::string msg, int code = 0, std::string type = "")
        : std::runtime_error(std::move(msg))
        , http_code(code)
        , error_type(std::move(type))
    {}
};

// Configuration for the Claude API client
struct ClientConfig {
    std::string api_key;
    std::string base_url   = "https://api.anthropic.com";
    std::string api_version = "2023-06-01";
    std::string default_model = "claude-opus-4-5";
    int         timeout_seconds = 300;
    bool        computer_use_beta = false;  // adds anthropic-beta: computer-use-2024-10-22
};

// Streaming Claude API client using libcurl
class ClaudeClient {
public:
    explicit ClaudeClient(ClientConfig cfg);
    ~ClaudeClient();

    // Send a streaming request. Calls onEvent for each SSE event.
    // Blocks until the stream is complete or an error occurs.
    // Returns the completed response assembled from stream events.
    ApiResponse streamRequest(
        const ApiRequest&     req,
        const StreamCallback& onEvent = nullptr
    );

    // Non-streaming variant (collects full response)
    ApiResponse request(const ApiRequest& req);

    const ClientConfig& config() const { return cfg_; }

private:
    ClientConfig cfg_;

    // libcurl write callback state
    struct StreamState {
        std::string           buffer;        // incomplete line buffer
        StreamCallback        onEvent;
        ApiResponse*          response;      // filled as events arrive
        std::vector<std::string> partial_json; // indexed by content block index
        std::string           current_text;
        int                   http_code = 0;
        bool                  has_error = false;
        std::string           error_msg;
    };

    void processLine(const std::string& line, StreamState& state);
    void applyEvent(const StreamEvent& ev, StreamState& state);
};

} // namespace agentcpp::api
