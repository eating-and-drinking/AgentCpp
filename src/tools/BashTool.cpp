#include <tools/BashTool.hpp>
#include <utils/StringUtils.hpp>
#include <utils/Logger.hpp>
#include <array>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <thread>
#include <chrono>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <poll.h>

namespace agentcpp::tools {

std::string BashTool::description() const {
    return
        "Runs a shell command in bash and returns the combined stdout/stderr output. "
        "Use for running tests, build commands, file operations, git commands, etc. "
        "Commands run in the current working directory. "
        "The timeout parameter controls how long to wait (default 120 seconds, max 600). "
        "Long-running processes should be run in the background with & and nohup.";
}

json BashTool::inputSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"command", {
                {"type", "string"},
                {"description", "The bash command to execute."}
            }},
            {"timeout", {
                {"type", "integer"},
                {"description", "Timeout in milliseconds (default 120000, max 600000)."},
                {"default", 120000}
            }},
            {"description", {
                {"type", "string"},
                {"description", "Short description of what this command does (shown in UI)."}
            }}
        }},
        {"required", json::array({"command"})}
    };
}

ToolCallResult BashTool::execute(const json& input, const ToolContext& ctx) {
    std::string command  = input.value("command", "");
    int timeout_ms       = input.value("timeout", 120000);
    timeout_ms = std::min(timeout_ms, 600000);

    if (command.empty()) {
        return ToolCallResult::error("No command provided");
    }

    // Permission check for destructive commands
    if (!ctx.auto_approve && requiresPermission(command)) {
        if (ctx.askPermission) {
            bool ok = ctx.askPermission("Run command: " + command);
            if (!ok) return ToolCallResult::error("User denied permission to run command");
        }
    }

    LOG_INFO("BashTool: " + command);

    auto result = runCommand(command, timeout_ms, ctx.cwd);

    std::string output;
    if (!result.stdout_output.empty()) output += result.stdout_output;
    if (!result.stderr_output.empty()) {
        if (!output.empty()) output += "\n";
        output += result.stderr_output;
    }
    if (output.empty()) output = "(no output)";

    // Truncate large outputs
    constexpr size_t MAX_OUTPUT = 100 * 1024; // 100KB
    if (output.size() > MAX_OUTPUT) {
        output = output.substr(0, MAX_OUTPUT) + "\n... (output truncated)";
    }

    bool is_error = (result.exit_code != 0) || result.timed_out;
    if (result.timed_out) {
        output += "\n\n[Command timed out after " + std::to_string(timeout_ms / 1000) + "s]";
    } else if (result.exit_code != 0) {
        output += "\n\n[Exit code: " + std::to_string(result.exit_code) + "]";
    }

    return { output, is_error };
}

BashTool::ExecResult BashTool::runCommand(
    const std::string& command,
    int timeout_ms,
    const std::filesystem::path& cwd
) {
    ExecResult result;

    // Set up pipes for stdout and stderr
    int stdout_pipe[2], stderr_pipe[2];
    if (pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
        result.stdout_output = "Failed to create pipes";
        result.exit_code = -1;
        return result;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        close(stderr_pipe[0]); close(stderr_pipe[1]);
        result.stdout_output = "Failed to fork";
        result.exit_code = -1;
        return result;
    }

    if (pid == 0) {
        // Child process
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);

        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);

        close(stdout_pipe[1]);
        close(stderr_pipe[1]);

        if (!cwd.empty()) {
            chdir(cwd.c_str());
        }

        execl("/bin/bash", "bash", "-c", command.c_str(), nullptr);
        _exit(127);
    }

    // Parent process
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    // Non-blocking reads
    fcntl(stdout_pipe[0], F_SETFL, O_NONBLOCK);
    fcntl(stderr_pipe[0], F_SETFL, O_NONBLOCK);

    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeout_ms);

    std::string stdout_buf, stderr_buf;
    bool stdout_done = false, stderr_done = false;

    while (!stdout_done || !stderr_done) {
        auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            kill(pid, SIGKILL);
            result.timed_out = true;
            break;
        }

        int remaining_ms = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count()
        );

        struct pollfd fds[2];
        int nfds = 0;
        if (!stdout_done) { fds[nfds] = { stdout_pipe[0], POLLIN, 0 }; nfds++; }
        if (!stderr_done) { fds[nfds] = { stderr_pipe[0], POLLIN, 0 }; nfds++; }

        int ret = poll(fds, nfds, std::min(remaining_ms, 100));
        if (ret < 0) break;

        char buf[4096];
        int fi = 0;
        if (!stdout_done) {
            if (fds[fi].revents & POLLIN) {
                ssize_t n = read(stdout_pipe[0], buf, sizeof(buf));
                if (n > 0) stdout_buf.append(buf, n);
                else if (n == 0 || (n < 0 && errno != EAGAIN)) stdout_done = true;
            } else if (fds[fi].revents & (POLLHUP | POLLERR)) {
                stdout_done = true;
            }
            fi++;
        }
        if (!stderr_done) {
            if (fds[fi].revents & POLLIN) {
                ssize_t n = read(stderr_pipe[0], buf, sizeof(buf));
                if (n > 0) stderr_buf.append(buf, n);
                else if (n == 0 || (n < 0 && errno != EAGAIN)) stderr_done = true;
            } else if (fds[fi].revents & (POLLHUP | POLLERR)) {
                stderr_done = true;
            }
        }

        // Check if process exited
        int status;
        pid_t w = waitpid(pid, &status, WNOHANG);
        if (w == pid) {
            // Drain remaining
            while (true) {
                ssize_t n = read(stdout_pipe[0], buf, sizeof(buf));
                if (n <= 0) break;
                stdout_buf.append(buf, n);
            }
            while (true) {
                ssize_t n = read(stderr_pipe[0], buf, sizeof(buf));
                if (n <= 0) break;
                stderr_buf.append(buf, n);
            }
            result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            stdout_done = stderr_done = true;
        }
    }

    close(stdout_pipe[0]);
    close(stderr_pipe[0]);

    if (!result.timed_out) {
        int status;
        waitpid(pid, &status, 0);
        result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }

    result.stdout_output = std::move(stdout_buf);
    result.stderr_output = std::move(stderr_buf);
    return result;
}

bool BashTool::requiresPermission(const std::string& command) const {
    // Patterns that are potentially destructive and warrant a prompt
    static const std::vector<std::string> dangerous = {
        "rm ", "rm\t", "rmdir", "mkfs", "dd ", "format",
        "> /", "chmod 777", "sudo rm", "sudo dd",
        ":(){:|:&};:", // fork bomb
    };
    for (const auto& d : dangerous) {
        if (command.find(d) != std::string::npos) return true;
    }
    return false;
}

bool BashTool::isDestructiveCommand(const std::string& command) const {
    return requiresPermission(command);
}

} // namespace agentcpp::tools
