#include <mcp/McpManager.hpp>
#include <utils/Logger.hpp>
#include <fstream>
#include <sstream>
#include <cstdlib>

namespace agentcpp::mcp {

namespace fs = std::filesystem;

fs::path McpManager::defaultConfigPath() {
    if (const char* v = std::getenv("AGENTCPP_MCP_CONFIG"); v && *v) {
        return fs::path(v);
    }
#ifdef _WIN32
    if (const char* a = std::getenv("APPDATA"); a && *a) {
        return fs::path(a) / "agentcpp" / "mcp.json";
    }
    if (const char* u = std::getenv("USERPROFILE"); u && *u) {
        return fs::path(u) / ".agentcpp" / "mcp.json";
    }
#else
    if (const char* h = std::getenv("HOME"); h && *h) {
        return fs::path(h) / ".agentcpp" / "mcp.json";
    }
#endif
    return fs::path(".agentcpp") / "mcp.json";
}

int McpManager::loadConfig(const fs::path& path) {
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        LOG_DEBUG("mcp: config not found at " + path.string() + " (skipping)");
        return 0;
    }
    std::ifstream f(path);
    if (!f) {
        LOG_WARN("mcp: cannot open " + path.string());
        return 0;
    }
    std::ostringstream ss; ss << f.rdbuf();
    json cfg_json;
    try {
        cfg_json = json::parse(ss.str(), nullptr, true, true); // allow comments
    } catch (const std::exception& e) {
        LOG_WARN(std::string("mcp: JSON parse error: ") + e.what());
        return 0;
    }

    if (!cfg_json.contains("mcpServers") || !cfg_json["mcpServers"].is_object()) {
        LOG_DEBUG("mcp: no `mcpServers` object in " + path.string());
        return 0;
    }

    int started = 0;
    for (auto it = cfg_json["mcpServers"].begin(); it != cfg_json["mcpServers"].end(); ++it) {
        const std::string& id = it.key();
        const json& spec      = it.value();
        if (!spec.is_object()) continue;
        if (spec.value("disabled", false)) {
            LOG_INFO("mcp: server '" + id + "' disabled, skipping");
            continue;
        }

        ServerConfig sc;
        sc.id      = id;
        sc.command = spec.value("command", "");
        if (sc.command.empty()) {
            LOG_WARN("mcp: server '" + id + "' has no `command`, skipping");
            continue;
        }
        if (spec.contains("args") && spec["args"].is_array()) {
            for (const auto& a : spec["args"]) {
                if (a.is_string()) sc.args.push_back(a.get<std::string>());
            }
        }
        if (spec.contains("env") && spec["env"].is_object()) {
            for (auto eit = spec["env"].begin(); eit != spec["env"].end(); ++eit) {
                if (eit.value().is_string()) {
                    sc.env[eit.key()] = eit.value().get<std::string>();
                }
            }
        }

        auto client = std::make_unique<McpClient>(std::move(sc));
        std::string err = client->start();
        if (!err.empty()) {
            LOG_WARN("mcp: server '" + id + "' failed: " + err);
            continue;
        }

        LOG_INFO("mcp: started '" + id + "' with " +
                 std::to_string(client->tools().size()) + " tool(s)");

        for (const auto& t : client->tools()) {
            ResolvedTool rt;
            rt.prefixed_name = id + "__" + t.name;
            rt.info          = t;
            rt.client        = client.get();
            tools_.push_back(std::move(rt));
        }
        clients_.push_back(std::move(client));
        ++started;
    }
    return started;
}

} // namespace agentcpp::mcp
