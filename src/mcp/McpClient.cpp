#include <mcp/McpClient.hpp>
#include <utils/Logger.hpp>
#include <utils/StringUtils.hpp>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;

namespace agentcpp::mcp {

namespace {

// Build a NULL-terminated argv from cfg.command + cfg.args.
std::vector<char*> buildArgv(const ServerConfig& cfg, std::vector<std::string>& storage) {
    storage.clear();
    storage.push_back(cfg.command);
    for (const auto& a : cfg.args) storage.push_back(a);
    std::vector<char*> out;
    out.reserve(storage.size() + 1);
    for (auto& s : storage) out.push_back(s.data());
    out.push_back(nullptr);
    return out;
}

} // namespace

McpClient::McpClient(ServerConfig cfg) : cfg_(std::move(cfg)) {}

McpClient::~McpClient() { stop(); }

bool McpClient::isAlive() const {
    if (child_pid_ < 0) return false;
    int status = 0;
    pid_t r = waitpid(child_pid_, &status, WNOHANG);
    return r == 0;
}

std::string McpClient::start() {
    int in_pipe[2];  // parent -> child stdin
    int out_pipe[2]; // child stdout -> parent
    if (pipe(in_pipe) != 0 || pipe(out_pipe) != 0) return "pipe() failed";

    pid_t pid = fork();
    if (pid < 0) {
        ::close(in_pipe[0]); ::close(in_pipe[1]);
        ::close(out_pipe[0]); ::close(out_pipe[1]);
        return "fork() failed";
    }

    if (pid == 0) {
        // child
        dup2(in_pipe[0], STDIN_FILENO);
        dup2(out_pipe[1], STDOUT_FILENO);
        ::close(in_pipe[0]); ::close(in_pipe[1]);
        ::close(out_pipe[0]); ::close(out_pipe[1]);
        for (const auto& kv : cfg_.env) setenv(kv.first.c_str(), kv.second.c_str(), 1);
        std::vector<std::string> storage;
        auto argv = buildArgv(cfg_, storage);
        execvp(cfg_.command.c_str(), argv.data());
        // execvp only returns on failure
        _exit(127);
    }

    // parent
    ::close(in_pipe[0]);
    ::close(out_pipe[1]);
    in_fd_     = in_pipe[1];
    out_fd_    = out_pipe[0];
    child_pid_ = pid;
    // Set out_fd_ to non-blocking so readLine can use poll() reliably.
    int flags = fcntl(out_fd_, F_GETFL, 0);
    if (flags >= 0) fcntl(out_fd_, F_SETFL, flags | O_NONBLOCK);

    // initialize
    json init_params = {
        {"protocolVersion", "2024-11-05"},
        {"capabilities", json::object()},
        {"clientInfo", {{"name", "agentcpp"}, {"version", "1.0.0"}}}
    };
    auto resp = sendRequest("initialize", init_params, cfg_.init_timeout);
    if (resp.is_error) {
        std::string e = "initialize failed: " + resp.err_msg;
        stop();
        return e;
    }
    sendNotification("notifications/initialized", json::object());

    return loadTools();
}

std::string McpClient::loadTools() {
    auto resp = sendRequest("tools/list", json::object(), cfg_.init_timeout);
    if (resp.is_error) return "tools/list failed: " + resp.err_msg;
    if (!resp.result.contains("tools") || !resp.result["tools"].is_array()) {
        return "tools/list returned no `tools` array";
    }
    tools_.clear();
    for (const auto& t : resp.result["tools"]) {
        McpToolInfo info;
        info.name         = t.value("name", "");
        info.description  = t.value("description", "");
        info.input_schema = t.value("inputSchema", json::object());
        if (!info.name.empty()) tools_.push_back(std::move(info));
    }
    return "";
}

std::string McpClient::callTool(const std::string& name, const json& arguments, bool& is_error) {
    json params = {{"name", name}, {"arguments", arguments}};
    auto resp = sendRequest("tools/call", params, cfg_.call_timeout);
    if (resp.is_error) {
        is_error = true;
        return "MCP error " + std::to_string(resp.err_code) + ": " + resp.err_msg;
    }
    is_error = resp.result.value("isError", false);
    // The standard shape is { content: [ {type:"text", text:"..."}, ... ], isError: bool }
    std::string out;
    if (resp.result.contains("content") && resp.result["content"].is_array()) {
        for (const auto& c : resp.result["content"]) {
            if (c.value("type", "") == "text") {
                if (!out.empty()) out += "\n";
                out += c.value("text", "");
            }
        }
    } else {
        out = resp.result.dump();
    }
    return out;
}

void McpClient::stop() {
    if (in_fd_  >= 0) { ::close(in_fd_);  in_fd_  = -1; }
    if (out_fd_ >= 0) { ::close(out_fd_); out_fd_ = -1; }
    if (child_pid_ > 0) {
        int status = 0;
        for (int i = 0; i < 10; ++i) {
            if (waitpid(child_pid_, &status, WNOHANG) != 0) { child_pid_ = -1; return; }
            usleep(50 * 1000);
        }
        kill(child_pid_, SIGTERM);
        usleep(100 * 1000);
        if (waitpid(child_pid_, &status, WNOHANG) == 0) kill(child_pid_, SIGKILL);
        waitpid(child_pid_, &status, 0);
        child_pid_ = -1;
    }
}

bool McpClient::writeLine(const std::string& s) {
    std::string buf = s; buf.push_back('\n');
    const char* p = buf.data();
    std::size_t left = buf.size();
    while (left > 0) {
        ssize_t n = ::write(in_fd_, p, left);
        if (n < 0) { if (errno == EINTR) continue; return false; }
        p    += n;
        left -= static_cast<std::size_t>(n);
    }
    return true;
}

std::string McpClient::readLine(std::chrono::milliseconds timeout) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
        auto nl = read_buf_.find('\n');
        if (nl != std::string::npos) {
            std::string line = read_buf_.substr(0, nl);
            read_buf_.erase(0, nl + 1);
            return line;
        }
        auto now = std::chrono::steady_clock::now();
        if (now >= deadline) return "";
        int ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
        pollfd pfd{ out_fd_, POLLIN, 0 };
        int pr = poll(&pfd, 1, ms);
        if (pr <= 0) return "";
        char buf[4096];
        ssize_t n = ::read(out_fd_, buf, sizeof(buf));
        if (n <= 0) {
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) continue;
            return "";
        }
        read_buf_.append(buf, buf + n);
    }
}

Response McpClient::sendRequest(const std::string& method, const json& params, std::chrono::milliseconds timeout) {
    Request req; req.id = next_id_++; req.method = method; req.params = params;
    Response err; err.is_error = true; err.id = req.id;
    if (!writeLine(req.toJson().dump())) { err.err_msg = "write failed"; return err; }

    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
        auto now = std::chrono::steady_clock::now();
        if (now >= deadline) { err.err_msg = "timeout waiting for response to " + method; return err; }
        auto remain = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        std::string line = readLine(remain);
        if (line.empty()) { err.err_msg = "EOF or timeout on " + method; return err; }
        try {
            json j = json::parse(line);
            if (!j.contains("id")) continue; // notification from server, ignore
            auto resp = parseResponse(j);
            if (resp.id != req.id) continue;
            return resp;
        } catch (const std::exception& e) {
            LOG_DEBUG(std::string("mcp: parse failed: ") + e.what() + " line=" + line);
            continue;
        }
    }
}

void McpClient::sendNotification(const std::string& method, const json& params) {
    Request n; n.is_notify = true; n.method = method; n.params = params;
    writeLine(n.toJson().dump());
}

} // namespace agentcpp::mcp
