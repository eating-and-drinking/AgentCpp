#pragma once
//
// Data tool group. Tabular analysis helpers.
//
//   CsvReadTool  — read a CSV file; return rows as JSON-ish text + small preview
//   CsvWriteTool — write JSON-shaped data to a CSV file
//   SqlQueryTool — run SQL against an in-memory or file-backed SQLite database
//                  (only compiled when AGENTCPP_WITH_SQLITE=ON, see CMake)
//   ChartTool    — render a basic bar/line chart to PNG (skeleton in PR2;
//                  full implementation lives in a follow-up PR — see comments)
//
#include "Tool.hpp"

namespace agentcpp::tools {

// ── CSV ──────────────────────────────────────────────────────────────────────
class CsvReadTool : public Tool {
public:
    std::string name()        const override { return "CsvRead"; }
    std::string category()    const override { return "data"; }
    std::string description() const override;
    json        inputSchema() const override;

    ToolCallResult execute(const json& input, const ToolContext& ctx) override;

    // Minimal RFC-4180 reader: handles "..." quoted fields with "" escapes,
    // bare commas as separators, and \r\n / \n line endings.
    // Public so other tools (e.g. SqlQueryTool's CSV->table init) can reuse it.
    static std::vector<std::vector<std::string>> parse(const std::string& text, char delim);

private:
    static constexpr int kPreviewRows = 20;
    static constexpr int kMaxRows     = 50000;
};

class CsvWriteTool : public Tool {
public:
    std::string name()        const override { return "CsvWrite"; }
    std::string category()    const override { return "data"; }
    std::string description() const override;
    json        inputSchema() const override;

    ToolCallResult execute(const json& input, const ToolContext& ctx) override;

private:
    static std::string escapeField(const std::string& v, char delim);
};

// ── SQL ──────────────────────────────────────────────────────────────────────
//
// Compiled only when AGENTCPP_WITH_SQLITE is defined (CMake flag). When the
// flag is off, the class definition is still present so the registry can
// report a clean error message at startup if a persona requests "data".
//
class SqlQueryTool : public Tool {
public:
    std::string name()        const override { return "SqlQuery"; }
    std::string category()    const override { return "data"; }
    std::string description() const override;
    json        inputSchema() const override;

    ToolCallResult execute(const json& input, const ToolContext& ctx) override;

    static bool sqliteAvailable();   // true iff compiled with sqlite3
};

// ── Chart (skeleton) ─────────────────────────────────────────────────────────
//
// Renders to a PNG; result returned via ToolCallResult::okWithImage so the
// model can see the chart directly. In PR2 this is a stub that delegates to
// gnuplot via shell-out (if installed), with a TODO for a native renderer.
//
class ChartTool : public Tool {
public:
    std::string name()        const override { return "Chart"; }
    std::string category()    const override { return "data"; }
    std::string description() const override;
    json        inputSchema() const override;

    ToolCallResult execute(const json& input, const ToolContext& ctx) override;

private:
    static bool gnuplotAvailable();
};

} // namespace agentcpp::tools
