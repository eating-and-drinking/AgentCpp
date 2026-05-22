#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <optional>

namespace agentcpp::mcp {

using json = nlohmann::json;

// JSON-RPC 2.0 request envelope. `id` may be absent for notifications.
struct Request {
    int                 id        = 0;
    bool                is_notify = false;
    std::string         method;
    json                params    = json::object();

    json toJson() const {
        json j = {{"jsonrpc", "2.0"}, {"method", method}, {"params", params}};
        if (!is_notify) j["id"] = id;
        return j;
    }
};

// JSON-RPC 2.0 response envelope. Exactly one of `result` / `error` is set.
struct Response {
    int                 id        = 0;
    bool                is_error  = false;
    json                result    = json::object();
    int                 err_code  = 0;
    std::string         err_msg;
};

inline Response parseResponse(const json& j) {
    Response r;
    if (j.contains("id") && j["id"].is_number_integer()) r.id = j["id"].get<int>();
    if (j.contains("error")) {
        r.is_error = true;
        if (j["error"].is_object()) {
            r.err_code = j["error"].value("code", 0);
            r.err_msg  = j["error"].value("message", "");
        }
    } else if (j.contains("result")) {
        r.result = j["result"];
    }
    return r;
}

} // namespace agentcpp::mcp
