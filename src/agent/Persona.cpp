#include <agent/Persona.hpp>
#include <utils/Logger.hpp>
#include <utils/StringUtils.hpp>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace agentcpp::agent {

namespace fs = std::filesystem;

// ── Persona member functions ─────────────────────────────────────────────────
std::string Persona::toSystemPromptSection() const {
    if (id.empty()) return {};
    std::ostringstream ss;
    ss << "# " << (display_name.empty() ? id : display_name) << "\n\n";
    if (!mission.empty()) ss << mission << "\n";
    if (!style.empty())   ss << "\n## Style\n\n" << style << "\n";
    return ss.str();
}

std::string Persona::oneLineSummary() const {
    std::string head = mission;
    auto nl = head.find('\n');
    if (nl != std::string::npos) head.erase(nl);
    if (head.size() > 80) head = head.substr(0, 77) + "...";
    return id + " — " + (display_name.empty() ? id : display_name)
                       + (head.empty() ? "" : "  | " + head);
}

// ── PersonaRegistry: builtins ────────────────────────────────────────────────
PersonaRegistry::PersonaRegistry() {
    registerBuiltins();
}

void PersonaRegistry::registerBuiltins() {
    // general — the default. Broad toolset, no special bias.
    registerPersona({
        /*.id*/            "general",
        /*.display_name*/  "General Agent",
        /*.mission*/
            "You are a capable general-purpose assistant. You can browse the web, "
            "read and write files, analyze data, work with documents, and run code. "
            "Decompose complex requests into concrete steps, use tools when they "
            "save you guesswork, and answer in the user's language.",
        /*.style*/
            "Be direct. Cite sources when you used the web. Show your work for math "
            "and code. Ask one clarifying question only when the request is genuinely "
            "ambiguous; otherwise pick a reasonable interpretation and proceed.",
        /*.toolsets*/      { "core", "web", "files", "doc", "data", "memory" },
        /*.default_skills*/{},
        /*.extras*/        nlohmann::json::object(),
    });

    // researcher — focused on information gathering and synthesis.
    registerPersona({
        "researcher",
        "Research Analyst",
        "You are a research analyst. Gather information from the web and from "
        "provided documents, synthesize it, and produce well-cited briefings. "
        "Prefer primary sources, triangulate claims across multiple sources, "
        "and surface uncertainty explicitly.",
        "Always cite sources inline as [n] with a numbered list at the end. "
        "Distinguish primary from secondary sources. Flag uncertainty with "
        "phrases like 'one source claims' vs 'consensus is'. Prefer recent "
        "sources for fast-moving topics; mark publication dates when relevant.",
        { "core", "web", "doc", "memory" },
        {},
        nlohmann::json{{"plan_by_default", true}},
    });

    // office — document production.
    registerPersona({
        "office",
        "Office Assistant",
        "You produce polished Word documents, spreadsheets, presentations, and "
        "PDFs from user requests. You confirm scope before generating long "
        "deliverables, and you preserve existing formatting when editing.",
        "Default to a professional tone. For multi-page deliverables, propose "
        "an outline first and wait for confirmation. Keep tables consistent. "
        "Never invent figures — ask the user for source data.",
        { "core", "files", "doc", "memory" },
        { "docx", "xlsx", "pptx", "pdf" },
        nlohmann::json{{"plan_by_default", true}},
    });

    // data — tabular analysis and visualization.
    registerPersona({
        "data",
        "Data Analyst",
        "You analyze tabular and structured data: load CSV, Parquet, or SQL "
        "tables, compute statistics, build models when asked, and produce "
        "charts. You explain methodology and treat reproducibility as a "
        "first-class concern.",
        "Show the schema and a sample before analysis. State assumptions "
        "(e.g. distribution, null handling) before computing. Prefer "
        "reproducible code over one-off answers — emit the SQL or Python "
        "you ran. Round intelligently; never report spurious precision.",
        { "core", "files", "data", "code", "memory" },
        {},
        nlohmann::json::object(),
    });

    // coder — the legacy behavior. Default for backward compatibility.
    registerPersona({
        "coder",
        "Coding Assistant",
        "You are an expert software engineer working in a command-line "
        "environment. You read code before changing it, run tests when "
        "relevant, and produce minimal, focused diffs.",
        "Be concise. Read before writing. Match the codebase's existing "
        "style. Run tests when changes are non-trivial. Never invent APIs — "
        "verify against actual source or docs.",
        { "core", "files", "code", "memory" },
        {},
        nlohmann::json::object(),
    });
}

void PersonaRegistry::registerPersona(Persona p) {
    auto it = std::find_if(personas_.begin(), personas_.end(),
                           [&](const Persona& x){ return x.id == p.id; });
    if (it == personas_.end()) personas_.push_back(std::move(p));
    else                       *it = std::move(p);
}

const Persona* PersonaRegistry::find(std::string_view id) const {
    for (const auto& p : personas_) if (p.id == id) return &p;
    return nullptr;
}

std::vector<Persona> PersonaRegistry::all() const { return personas_; }

// ── Default overlay directory ────────────────────────────────────────────────
fs::path PersonaRegistry::defaultPersonaDir() {
    if (const char* env = std::getenv("AGENTCPP_PERSONA_DIR"); env && *env) {
        return fs::path(env);
    }
#ifdef _WIN32
    if (const char* appdata = std::getenv("APPDATA"); appdata && *appdata) {
        return fs::path(appdata) / "agentcpp" / "personas";
    }
    return fs::path("agentcpp") / "personas";
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) {
        return fs::path(xdg) / "agentcpp" / "personas";
    }
    if (const char* home = std::getenv("HOME"); home && *home) {
        return fs::path(home) / ".agentcpp" / "personas";
    }
    return fs::path(".agentcpp") / "personas";
#endif
}

std::size_t PersonaRegistry::loadFromDir(const fs::path& dir) {
    std::error_code ec;
    if (dir.empty() || !fs::exists(dir, ec) || !fs::is_directory(dir, ec)) return 0;

    std::size_t added = 0;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        auto ext = entry.path().extension().string();
        if (ext != ".yaml" && ext != ".yml") continue;

        std::ifstream f(entry.path());
        if (!f) {
            LOG_WARN("persona overlay: cannot open " + entry.path().string());
            continue;
        }
        std::stringstream buf;
        buf << f.rdbuf();
        auto p = parsePersonaYaml(buf.str());
        if (!p || p->id.empty()) {
            LOG_WARN("persona overlay: failed to parse " + entry.path().string());
            continue;
        }
        LOG_INFO("persona overlay: " + p->id + " <- " + entry.path().string());
        registerPersona(std::move(*p));
        ++added;
    }
    return added;
}

// ── Minimal YAML subset parser ───────────────────────────────────────────────
//
// Supports exactly what our persona files use:
//   key: scalar              # bare or "double-quoted" or 'single-quoted'
//   key: |                   # block literal — keep newlines
//     line one
//     line two
//   key: >                   # block folded — collapse to single line
//     line one
//     line two
//   key: [a, "b c", d]       # flow sequence of scalars
//   key:
//     - item                 # block sequence
//     - "item two"
//
// Indentation is 2 spaces (matches our templates). Comments (#...) and blank
// lines are skipped. Unknown keys are ignored. Returns nullopt only if no `id`
// key is found.
//
namespace {

std::string stripComment(std::string s) {
    bool in_dq = false, in_sq = false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '"'  && !in_sq) in_dq = !in_dq;
        else if (c == '\'' && !in_dq) in_sq = !in_sq;
        else if (c == '#'  && !in_dq && !in_sq) { s.erase(i); break; }
    }
    return s;
}

std::string rtrim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    return s;
}

std::string ltrim(std::string s) {
    std::size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    return s.substr(i);
}

int leadingSpaces(const std::string& s) {
    int n = 0;
    while (n < (int)s.size() && s[n] == ' ') ++n;
    return n;
}

std::string unquote(std::string s) {
    if (s.size() >= 2 &&
        ((s.front() == '"' && s.back() == '"') ||
         (s.front() == '\'' && s.back() == '\''))) {
        return s.substr(1, s.size() - 2);
    }
    return s;
}

// Parse a flow sequence: "[a, b, \"c, d\", e]" -> {"a","b","c, d","e"}
std::vector<std::string> parseFlowSequence(std::string s) {
    std::vector<std::string> out;
    s = ltrim(rtrim(s));
    if (s.size() < 2 || s.front() != '[' || s.back() != ']') return out;
    s = s.substr(1, s.size() - 2);
    std::string cur;
    bool in_dq = false, in_sq = false;
    auto flush = [&]{
        std::string t = ltrim(rtrim(cur));
        if (!t.empty()) out.push_back(unquote(std::move(t)));
        cur.clear();
    };
    for (char c : s) {
        if (c == '"'  && !in_sq) { in_dq = !in_dq; cur += c; }
        else if (c == '\'' && !in_dq) { in_sq = !in_sq; cur += c; }
        else if (c == ','  && !in_dq && !in_sq) flush();
        else cur += c;
    }
    flush();
    return out;
}

void assignScalar(Persona& p, const std::string& key, std::string val) {
    val = unquote(ltrim(rtrim(std::move(val))));
    if      (key == "id")             p.id = val;
    else if (key == "display_name")   p.display_name = val;
    else if (key == "mission")        p.mission = val;
    else if (key == "style")          p.style = val;
}

void assignSequence(Persona& p, const std::string& key, std::vector<std::string> seq) {
    if      (key == "toolsets")       p.toolsets       = std::move(seq);
    else if (key == "default_skills") p.default_skills = std::move(seq);
}

} // anonymous namespace

std::optional<Persona> parsePersonaYaml(const std::string& yaml_text) {
    Persona p;
    std::vector<std::string> raw_lines;
    {
        std::stringstream ss(yaml_text);
        std::string line;
        while (std::getline(ss, line)) raw_lines.push_back(std::move(line));
    }

    auto isBlank = [](const std::string& s){
        for (char c : s) if (!std::isspace((unsigned char)c)) return false;
        return true;
    };

    std::size_t i = 0;
    while (i < raw_lines.size()) {
        std::string line = stripComment(raw_lines[i]);
        if (isBlank(line)) { ++i; continue; }

        // Top-level keys live at indent 0.
        if (leadingSpaces(line) != 0) { ++i; continue; }

        auto colon = line.find(':');
        if (colon == std::string::npos) { ++i; continue; }
        std::string key   = ltrim(rtrim(line.substr(0, colon)));
        std::string after = (colon + 1 < line.size()) ? line.substr(colon + 1) : "";
        std::string after_trimmed = ltrim(rtrim(after));

        // Case 1: "key: value" (inline scalar / flow sequence)
        if (!after_trimmed.empty() && after_trimmed.front() != '|' && after_trimmed.front() != '>') {
            if (after_trimmed.front() == '[') {
                assignSequence(p, key, parseFlowSequence(after_trimmed));
            } else {
                assignScalar(p, key, after_trimmed);
            }
            ++i;
            continue;
        }

        // Case 2: "key: |" or "key: >" — block scalar at greater indent
        if (after_trimmed == "|" || after_trimmed == ">") {
            const bool fold = (after_trimmed == ">");
            ++i;
            std::vector<std::string> block;
            int indent = -1;
            while (i < raw_lines.size()) {
                const std::string& bl = raw_lines[i];
                if (isBlank(bl)) { block.push_back(""); ++i; continue; }
                int ls = leadingSpaces(bl);
                if (ls == 0) break;                  // dedented to top
                if (indent < 0) indent = ls;
                if (ls < indent) break;
                block.push_back(bl.substr(indent));
                ++i;
            }
            std::string joined;
            if (fold) {
                for (std::size_t k = 0; k < block.size(); ++k) {
                    if (k) joined += block[k].empty() ? "\n" : " ";
                    joined += block[k];
                }
            } else {
                for (std::size_t k = 0; k < block.size(); ++k) {
                    if (k) joined += "\n";
                    joined += block[k];
                }
            }
            assignScalar(p, key, joined);
            continue;
        }

        // Case 3: "key:" with nothing after, followed by block sequence
        if (after_trimmed.empty()) {
            ++i;
            std::vector<std::string> seq;
            while (i < raw_lines.size()) {
                std::string bl = stripComment(raw_lines[i]);
                if (isBlank(bl)) { ++i; continue; }
                int ls = leadingSpaces(bl);
                if (ls == 0) break;
                std::string trimmed = ltrim(bl);
                if (trimmed.size() < 2 || trimmed[0] != '-') break;
                std::string item = ltrim(rtrim(trimmed.substr(1)));
                seq.push_back(unquote(std::move(item)));
                ++i;
            }
            assignSequence(p, key, std::move(seq));
            continue;
        }

        ++i;
    }

    if (p.id.empty()) return std::nullopt;
    if (p.display_name.empty()) p.display_name = p.id;
    if (p.extras.is_null())     p.extras = nlohmann::json::object();
    return p;
}

} // namespace agentcpp::agent
