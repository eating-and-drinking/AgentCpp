#include <memory/MemoryTypes.hpp>

#include <chrono>
#include <ctime>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>

namespace agentcpp::memory {

// ─── enum strings ─────────────────────────────────────────────────────────────

const char* factTypeToString(FactType t) {
    switch (t) {
        case FactType::World:       return "world";
        case FactType::Experience:  return "experience";
        case FactType::Observation: return "observation";
    }
    return "world";
}

FactType factTypeFromString(const std::string& s) {
    if (s == "world")       return FactType::World;
    if (s == "experience")  return FactType::Experience;
    if (s == "observation") return FactType::Observation;
    return FactType::World;
}

const char* linkTypeToString(LinkType t) {
    switch (t) {
        case LinkType::Temporal: return "temporal";
        case LinkType::Semantic: return "semantic";
        case LinkType::Entity:   return "entity";
        case LinkType::Causes:   return "causes";
        case LinkType::CausedBy: return "caused_by";
        case LinkType::Enables:  return "enables";
        case LinkType::Prevents: return "prevents";
    }
    return "entity";
}

LinkType linkTypeFromString(const std::string& s) {
    if (s == "temporal")  return LinkType::Temporal;
    if (s == "semantic")  return LinkType::Semantic;
    if (s == "entity")    return LinkType::Entity;
    if (s == "causes")    return LinkType::Causes;
    if (s == "caused_by") return LinkType::CausedBy;
    if (s == "enables")   return LinkType::Enables;
    if (s == "prevents")  return LinkType::Prevents;
    return LinkType::Entity;
}

// ─── time helpers ─────────────────────────────────────────────────────────────

TimePoint nowUtc() {
    return std::chrono::system_clock::now();
}

std::string isoFormat(TimePoint tp) {
    using namespace std::chrono;
    if (tp.time_since_epoch().count() == 0) return "";
    auto t  = system_clock::to_time_t(tp);
    auto us = duration_cast<microseconds>(tp.time_since_epoch()) % seconds{1};
    std::tm tm_utc{};
#ifdef _WIN32
    gmtime_s(&tm_utc, &t);
#else
    gmtime_r(&t, &tm_utc);
#endif
    std::ostringstream ss;
    ss << std::put_time(&tm_utc, "%Y-%m-%dT%H:%M:%S");
    ss << '.' << std::setw(6) << std::setfill('0') << us.count() << 'Z';
    return ss.str();
}

TimePoint isoParse(const std::string& s) {
    if (s.empty()) return TimePoint{};
    // Accept "YYYY-MM-DDTHH:MM:SS[.ffffff][Z|+HH:MM]". We discard fractional
    // seconds and timezone offset (assume UTC).
    std::string trimmed = s;
    if (!trimmed.empty() && trimmed.back() == 'Z') trimmed.pop_back();

    std::tm tm_in{};
    std::istringstream iss(trimmed);
    iss >> std::get_time(&tm_in, "%Y-%m-%dT%H:%M:%S");
    if (iss.fail()) return TimePoint{};
#ifdef _WIN32
    auto t = _mkgmtime(&tm_in);
#else
    auto t = timegm(&tm_in);
#endif
    return std::chrono::system_clock::from_time_t(t);
}

// ─── DispositionTraits ────────────────────────────────────────────────────────

void to_json(json& j, const DispositionTraits& d) {
    j = json{
        {"skepticism", d.skepticism},
        {"literalism", d.literalism},
        {"empathy",    d.empathy},
    };
}

void from_json(const json& j, DispositionTraits& d) {
    d.skepticism = j.value("skepticism", 3);
    d.literalism = j.value("literalism", 3);
    d.empathy    = j.value("empathy",    3);
}

// ─── Bank ─────────────────────────────────────────────────────────────────────

void to_json(json& j, const Bank& b) {
    j = json{
        {"bank_id",     b.bank_id},
        {"disposition", b.disposition},
        {"mission",     b.mission},
        {"created_at",  isoFormat(b.created_at)},
        {"updated_at",  isoFormat(b.updated_at)},
    };
}

void from_json(const json& j, Bank& b) {
    b.bank_id     = j.value("bank_id", "");
    if (j.contains("disposition")) b.disposition = j.at("disposition").get<DispositionTraits>();
    b.mission     = j.value("mission", j.value("background", std::string{}));
    b.created_at  = isoParse(j.value("created_at", std::string{}));
    b.updated_at  = isoParse(j.value("updated_at", std::string{}));
}

// ─── Document ─────────────────────────────────────────────────────────────────

void to_json(json& j, const Document& d) {
    j = json{
        {"id",            d.id},
        {"bank_id",       d.bank_id},
        {"original_text", d.original_text},
        {"content_hash",  d.content_hash},
        {"tags",          d.tags},
        {"created_at",    isoFormat(d.created_at)},
        {"updated_at",    isoFormat(d.updated_at)},
    };
}

void from_json(const json& j, Document& d) {
    d.id            = j.value("id", "");
    d.bank_id       = j.value("bank_id", "");
    d.original_text = j.value("original_text", "");
    d.content_hash  = j.value("content_hash", "");
    d.tags          = j.value("tags", std::vector<std::string>{});
    d.created_at    = isoParse(j.value("created_at", std::string{}));
    d.updated_at    = isoParse(j.value("updated_at", std::string{}));
}

// ─── Entity ───────────────────────────────────────────────────────────────────

void to_json(json& j, const Entity& e) {
    j = json{
        {"id",             e.id},
        {"bank_id",        e.bank_id},
        {"canonical_name", e.canonical_name},
        {"metadata",       e.metadata},
        {"first_seen",     isoFormat(e.first_seen)},
        {"last_seen",      isoFormat(e.last_seen)},
        {"mention_count",  e.mention_count},
    };
}

void from_json(const json& j, Entity& e) {
    e.id             = j.value("id", "");
    e.bank_id        = j.value("bank_id", "");
    e.canonical_name = j.value("canonical_name", "");
    e.metadata       = j.value("metadata", std::map<std::string, std::string>{});
    e.first_seen     = isoParse(j.value("first_seen", std::string{}));
    e.last_seen      = isoParse(j.value("last_seen",  std::string{}));
    e.mention_count  = j.value("mention_count", 1);
}

// ─── MemoryUnit ───────────────────────────────────────────────────────────────

static void writeOptIso(json& j, const char* k, const std::optional<TimePoint>& t) {
    if (t) j[k] = isoFormat(*t);
    else   j[k] = nullptr;
}

static std::optional<TimePoint> readOptIso(const json& j, const char* k) {
    if (!j.contains(k) || j.at(k).is_null()) return std::nullopt;
    if (j.at(k).is_string()) {
        auto s = j.at(k).get<std::string>();
        if (s.empty()) return std::nullopt;
        return isoParse(s);
    }
    return std::nullopt;
}

void to_json(json& j, const MemoryUnit& u) {
    j = json{
        {"id",          u.id},
        {"bank_id",     u.bank_id},
        {"document_id", u.document_id},
        {"text",        u.text},
        {"context",     u.context},
        {"fact_type",   factTypeToString(u.fact_type)},
        {"event_date",  isoFormat(u.event_date)},
        {"metadata",    u.metadata},
        {"tags",        u.tags},
        {"entity_ids",  u.entity_ids},
        {"embedding",   u.embedding},
        {"proof_count", u.proof_count},
        {"created_at",  isoFormat(u.created_at)},
        {"updated_at",  isoFormat(u.updated_at)},
    };
    writeOptIso(j, "occurred_start", u.occurred_start);
    writeOptIso(j, "occurred_end",   u.occurred_end);
    writeOptIso(j, "mentioned_at",   u.mentioned_at);
    if (u.chunk_id) j["chunk_id"] = *u.chunk_id;
    else            j["chunk_id"] = nullptr;
}

void from_json(const json& j, MemoryUnit& u) {
    u.id          = j.value("id", "");
    u.bank_id     = j.value("bank_id", "");
    u.document_id = j.value("document_id", "");
    u.text        = j.value("text", "");
    u.context     = j.value("context", "");
    u.fact_type   = factTypeFromString(j.value("fact_type", std::string("world")));
    u.event_date  = isoParse(j.value("event_date", std::string{}));
    u.metadata    = j.value("metadata", std::map<std::string, std::string>{});
    u.tags        = j.value("tags",     std::vector<std::string>{});
    u.entity_ids  = j.value("entity_ids", std::vector<std::string>{});
    u.embedding   = j.value("embedding",  std::vector<float>{});
    u.proof_count = j.value("proof_count", 0);
    u.created_at  = isoParse(j.value("created_at", std::string{}));
    u.updated_at  = isoParse(j.value("updated_at", std::string{}));
    u.occurred_start = readOptIso(j, "occurred_start");
    u.occurred_end   = readOptIso(j, "occurred_end");
    u.mentioned_at   = readOptIso(j, "mentioned_at");
    if (j.contains("chunk_id") && j.at("chunk_id").is_string()) {
        u.chunk_id = j.at("chunk_id").get<std::string>();
    } else {
        u.chunk_id.reset();
    }
}

// ─── MemoryLink ───────────────────────────────────────────────────────────────

std::string MemoryLink::compositeKey() const {
    // (from)__(type)__(to)__(entity?). UUIDs are filename-safe.
    std::string k = from_unit_id;
    k += "__";
    k += linkTypeToString(link_type);
    k += "__";
    k += to_unit_id;
    if (entity_id) {
        k += "__";
        k += *entity_id;
    }
    return k;
}

void to_json(json& j, const MemoryLink& l) {
    j = json{
        {"from_unit_id", l.from_unit_id},
        {"to_unit_id",   l.to_unit_id},
        {"link_type",    linkTypeToString(l.link_type)},
        {"weight",       l.weight},
        {"created_at",   isoFormat(l.created_at)},
    };
    if (l.entity_id) j["entity_id"] = *l.entity_id;
    else             j["entity_id"] = nullptr;
}

void from_json(const json& j, MemoryLink& l) {
    l.from_unit_id = j.value("from_unit_id", "");
    l.to_unit_id   = j.value("to_unit_id",   "");
    l.link_type    = linkTypeFromString(j.value("link_type", std::string("entity")));
    l.weight       = j.value("weight", 1.0);
    l.created_at   = isoParse(j.value("created_at", std::string{}));
    if (j.contains("entity_id") && j.at("entity_id").is_string()) {
        l.entity_id = j.at("entity_id").get<std::string>();
    } else {
        l.entity_id.reset();
    }
}

// ─── MentalModel ──────────────────────────────────────────────────────────────

void to_json(json& j, const MentalModel& m) {
    j = json{
        {"id",          m.id},
        {"bank_id",     m.bank_id},
        {"name",        m.name},
        {"description", m.description},
        {"summary",     m.summary},
        {"created_at",  isoFormat(m.created_at)},
    };
    writeOptIso(j, "summary_updated_at", m.summary_updated_at);
}

void from_json(const json& j, MentalModel& m) {
    m.id          = j.value("id", "");
    m.bank_id     = j.value("bank_id", "");
    m.name        = j.value("name", "");
    m.description = j.value("description", "");
    m.summary     = j.value("summary", "");
    m.created_at  = isoParse(j.value("created_at", std::string{}));
    m.summary_updated_at = readOptIso(j, "summary_updated_at");
}

// ─── helpers ──────────────────────────────────────────────────────────────────

std::string newUuid() {
    // RFC4122-ish v4 string using std::mt19937_64. Not cryptographically
    // strong, but unique enough for local file identifiers.
    static thread_local std::mt19937_64 rng{
        std::random_device{}() ^
        static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count())
    };
    auto rand_hex = [&](int n) {
        static const char* kHex = "0123456789abcdef";
        std::string out(n, '0');
        for (int i = 0; i < n; ++i) {
            out[i] = kHex[rng() & 0xF];
        }
        return out;
    };
    std::string s;
    s.reserve(36);
    s += rand_hex(8);  s += '-';
    s += rand_hex(4);  s += '-';
    s += '4';                       // version 4
    s += rand_hex(3);  s += '-';
    {
        static const char* kVar = "89ab";
        s += kVar[rng() & 0x3];     // variant 10xx
        s += rand_hex(3);
    }
    s += '-';
    s += rand_hex(12);
    return s;
}

std::string contentHash(const std::string& s) {
    // Deliberately simple — std::hash + length, hex-encoded. Adequate for
    // dedup of identical content within a bank.
    std::ostringstream ss;
    ss << std::hex << std::setw(16) << std::setfill('0')
       << std::hash<std::string>{}(s);
    ss << '-' << s.size();
    return ss.str();
}

} // namespace agentcpp::memory
<std::string>{}(s);
    ss << '-' << s.size();
    return ss.str();
}

} // namespace agentcpp::memory
