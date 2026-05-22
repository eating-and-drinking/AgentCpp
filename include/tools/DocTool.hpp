#pragma once
//
// Doc tool group. Read/write Office and PDF formats by shell-out to the
// Python scripts shipped with the `docx`, `xlsx`, `pptx`, `pdf` skills.
//
// Discovery rule:
//   For tool `DocxRead`, look for an executable script at
//     <skills_dir>/docx/scripts/read.py     (one of the configured skill roots)
//   Invoked as:  python3 <script> <args...>
//
// If python3 isn't available or no matching script is found, the tool
// returns a clear error at execute() time and the registry still surfaces it
// (so the user can see what went wrong and install dependencies).
//
#include "Tool.hpp"
#include <skills/SkillRegistry.hpp>

namespace agentcpp::tools {

// Shared helper to run a skill script.  Public so all six doc tools below
// can reuse the same shell-out plumbing without duplication.
class SkillScriptRunner {
public:
    explicit SkillScriptRunner(const agentcpp::skills::SkillRegistry* reg)
        : skills_(reg) {}

    // Look up <skill_id>/scripts/<script_name>.py from any registered skill
    // root. Returns empty path if not found.
    std::filesystem::path locate(const std::string& skill_id,
                                 const std::string& script_name) const;

    // Run python3 <script> <args...>. Captures stdout + stderr separately.
    // `stdin_text` is piped to the child if non-empty.
    struct Result {
        int          exit_code = 0;
        std::string  stdout_text;
        std::string  stderr_text;
        bool         missing_python = false;
    };
    static Result run(const std::filesystem::path& script,
                      const std::vector<std::string>& args,
                      const std::string& stdin_text = {});

    static bool pythonAvailable();   // cached after first call

private:
    const agentcpp::skills::SkillRegistry* skills_;
};

// ── One header struct per (format × direction) pair ──────────────────────────

class DocxReadTool : public Tool {
public:
    explicit DocxReadTool(const agentcpp::skills::SkillRegistry* r) : runner_(r) {}
    std::string name()        const override { return "DocxRead"; }
    std::string category()    const override { return "doc"; }
    std::string description() const override;
    json        inputSchema() const override;
    ToolCallResult execute(const json& input, const ToolContext& ctx) override;
private:
    SkillScriptRunner runner_;
};

class DocxWriteTool : public Tool {
public:
    explicit DocxWriteTool(const agentcpp::skills::SkillRegistry* r) : runner_(r) {}
    std::string name()        const override { return "DocxWrite"; }
    std::string category()    const override { return "doc"; }
    std::string description() const override;
    json        inputSchema() const override;
    ToolCallResult execute(const json& input, const ToolContext& ctx) override;
private:
    SkillScriptRunner runner_;
};

class PdfReadTool : public Tool {
public:
    explicit PdfReadTool(const agentcpp::skills::SkillRegistry* r) : runner_(r) {}
    std::string name()        const override { return "PdfRead"; }
    std::string category()    const override { return "doc"; }
    std::string description() const override;
    json        inputSchema() const override;
    ToolCallResult execute(const json& input, const ToolContext& ctx) override;
private:
    SkillScriptRunner runner_;
};

class XlsxReadTool : public Tool {
public:
    explicit XlsxReadTool(const agentcpp::skills::SkillRegistry* r) : runner_(r) {}
    std::string name()        const override { return "XlsxRead"; }
    std::string category()    const override { return "doc"; }
    std::string description() const override;
    json        inputSchema() const override;
    ToolCallResult execute(const json& input, const ToolContext& ctx) override;
private:
    SkillScriptRunner runner_;
};

class XlsxWriteTool : public Tool {
public:
    explicit XlsxWriteTool(const agentcpp::skills::SkillRegistry* r) : runner_(r) {}
    std::string name()        const override { return "XlsxWrite"; }
    std::string category()    const override { return "doc"; }
    std::string description() const override;
    json        inputSchema() const override;
    ToolCallResult execute(const json& input, const ToolContext& ctx) override;
private:
    SkillScriptRunner runner_;
};

class PptxReadTool : public Tool {
public:
    explicit PptxReadTool(const agentcpp::skills::SkillRegistry* r) : runner_(r) {}
    std::string name()        const override { return "PptxRead"; }
    std::string category()    const override { return "doc"; }
    std::string description() const override;
    json        inputSchema() const override;
    ToolCallResult execute(const json& input, const ToolContext& ctx) override;
private:
    SkillScriptRunner runner_;
};

class PptxWriteTool : public Tool {
public:
    explicit PptxWriteTool(const agentcpp::skills::SkillRegistry* r) : runner_(r) {}
    std::string name()        const override { return "PptxWrite"; }
    std::string category()    const override { return "doc"; }
    std::string description() const override;
    json        inputSchema() const override;
    ToolCallResult execute(const json& input, const ToolContext& ctx) override;
private:
    SkillScriptRunner runner_;
};

} // namespace agentcpp::tools
