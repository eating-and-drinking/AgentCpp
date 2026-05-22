#include <tools/DataTool.hpp>
#include <utils/Logger.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#ifdef AGENTCPP_WITH_SQLITE
#  include <sqlite3.h>
#endif

namespace agentcpp::tools {

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
// CsvReadTool
// ─────────────────────────────────────────────────────────────────────────────
std::string CsvReadTool::description() const {
    return "Read a CSV/TSV file and return a structured preview: schema "
           "(column names + inferred sample type), row count, and the first "
           "20 rows. Supports quoted fields. Use the schema before doing "
           "further analysis.";
}

nlohmann::json CsvReadTool::inputSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"path",   { {"type","string"}, {"description","Path to a .csv or .tsv file"} }},
            {"delim",  { {"type","string"}, {"description","Field delimiter (default ',' or '\\t' for .tsv)"} }},
            {"header", { {"type","boolean"}, {"description","First row contains column names (default true)"} }}
        }},
        {"required", nlohmann::json::array({"path"})}
    };
}

// Minimal RFC-4180 reader. Handles "..." with "" escapes, CRLF/LF.
std::vector<std::vector<std::string>> CsvReadTool::parse(const std::string& text, char delim) {
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row;
    std::string field;
    bool in_quote = false;

    for (std::size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (in_quote) {
            if (c == '"') {
                if (i + 1 < text.size() && text[i+1] == '"') { field += '"'; ++i; }
                else in_quote = false;
            } else field += c;
        } else {
            if (c == '"') in_quote = true;
            else if (c == delim) { row.push_back(std::move(field)); field.clear(); }
            else if (c == '\n')  { row.push_back(std::move(field)); field.clear();
                                   rows.push_back(std::move(row));  row.clear(); }
            else if (c == '\r')  { /* eat — \n will land the row */ }
            else field += c;
        }
    }
    if (!field.empty() || !row.empty()) {
        row.push_back(std::move(field));
        rows.push_back(std::move(row));
    }
    return rows;
}

namespace {
// Crude type inference: return "int", "float", "bool", or "text".
std::string inferType(const std::string& s) {
    if (s.empty()) return "text";
    auto t = s;
    bool dot = false, digit = false, sign_ok = true;
    for (std::size_t i = 0; i < t.size(); ++i) {
        char c = t[i];
        if (c == '-' || c == '+') { if (!sign_ok) return "text"; }
        else if (c == '.')         { if (dot) return "text"; dot = true; }
        else if (std::isdigit((unsigned char)c)) { digit = true; }
        else return "text";
        sign_ok = false;
    }
    if (!digit) return "text";
    return dot ? "float" : "int";
}
} // anon

ToolCallResult CsvReadTool::execute(const nlohmann::json& input, const ToolContext& ctx) {
    std::string p = input.value("path", "");
    if (p.empty()) return ToolCallResult::error("path is required");
    fs::path path = fs::path(p).is_absolute() ? fs::path(p) : ctx.cwd / p;
    if (!fs::exists(path)) return ToolCallResult::error("file not found: " + path.string());

    char delim = ',';
    if (input.contains("delim") && !input["delim"].get<std::string>().empty()) {
        delim = input["delim"].get<std::string>()[0];
    } else if (path.extension() == ".tsv") {
        delim = '\t';
    }
    bool has_header = input.value("header", true);

    std::ifstream f(path);
    if (!f) return ToolCallResult::error("cannot open: " + path.string());
    std::stringstream buf; buf << f.rdbuf();
    auto rows = parse(buf.str(), delim);
    if (rows.empty()) return ToolCallResult::error("file is empty");
    if ((int)rows.size() > kMaxRows) {
        rows.resize(kMaxRows);   // hard cap; the preview keeps it readable
    }

    std::vector<std::string> headers;
    if (has_header) {
        headers = rows.front();
        rows.erase(rows.begin());
    } else {
        for (std::size_t i = 0; i < rows.front().size(); ++i)
            headers.push_back("col" + std::to_string(i));
    }

    // Infer type from first non-empty value per column
    std::vector<std::string> col_types(headers.size(), "text");
    for (std::size_t c = 0; c < headers.size(); ++c) {
        for (const auto& row : rows) {
            if (c < row.size() && !row[c].empty()) {
                col_types[c] = inferType(row[c]);
                break;
            }
        }
    }

    std::ostringstream out;
    out << "File: "    << path.string() << "\n"
        << "Rows: "    << rows.size()   << "  (preview shows up to " << kPreviewRows << ")\n"
        << "Schema:\n";
    for (std::size_t c = 0; c < headers.size(); ++c) {
        out << "  " << headers[c] << " : " << col_types[c] << "\n";
    }
    out << "\n";

    // Markdown table preview
    out << "| "; for (const auto& h : headers) out << h << " | "; out << "\n|";
    for (std::size_t c = 0; c < headers.size(); ++c) out << "---|";
    out << "\n";
    int shown = std::min<int>(kPreviewRows, (int)rows.size());
    for (int r = 0; r < shown; ++r) {
        out << "| ";
        for (std::size_t c = 0; c < headers.size(); ++c) {
            std::string v = (c < rows[r].size()) ? rows[r][c] : "";
            for (auto& ch : v) if (ch == '|') ch = '/';
            out << v << " | ";
        }
        out << "\n";
    }
    return ToolCallResult::ok(out.str());
}

// ─────────────────────────────────────────────────────────────────────────────
// CsvWriteTool
// ─────────────────────────────────────────────────────────────────────────────
std::string CsvWriteTool::description() const {
    return "Write a CSV file from a JSON object that has 'columns' (array of "
           "strings) and 'rows' (array of arrays, one per row). Overwrites the "
           "target file. Disabled in --read-only mode.";
}

nlohmann::json CsvWriteTool::inputSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"path",    { {"type","string"} }},
            {"columns", { {"type","array"}, {"items", {{"type","string"}}} }},
            {"rows",    { {"type","array"} }},
            {"delim",   { {"type","string"}, {"description","Default ',' (use '\\t' for TSV)"} }}
        }},
        {"required", nlohmann::json::array({"path","columns","rows"})}
    };
}

std::string CsvWriteTool::escapeField(const std::string& v, char delim) {
    bool need = false;
    for (char c : v) if (c == delim || c == '"' || c == '\n' || c == '\r') { need = true; break; }
    if (!need) return v;
    std::string out = "\"";
    for (char c : v) { if (c == '"') out += "\""; out += c; }
    out += "\"";
    return out;
}

ToolCallResult CsvWriteTool::execute(const nlohmann::json& input, const ToolContext& ctx) {
    if (ctx.read_only) return ToolCallResult::error("read-only mode: CsvWrite blocked");

    std::string p = input.value("path", "");
    if (p.empty()) return ToolCallResult::error("path is required");
    fs::path path = fs::path(p).is_absolute() ? fs::path(p) : ctx.cwd / p;

    char delim = ',';
    if (input.contains("delim") && !input["delim"].get<std::string>().empty())
        delim = input["delim"].get<std::string>()[0];

    auto cols = input.value("columns", nlohmann::json::array());
    auto rows = input.value("rows",    nlohmann::json::array());

    std::ofstream f(path, std::ios::trunc);
    if (!f) return ToolCallResult::error("cannot open for write: " + path.string());

    for (std::size_t i = 0; i < cols.size(); ++i) {
        if (i) f << delim;
        f << escapeField(cols[i].get<std::string>(), delim);
    }
    f << "\n";

    std::size_t n = 0;
    for (const auto& row : rows) {
        if (!row.is_array()) continue;
        for (std::size_t i = 0; i < row.size(); ++i) {
            if (i) f << delim;
            std::string v;
            if      (row[i].is_string()) v = row[i].get<std::string>();
            else if (row[i].is_null())   v = "";
            else                         v = row[i].dump();
            f << escapeField(v, delim);
        }
        f << "\n";
        ++n;
    }
    return ToolCallResult::ok("wrote " + std::to_string(n) + " rows to " + path.string());
}

// ─────────────────────────────────────────────────────────────────────────────
// SqlQueryTool
// ─────────────────────────────────────────────────────────────────────────────
bool SqlQueryTool::sqliteAvailable() {
#ifdef AGENTCPP_WITH_SQLITE
    return true;
#else
    return false;
#endif
}

std::string SqlQueryTool::description() const {
    if (!sqliteAvailable())
        return "Run SQL queries (SQLite). NOTE: this build was compiled "
               "without -DAGENTCPP_WITH_SQLITE=ON, so the tool is unavailable.";
    return "Run SQL queries against a SQLite database. Pass `path` for a file "
           "or omit it for an in-memory database. Use `init` to pre-load CSV "
           "files as tables. SELECTs return up to 1000 rows; DML rows-affected.";
}

nlohmann::json SqlQueryTool::inputSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"sql",  { {"type","string"} }},
            {"path", { {"type","string"}, {"description","SQLite file path; omit for in-memory"} }},
            {"init", { {"type","array"},  {"items", {{"type","object"}}},
                       {"description","Optional CSV-to-table setup: [{table,path}]"} }}
        }},
        {"required", nlohmann::json::array({"sql"})}
    };
}

#ifdef AGENTCPP_WITH_SQLITE
namespace {
struct SqliteHandle {
    sqlite3* db = nullptr;
    ~SqliteHandle() { if (db) sqlite3_close(db); }
};

int csvToTable(sqlite3* db, const std::string& table, const std::string& csv_path) {
    std::ifstream f(csv_path);
    if (!f) return -1;
    std::stringstream buf; buf << f.rdbuf();
    auto rows = CsvReadTool::parse(buf.str(), ',');
    if (rows.size() < 1) return -1;

    auto& headers = rows.front();
    std::string sql = "CREATE TABLE \"" + table + "\" (";
    for (std::size_t i = 0; i < headers.size(); ++i) {
        if (i) sql += ",";
        sql += "\"" + headers[i] + "\" TEXT";
    }
    sql += ");";
    char* err = nullptr;
    if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        if (err) sqlite3_free(err);
        return -1;
    }

    sqlite3_exec(db, "BEGIN;", nullptr, nullptr, nullptr);
    std::string ins = "INSERT INTO \"" + table + "\" VALUES (";
    for (std::size_t i = 0; i < headers.size(); ++i) { if (i) ins += ","; ins += "?"; }
    ins += ");";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, ins.c_str(), -1, &st, nullptr) != SQLITE_OK) return -1;
    int inserted = 0;
    for (std::size_t r = 1; r < rows.size(); ++r) {
        for (std::size_t c = 0; c < headers.size(); ++c) {
            std::string v = (c < rows[r].size()) ? rows[r][c] : "";
            sqlite3_bind_text(st, (int)c + 1, v.c_str(), -1, SQLITE_TRANSIENT);
        }
        if (sqlite3_step(st) == SQLITE_DONE) ++inserted;
        sqlite3_reset(st);
    }
    sqlite3_finalize(st);
    sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
    return inserted;
}
} // anon
#endif

ToolCallResult SqlQueryTool::execute(const nlohmann::json& input, const ToolContext& ctx) {
#ifndef AGENTCPP_WITH_SQLITE
    (void)input; (void)ctx;
    return ToolCallResult::error(
        "SqlQuery is unavailable: agentcpp was built without sqlite. "
        "Reconfigure with -DAGENTCPP_WITH_SQLITE=ON and ensure libsqlite3-dev is installed.");
#else
    std::string sql  = input.value("sql",  "");
    std::string path = input.value("path", "");
    if (sql.empty()) return ToolCallResult::error("sql is required");

    SqliteHandle h;
    int rc = sqlite3_open(path.empty() ? ":memory:" : path.c_str(), &h.db);
    if (rc != SQLITE_OK) {
        return ToolCallResult::error(std::string("sqlite open: ") + sqlite3_errmsg(h.db));
    }

    if (input.contains("init") && input["init"].is_array()) {
        for (const auto& entry : input["init"]) {
            std::string t = entry.value("table", "");
            std::string p = entry.value("path",  "");
            if (t.empty() || p.empty()) continue;
            fs::path src = fs::path(p).is_absolute() ? fs::path(p) : ctx.cwd / p;
            int n = csvToTable(h.db, t, src.string());
            if (n < 0) return ToolCallResult::error("init failed for table '" + t + "'");
        }
    }

    sqlite3_stmt* st = nullptr;
    rc = sqlite3_prepare_v2(h.db, sql.c_str(), -1, &st, nullptr);
    if (rc != SQLITE_OK) {
        return ToolCallResult::error(std::string("prepare: ") + sqlite3_errmsg(h.db));
    }

    std::ostringstream out;
    int cols = sqlite3_column_count(st);
    if (cols > 0) {
        out << "| ";
        for (int c = 0; c < cols; ++c) out << sqlite3_column_name(st, c) << " | ";
        out << "\n|";
        for (int c = 0; c < cols; ++c) out << "---|";
        out << "\n";
        int row = 0;
        while (sqlite3_step(st) == SQLITE_ROW && row < 1000) {
            out << "| ";
            for (int c = 0; c < cols; ++c) {
                const unsigned char* v = sqlite3_column_text(st, c);
                std::string s = v ? reinterpret_cast<const char*>(v) : "";
                for (auto& ch : s) if (ch == '|') ch = '/';
                out << s << " | ";
            }
            out << "\n";
            ++row;
        }
        out << "\n(" << row << " row" << (row == 1 ? "" : "s") << ")\n";
    } else {
        sqlite3_step(st);
        int changes = sqlite3_changes(h.db);
        out << "OK (" << changes << " row" << (changes == 1 ? "" : "s") << " affected)\n";
    }
    sqlite3_finalize(st);
    return ToolCallResult::ok(out.str());
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// ChartTool (skeleton)
// ─────────────────────────────────────────────────────────────────────────────
bool ChartTool::gnuplotAvailable() {
    // Cheap check: try to invoke `gnuplot -V` and see if it returns 0.
    return std::system("gnuplot -V >/dev/null 2>&1") == 0;
}

std::string ChartTool::description() const {
    if (!gnuplotAvailable())
        return "Render a basic chart to PNG. NOTE: gnuplot is not installed, "
               "so this tool is currently unavailable; install gnuplot to enable.";
    return "Render a basic bar/line chart to PNG. Inputs: 'kind' (bar|line), "
           "'title', 'labels' (x-axis), 'series' (array of {name, values}). "
           "Returns a saved PNG path; the model receives the image directly "
           "via the tool result.";
}

nlohmann::json ChartTool::inputSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"kind",   { {"type","string"}, {"enum", nlohmann::json::array({"bar","line"})} }},
            {"title",  { {"type","string"} }},
            {"labels", { {"type","array"}, {"items", {{"type","string"}}} }},
            {"series", { {"type","array"} }},
            {"path",   { {"type","string"}, {"description","Output PNG path (default ./chart.png)"} }}
        }},
        {"required", nlohmann::json::array({"kind","labels","series"})}
    };
}

ToolCallResult ChartTool::execute(const nlohmann::json& input, const ToolContext& ctx) {
    if (!gnuplotAvailable()) {
        return ToolCallResult::error(
            "gnuplot is not installed. Install it (apt install gnuplot / "
            "brew install gnuplot) to enable Chart.");
    }
    std::string kind   = input.value("kind", "bar");
    std::string title  = input.value("title", "");
    std::string outpng = input.value("path",  "chart.png");
    fs::path outpath = fs::path(outpng).is_absolute() ? fs::path(outpng) : ctx.cwd / outpng;

    auto labels = input["labels"];
    auto series = input["series"];

    // Write a temp data file: x label, then one column per series
    fs::path tmpdat = fs::temp_directory_path() / ("agentcpp_chart_" + std::to_string(std::rand()) + ".dat");
    {
        std::ofstream f(tmpdat);
        for (std::size_t i = 0; i < labels.size(); ++i) {
            f << "\"" << labels[i].get<std::string>() << "\"";
            for (const auto& s : series) {
                auto vs = s["values"];
                f << " " << (i < vs.size() ? vs[i].dump() : std::string("0"));
            }
            f << "\n";
        }
    }

    // Build a tiny gnuplot script
    std::ostringstream gp;
    gp << "set terminal pngcairo size 800,500 enhanced font 'sans,10'\n"
       << "set output '" << outpath.string() << "'\n"
       << "set title '"  << title << "'\n"
       << "set grid\n"
       << "set xtics rotate by -30\n"
       << "set style data " << (kind == "line" ? "lines" : "histograms") << "\n"
       << "set style fill solid 0.6 border -1\n"
       << "plot ";
    for (std::size_t i = 0; i < series.size(); ++i) {
        if (i) gp << ", ";
        std::string name = series[i].value("name", "series" + std::to_string(i));
        gp << "'" << tmpdat.string() << "' using " << (i + 2) << ":xtic(1) title '" << name << "'";
    }
    gp << "\n";

    fs::path tmpscript = fs::temp_directory_path() / ("agentcpp_chart_" + std::to_string(std::rand()) + ".gp");
    { std::ofstream f(tmpscript); f << gp.str(); }

    std::string cmd = "gnuplot '" + tmpscript.string() + "' 2>&1";
    int rc = std::system(cmd.c_str());
    std::error_code ec;
    fs::remove(tmpdat, ec);
    fs::remove(tmpscript, ec);

    if (rc != 0 || !fs::exists(outpath))
        return ToolCallResult::error("gnuplot failed (exit " + std::to_string(rc) + ")");

    // Read PNG and embed as image in tool result so model sees it
    std::ifstream png(outpath, std::ios::binary);
    std::stringstream b; b << png.rdbuf();
    std::string bytes = b.str();
    // base64 encode
    static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string enc;
    enc.reserve(((bytes.size() + 2) / 3) * 4);
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        unsigned a = (unsigned char)bytes[i];
        unsigned b1 = i + 1 < bytes.size() ? (unsigned char)bytes[i+1] : 0;
        unsigned c  = i + 2 < bytes.size() ? (unsigned char)bytes[i+2] : 0;
        unsigned trip = (a << 16) | (b1 << 8) | c;
        enc.push_back(tbl[(trip >> 18) & 0x3F]);
        enc.push_back(tbl[(trip >> 12) & 0x3F]);
        enc.push_back(i + 1 < bytes.size() ? tbl[(trip >> 6) & 0x3F] : '=');
        enc.push_back(i + 2 < bytes.size() ? tbl[trip & 0x3F]        : '=');
    }
    return ToolCallResult::okWithImage("chart saved to " + outpath.string(), enc, "image/png");
}

} // namespace agentcpp::tools
