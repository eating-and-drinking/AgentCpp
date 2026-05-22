#include <tools/MemoryTool.hpp>
#include <utils/StringUtils.hpp>

#include <iomanip>
#include <sstream>

namespace agentcpp::tools {

namespace mem = agentcpp::memory;

namespace {

constexpr const char* kBankHelp =
    "Memory bank id (a string namespace). Defaults to \"default\". Use the "
    "same bank id across calls so retain and recall see the same memories.";

std::string bankOf(const json& j) {
    auto b = j.value("bank_id", std::string{});
    return b.empty() ? std::string(mem::kDefaultBankId) : b;
}

mem::FactType factTypeFromJson(const json& j) {
    auto s = j.value("fact_type", std::string("world"));
    return mem::factTypeFromString(s);
}

} // namespace

// ─── MemoryRetain ────────────────────────────────────────────────────────────

std::string MemoryRetainTool::description() const {
    return
        "Store a new memory in the agent's long-term memory bank using the "
        "hindsight-style retain pipeline. The content is split into facts, "
        "entities are resolved, and links are created automatically. "
        "Use fact_type='world' for general knowledge, 'experience' for "
        "first-person events, 'observation' for synthesized notes.";
}

json MemoryRetainTool::inputSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"bank_id", {{"type", "string"}, {"description", kBankHelp}}},
            {"content", {{"type", "string"}, {"description", "The text to remember (required)."}}},
            {"context", {{"type", "string"}, {"description", "Optional context — where/why this matters."}}},
            {"fact_type", {{"type", "string"},
                           {"enum", json::array({"world", "experience", "observation"})},
                           {"description", "Classification of the fact (default: world)."}}},
            {"tags", {{"type", "array"}, {"items", {{"type", "string"}}},
                     {"description", "Visibility-scope tags."}}},
            {"entities", {{"type", "array"}, {"items", {{"type", "string"}}},
                         {"description", "Pre-known entity names referenced by this content."}}},
        }},
        {"required", json::array({"content"})}
    };
}

ToolCallResult MemoryRetainTool::execute(const json& input, const ToolContext& ctx) {
    if (!engine_.isReady()) return ToolCallResult::error("memory engine not initialised");
    if (ctx.read_only)      return ToolCallResult::error("read-only mode: MemoryRetain is disabled");

    auto content_str = input.value("content", std::string{});
    if (content_str.empty()) return ToolCallResult::error("'content' is required");

    mem::RetainContent c;
    c.content            = content_str;
    c.context            = input.value("context", std::string{});
    c.fact_type_override = factTypeFromJson(input);
    if (input.contains("tags") && input["tags"].is_array()) {
        for (auto& t : input["tags"]) {
            if (t.is_string()) c.tags.push_back(t.get<std::string>());
        }
    }
    if (input.contains("entities") && input["entities"].is_array()) {
        for (auto& e : input["entities"]) {
            if (e.is_string()) c.entities.push_back({e.get<std::string>(), ""});
        }
    }

    auto bank   = bankOf(input);
    auto result = engine_.retain(bank, c);

    std::ostringstream out;
    out << "Retained " << result.facts_extracted << " fact(s) into bank '" << bank << "'\n";
    out << "Units created: " << result.unit_ids.size() << "\n";
    out << "Entities seen: " << result.entity_ids.size() << "\n";
    out << "Links created: " << result.links_created << "\n";
    if (!result.unit_ids.empty()) {
        out << "First unit id: " << result.unit_ids.front();
    }
    return ToolCallResult::ok(out.str());
}

// ─── MemoryRecall ────────────────────────────────────────────────────────────

std::string MemoryRecallTool::description() const {
    return
        "Search the memory bank for facts relevant to a query, using the "
        "hindsight recall pipeline (BM25 + graph expansion + temporal + RRF "
        "fusion + heuristic rerank). Returns the top-k matching memory units.";
}

json MemoryRecallTool::inputSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"bank_id", {{"type", "string"}, {"description", kBankHelp}}},
            {"query",   {{"type", "string"}, {"description", "Natural-language query (required)."}}},
            {"k",       {{"type", "integer"}, {"description", "Max results to return (default 10)."}}},
            {"fact_type", {{"type", "array"}, {"items", {{"type", "string"},
                          {"enum", json::array({"world", "experience", "observation"})}}},
                          {"description", "Restrict to these fact types."}}},
            {"tags",   {{"type", "array"}, {"items", {{"type", "string"}}},
                       {"description", "Only return units that have any of these tags."}}},
        }},
        {"required", json::array({"query"})}
    };
}

ToolCallResult MemoryRecallTool::execute(const json& input, const ToolContext& /*ctx*/) {
    if (!engine_.isReady()) return ToolCallResult::error("memory engine not initialised");
    auto q_text = input.value("query", std::string{});
    if (q_text.empty()) return ToolCallResult::error("'query' is required");

    mem::RecallQuery q;
    q.query = q_text;
    q.k     = static_cast<std::size_t>(input.value("k", 10));
    if (input.contains("fact_type") && input["fact_type"].is_array()) {
        for (auto& t : input["fact_type"]) {
            if (t.is_string()) q.fact_type_filter.push_back(mem::factTypeFromString(t.get<std::string>()));
        }
    }
    if (input.contains("tags") && input["tags"].is_array()) {
        for (auto& t : input["tags"]) {
            if (t.is_string()) q.tags_any.push_back(t.get<std::string>());
        }
    }
    auto bank = bankOf(input);
    auto res  = engine_.recall(bank, q);

    std::ostringstream out;
    out << "Recall on bank '" << bank << "' for: \"" << q_text << "\"\n";
    out << "Results: " << res.results.size() << "\n\n";
    if (res.results.empty()) {
        out << "(no matches — try a different query or check whether anything has been retained yet)";
        return ToolCallResult::ok(out.str());
    }
    int rank = 1;
    for (const auto& sr : res.results) {
        const auto& r = sr.retrieval();
        out << "#" << rank++ << " [" << mem::factTypeToString(r.fact_type) << "] "
            << r.text << "\n";
        out << "    id=" << r.id
            << "  combined=" << std::fixed << std::setprecision(3) << sr.combined_score
            << "  rrf="      << std::setprecision(3) << sr.candidate.rrf_score
            << "\n";
        if (r.context && !r.context->empty()) {
            out << "    context: " << *r.context << "\n";
        }
    }
    return ToolCallResult::ok(out.str());
}

// ─── MemoryReflect ───────────────────────────────────────────────────────────

std::string MemoryReflectTool::description() const {
    return
        "Generate a structured reflection on a question using the hindsight "
        "reflect pipeline. Retrieves relevant memories, groups them by fact "
        "type (world / experience / observation), and emits a composed "
        "answer plus the supporting facts it was based on.";
}

json MemoryReflectTool::inputSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"bank_id", {{"type", "string"}, {"description", kBankHelp}}},
            {"query",   {{"type", "string"}, {"description", "The question to reflect on (required)."}}},
            {"context", {{"type", "string"}, {"description", "Extra context about the situation."}}},
            {"max_facts", {{"type", "integer"}, {"description", "Max supporting facts to surface (default 12)."}}},
        }},
        {"required", json::array({"query"})}
    };
}

ToolCallResult MemoryReflectTool::execute(const json& input, const ToolContext& /*ctx*/) {
    if (!engine_.isReady()) return ToolCallResult::error("memory engine not initialised");
    auto q_text = input.value("query", std::string{});
    if (q_text.empty()) return ToolCallResult::error("'query' is required");

    mem::ReflectQuery rq;
    rq.query     = q_text;
    rq.context   = input.value("context", std::string{});
    rq.max_facts = static_cast<std::size_t>(input.value("max_facts", 12));
    auto bank = bankOf(input);
    auto out  = engine_.reflect(bank, rq);
    return ToolCallResult::ok(out.text);
}

// ─── MemoryList ──────────────────────────────────────────────────────────────

std::string MemoryListTool::description() const {
    return
        "List memory units stored in a bank, optionally filtered by fact_type. "
        "Useful for getting a quick overview of what the agent remembers.";
}

json MemoryListTool::inputSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"bank_id",   {{"type", "string"}, {"description", kBankHelp}}},
            {"fact_type", {{"type", "string"},
                           {"enum", json::array({"world", "experience", "observation"})},
                           {"description", "Restrict to one fact type."}}},
            {"limit",     {{"type", "integer"}, {"description", "Max units to return (default 50)."}}},
        }},
    };
}

ToolCallResult MemoryListTool::execute(const json& input, const ToolContext& /*ctx*/) {
    if (!engine_.isReady()) return ToolCallResult::error("memory engine not initialised");
    auto bank  = bankOf(input);
    auto limit = static_cast<std::size_t>(input.value("limit", 50));

    std::optional<mem::FactType> filter;
    if (input.contains("fact_type") && input["fact_type"].is_string()) {
        filter = mem::factTypeFromString(input["fact_type"].get<std::string>());
    }
    auto units = engine_.listUnits(bank, filter, limit, 0);
    auto stats = engine_.getBankStats(bank);

    std::ostringstream out;
    out << "Bank '" << bank << "' — units=" << stats.units
        << " (world=" << stats.world
        << ", experience=" << stats.experience
        << ", observation=" << stats.observation << ")"
        << ", entities=" << stats.entities
        << ", links=" << stats.links << "\n";
    out << "Root: " << engine_.root().string() << "\n";
    if (units.empty()) {
        out << "\n(no units yet — call MemoryRetain to add some)\n";
        return ToolCallResult::ok(out.str());
    }
    out << "\nUnits (showing " << units.size() << "):\n";
    for (const auto& u : units) {
        out << "- [" << mem::factTypeToString(u.fact_type) << "] "
            << utils::truncate(u.text, 140) << "\n";
        out << "    id=" << u.id;
        if (!u.tags.empty()) {
            out << "  tags=";
            for (std::size_t i = 0; i < u.tags.size(); ++i) {
                if (i) out << ',';
                out << u.tags[i];
            }
        }
        out << "\n";
    }
    return ToolCallResult::ok(out.str());
}

} // namespace agentcpp::tools
