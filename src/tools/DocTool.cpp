#include <tools/DocTool.hpp>
#include <utils/Logger.hpp>
#include <nlohmann/json.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

namespace agentcpp::tools {

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
// SkillScriptRunner
// ─────────────────────────────────────────────────────────────────────────────
bool SkillScriptRunner::pythonAvailable() {
    static int cached = -1;
    if (cached < 0) cached = (std::system("python3 -V >/dev/null 2>&1") == 0) ? 1 : 0;
    return cached == 1;
}

fs::path SkillScriptRunner::locate(const std::string& skill_id,
                                   const std::string& script_name) const {
    if (!skills_) return {};
    const auto* skill = skills_->find(skill_id);
    if (!skill) return {};
    fs::path candidate = skill->dir / "scripts" / (script_name + ".py");
    if (fs::exists(candidate)) return candidate;
    // Also accept a top-level script (e.g. skill ships its only script at root)
    fs::path top = skill->dir / (script_name + ".py");
    if (fs::exists(top)) return top;
    return {};
}

SkillScriptRunner::Result SkillScriptRunner::run(
    const fs::path& script,
    const std::vector<std::string>& args,
    const std::string& stdin_text)
{
    Result r;
    if (!pythonAvailable()) {
        r.exit_code      = -1;
        r.missing_python = true;
        r.stderr_text    = "python3 not found in PATH";
        return r;
    }

    int out_pipe[2], err_pipe[2], in_pipe[2];
    if (pipe(out_pipe) != 0 || pipe(err_pipe) != 0 || pipe(in_pipe) != 0) {
        r.exit_code = -1; r.stderr_text = "pipe() failed"; return r;
    }

    pid_t pid = fork();
    if (pid < 0) {
        r.exit_code = -1; r.stderr_text = "fork() failed";
        for (int p : {out_pipe[0], out_pipe[1], err_pipe[0], err_pipe[1], in_pipe[0], in_pipe[1]})
            close(p);
        return r;
    }

    if (pid == 0) {
        // Child: wire pipes, exec python3
        close(out_pipe[0]); close(err_pipe[0]); close(in_pipe[1]);
        dup2(in_pipe[0],  STDIN_FILENO);   close(in_pipe[0]);
        dup2(out_pipe[1], STDOUT_FILENO);  close(out_pipe[1]);
        dup2(err_pipe[1], STDERR_FILENO);  close(err_pipe[1]);

        std::vector<char*> argv;
        std::string py("python3");
        std::string sc = script.string();
        argv.push_back(py.data());
        argv.push_back(sc.data());
        std::vector<std::string> store = args;  // keep memory alive
        for (auto& a : store) argv.push_back(a.data());
        argv.push_back(nullptr);
        execvp("python3", argv.data());
        _exit(127);
    }

    // Parent: write stdin, read stdout/stderr
    close(out_pipe[1]); close(err_pipe[1]); close(in_pipe[0]);

    if (!stdin_text.empty()) {
        ssize_t off = 0;
        while (off < (ssize_t)stdin_text.size()) {
            ssize_t w = write(in_pipe[1], stdin_text.data() + off, stdin_text.size() - off);
            if (w <= 0) break;
            off += w;
        }
    }
    close(in_pipe[1]);

    auto drain = [](int fd, std::string& dst) {
        char buf[4096];
        for (;;) {
            ssize_t n = read(fd, buf, sizeof(buf));
            if (n <= 0) break;
            dst.append(buf, buf + n);
            if (dst.size() > 1 * 1024 * 1024) break;   // 1 MB safety cap
        }
        close(fd);
    };
    drain(out_pipe[0], r.stdout_text);
    drain(err_pipe[0], r.stderr_text);

    int status = 0;
    waitpid(pid, &status, 0);
    r.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    if (r.exit_code == 127) r.missing_python = true;
    return r;
}

// ─────────────────────────────────────────────────────────────────────────────
// Shared dispatch: locate script -> run -> wrap as ToolCallResult
// ─────────────────────────────────────────────────────────────────────────────
namespace {

ToolCallResult dispatch(const SkillScriptRunner& runner,
                        const std::string& skill_id,
                        const std::string& script_name,
                        const std::vector<std::string>& args,
                        const std::string& stdin_text = {}) {
    auto script = runner.locate(skill_id, script_name);
    if (script.empty()) {
        return ToolCallResult::error(
            "Could not locate " + skill_id + "/scripts/" + script_name + ".py. "
            "Make sure the '" + skill_id + "' skill is installed and its "
            "scripts/ directory contains " + script_name + ".py.");
    }
    auto r = runner.run(script, args, stdin_text);
    if (r.missing_python) {
        return ToolCallResult::error("python3 is required but not available in PATH");
    }
    if (r.exit_code != 0) {
        std::ostringstream s;
        s << skill_id << "/" << script_name
          << " exited " << r.exit_code << "\n"
          << "stderr:\n" << r.stderr_text;
        return ToolCallResult::error(s.str());
    }
    return ToolCallResult::ok(r.stdout_text.empty() ? "(no output)" : r.stdout_text);
}

} // anon

// ─────────────────────────────────────────────────────────────────────────────
// Per-tool descriptions / schemas / dispatch
// Each tool maps:
//   <Format><Action>Tool  ->  skill="<format>", script="<action>"
// e.g. DocxReadTool -> skill="docx", script="read"
// The Python scripts are expected to honour --path / --output / etc. flags.
// ─────────────────────────────────────────────────────────────────────────────

namespace {
nlohmann::json pathOnlySchema(const char* desc) {
    return {
        {"type", "object"},
        {"properties", {{"path", {{"type","string"},{"description", desc}}}}},
        {"required", nlohmann::json::array({"path"})}
    };
}
nlohmann::json writeSchema(const char* desc) {
    return {
        {"type", "object"},
        {"properties", {
            {"path",    {{"type","string"},{"description", desc}}},
            {"content", {{"type","string"},{"description","Body content (markdown / JSON depending on script)"}}}
        }},
        {"required", nlohmann::json::array({"path","content"})}
    };
}
} // anon

// docx
std::string DocxReadTool::description() const  { return "Extract text from a .docx file via the docx skill's read.py."; }
nlohmann::json DocxReadTool::inputSchema() const { return pathOnlySchema("Path to a .docx file"); }
ToolCallResult DocxReadTool::execute(const nlohmann::json& input, const ToolContext& /*ctx*/) {
    return dispatch(runner_, "docx", "read", {"--path", input.value("path","")});
}
std::string DocxWriteTool::description() const  { return "Create a .docx file from markdown content via the docx skill's write.py."; }
nlohmann::json DocxWriteTool::inputSchema() const { return writeSchema("Target .docx path"); }
ToolCallResult DocxWriteTool::execute(const nlohmann::json& input, const ToolContext& ctx) {
    if (ctx.read_only) return ToolCallResult::error("read-only mode: DocxWrite blocked");
    return dispatch(runner_, "docx", "write",
                    {"--path", input.value("path","")},
                    input.value("content",""));
}

// pdf
std::string PdfReadTool::description() const  { return "Extract text from a PDF (and optionally OCR scanned pages) via the pdf skill."; }
nlohmann::json PdfReadTool::inputSchema() const { return pathOnlySchema("Path to a .pdf file"); }
ToolCallResult PdfReadTool::execute(const nlohmann::json& input, const ToolContext& /*ctx*/) {
    return dispatch(runner_, "pdf", "read", {"--path", input.value("path","")});
}

// xlsx
std::string XlsxReadTool::description() const  { return "Read an .xlsx workbook: list sheets and emit a preview of rows per sheet."; }
nlohmann::json XlsxReadTool::inputSchema() const { return pathOnlySchema("Path to an .xlsx file"); }
ToolCallResult XlsxReadTool::execute(const nlohmann::json& input, const ToolContext& /*ctx*/) {
    return dispatch(runner_, "xlsx", "read", {"--path", input.value("path","")});
}
std::string XlsxWriteTool::description() const  { return "Write an .xlsx workbook from a JSON spec (sheets, columns, rows)."; }
nlohmann::json XlsxWriteTool::inputSchema() const { return writeSchema("Target .xlsx path"); }
ToolCallResult XlsxWriteTool::execute(const nlohmann::json& input, const ToolContext& ctx) {
    if (ctx.read_only) return ToolCallResult::error("read-only mode: XlsxWrite blocked");
    return dispatch(runner_, "xlsx", "write",
                    {"--path", input.value("path","")},
                    input.value("content",""));
}

// pptx
std::string PptxReadTool::description() const  { return "Extract text and structure from a .pptx file."; }
nlohmann::json PptxReadTool::inputSchema() const { return pathOnlySchema("Path to a .pptx file"); }
ToolCallResult PptxReadTool::execute(const nlohmann::json& input, const ToolContext& /*ctx*/) {
    return dispatch(runner_, "pptx", "read", {"--path", input.value("path","")});
}
std::string PptxWriteTool::description() const  { return "Generate a .pptx deck from a structured outline."; }
nlohmann::json PptxWriteTool::inputSchema() const { return writeSchema("Target .pptx path"); }
ToolCallResult PptxWriteTool::execute(const nlohmann::json& input, const ToolContext& ctx) {
    if (ctx.read_only) return ToolCallResult::error("read-only mode: PptxWrite blocked");
    return dispatch(runner_, "pptx", "write",
                    {"--path", input.value("path","")},
                    input.value("content",""));
}

} // namespace agentcpp::tools
