#pragma once
//
// Core data model for the hindsight-style memory subsystem.
//
// This mirrors the SQLAlchemy models defined in
// hindsight-api-slim/hindsight_api/models.py and the dataclasses in
// hindsight_api/engine/retain/types.py and hindsight_api/engine/search/types.py,
// translated 1:1 into C++20 structs.
//
// Storage backend: plain files on disk (see MemoryStorage.hpp). There is no
// PostgreSQL, no LLM, no embedding model. Where hindsight uses an LLM
// (fact extraction) or pgvector (semantic recall), we substitute heuristic
// equivalents (sentence splitting, BM25). The public API surface — retain,
// recall, reflect, banks, entities, links, mental models — matches.
//
#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>
#include <nlohmann/json.hpp>

namespace agentcpp::memory {

using json     = nlohmann::json;
using TimePoint = std::chrono::system_clock::time_point;

inline constexpr const char* kDefaultBankId = "default";

// ─── enums ────────────────────────────────────────────────────────────────────

// hindsight: CheckConstraint("fact_type IN ('world', 'experience', 'observation')")
enum class FactType { World, Experience, Observation };

const char* factTypeToString(FactType t);
FactType    factTypeFromString(const std::string& s);

// hindsight: link_type IN ('temporal','semantic','entity','causes','caused_by','enables','prevents')
enum class LinkType {
    Temporal,
    Semantic,
    Entity,
    Causes,
    CausedBy,
    Enables,
    Prevents,
};

const char* linkTypeToString(LinkType t);
LinkType    linkTypeFromString(const std::string& s);

// hindsight: Budget (LOW, MID, HIGH) — search budget controlling fan-out.
enum class Budget { Low, Mid, High };

// ─── time helpers (ISO 8601) ──────────────────────────────────────────────────

std::string  isoFormat(TimePoint t);
TimePoint    isoParse  (const std::string& s);
TimePoint    nowUtc();

// ─── DispositionTraits (per-bank reflect bias) ────────────────────────────────
//
// hindsight: Bank.disposition: {"skepticism": 3, "literalism": 3, "empathy": 3}
struct DispositionTraits {
    int skepticism = 3;  // 1=trusting, 5=skeptical
    int literalism = 3;  // 1=flexible, 5=literal
    int empathy    = 3;  // 1=detached, 5=empathetic
};

void to_json  (json& j, const DispositionTraits& d);
void from_json(const json& j, DispositionTraits& d);

// ─── Bank ─────────────────────────────────────────────────────────────────────
//
// hindsight: class Bank(Base) — banks are top-level namespaces for memory.
struct Bank {
    std::string       bank_id;
    DispositionTraits disposition{};
    std::string       mission;          // hindsight calls this "background"
    TimePoint         created_at{};
    TimePoint         updated_at{};
};

void to_json  (json& j, const Bank& b);
void from_json(const json& j, Bank& b);

// ─── Document ─────────────────────────────────────────────────────────────────
//
// hindsight: class Document(Base) — source content the units were extracted from.
struct Document {
    std::string id;
    std::string bank_id;
    std::string original_text;
    std::string content_hash;
    std::vector<std::string> tags;
    TimePoint   created_at{};
    TimePoint   updated_at{};
};

void to_json  (json& j, const Document& d);
void from_json(const json& j, Document& d);

// ─── Entity ───────────────────────────────────────────────────────────────────
//
// hindsight: class Entity(Base) — resolved entity (person, org, place, concept).
struct Entity {
    std::string                          id;
    std::string                          bank_id;
    std::string                          canonical_name;
    std::map<std::string, std::string>   metadata;
    TimePoint                            first_seen{};
    TimePoint                            last_seen{};
    int                                  mention_count = 1;
};

void to_json  (json& j, const Entity& e);
void from_json(const json& j, Entity& e);

// ─── MemoryUnit ───────────────────────────────────────────────────────────────
//
// hindsight: class MemoryUnit(Base) — atomic fact stored in the bank.
// In the file backend we drop the pgvector embedding column; recall uses
// BM25 over the `text` field instead.
struct MemoryUnit {
    std::string                          id;
    std::string                          bank_id;
    std::string                          document_id;
    std::string                          text;
    std::string                          context;
    FactType                             fact_type = FactType::World;
    TimePoint                            event_date{};        // legacy: usually = mentioned_at
    std::optional<TimePoint>             occurred_start;
    std::optional<TimePoint>             occurred_end;
    std::optional<TimePoint>             mentioned_at;
    std::map<std::string, std::string>   metadata;
    std::vector<std::string>             tags;
    std::vector<std::string>             entity_ids;          // joined via unit_entities
    std::optional<std::string>           chunk_id;
    std::vector<float>                   embedding;           // optional; empty when no embedder
    int                                  proof_count = 0;     // observation only
    TimePoint                            created_at{};
    TimePoint                            updated_at{};
};

void to_json  (json& j, const MemoryUnit& u);
void from_json(const json& j, MemoryUnit& u);

// ─── MemoryLink ───────────────────────────────────────────────────────────────
//
// hindsight: class MemoryLink(Base) — typed edge between two units.
struct MemoryLink {
    std::string                from_unit_id;
    std::string                to_unit_id;
    LinkType                   link_type = LinkType::Entity;
    std::optional<std::string> entity_id;     // present iff link_type == Entity
    double                     weight = 1.0;  // hindsight: [0.0, 1.0]
    TimePoint                  created_at{};

    // hindsight uses a composite PK (from, to, type, entity_id). For file
    // storage we derive a single stable file name from those four fields.
    std::string compositeKey() const;
};

void to_json  (json& j, const MemoryLink& l);
void from_json(const json& j, MemoryLink& l);

// ─── MentalModel ──────────────────────────────────────────────────────────────
//
// hindsight: MentalModel (response_models.py) — user-configured focus area
// with a summary regenerated as new facts arrive.
struct MentalModel {
    std::string                          id;
    std::string                          bank_id;
    std::string                          name;
    std::string                          description;
    std::string                          summary;
    std::optional<TimePoint>             summary_updated_at;
    TimePoint                            created_at{};
};

void to_json  (json& j, const MentalModel& m);
void from_json(const json& j, MentalModel& m);

// ─── Retain inputs/outputs ────────────────────────────────────────────────────
//
// hindsight: RetainContentDict / RetainContent / RetainBatch in retain/types.py.
struct EntityHint {
    std::string text;
    std::string type;  // optional, e.g. "PERSON", "ORG"
};

struct RetainContent {
    std::string                          content;     // raw text (required)
    std::string                          context;
    std::optional<TimePoint>             event_date;
    std::optional<TimePoint>             occurred_start;
    std::optional<TimePoint>             occurred_end;
    std::string                          document_id; // optional, auto-generated if empty
    FactType                             fact_type_override = FactType::World;
    std::map<std::string, std::string>   metadata;
    std::vector<EntityHint>              entities;    // user-provided entities
    std::vector<std::string>             tags;
};

struct RetainResult {
    std::vector<std::string>             unit_ids;     // per-content (flattened)
    std::vector<std::vector<std::string>> unit_ids_by_content;
    std::vector<std::string>             entity_ids;
    std::vector<std::string>             document_ids;
    std::size_t                          facts_extracted = 0;
    std::size_t                          links_created   = 0;
};

// ─── Recall pipeline types ────────────────────────────────────────────────────
//
// hindsight: search/types.py — RetrievalResult, MergedCandidate, ScoredResult.
struct RetrievalResult {
    std::string                                          id;
    std::string                                          text;
    FactType                                             fact_type = FactType::World;
    std::optional<std::string>                           context;
    std::optional<TimePoint>                             event_date;
    std::optional<TimePoint>                             occurred_start;
    std::optional<TimePoint>                             occurred_end;
    std::optional<TimePoint>                             mentioned_at;
    std::optional<std::string>                           document_id;
    std::optional<std::string>                           chunk_id;
    std::vector<std::string>                             tags;
    std::map<std::string, std::string>                   metadata;
    int                                                  proof_count = 0;

    // method-specific scores (only one set depending on retrieval method)
    std::optional<double>                                similarity;       // semantic
    std::optional<double>                                bm25_score;       // BM25
    std::optional<double>                                activation;       // graph
    std::optional<double>                                temporal_score;   // temporal
    std::optional<double>                                temporal_proximity;
};

struct MergedCandidate {
    RetrievalResult                       retrieval;
    double                                rrf_score = 0.0;
    int                                   rrf_rank  = 0;
    std::map<std::string, int>            source_ranks;

    const std::string& id() const { return retrieval.id; }
};

struct ScoredResult {
    MergedCandidate                       candidate;

    double cross_encoder_score            = 0.0;
    double cross_encoder_score_normalized = 0.0;

    double rrf_normalized                 = 0.0;
    double recency                        = 0.5;
    double temporal                       = 0.5;

    double combined_score                 = 0.0;
    double weight                         = 0.0;

    const std::string&     id()        const { return candidate.id(); }
    const RetrievalResult& retrieval() const { return candidate.retrieval; }
};

// ─── Recall public API ────────────────────────────────────────────────────────

struct RecallQuery {
    std::string                          query;
    Budget                               budget       = Budget::Mid;
    std::optional<TimePoint>             question_date;
    std::vector<FactType>                fact_type_filter;
    std::vector<std::string>             tags_any;
    std::size_t                          k            = 10;
    bool                                 enable_trace = false;
    bool                                 include_entities = false;
};

struct RecallResult {
    std::vector<ScoredResult>            results;
    json                                 trace;     // populated when enable_trace
};

// ─── Reflect public API ───────────────────────────────────────────────────────

struct ReflectQuery {
    std::string                          query;
    std::string                          context;
    Budget                               budget       = Budget::Mid;
    std::size_t                          max_facts    = 12;
};

struct ReflectResult {
    std::string                                              text;
    // Grouped by "world" / "experience" / "observation" / "mental_models"
    std::map<std::string, std::vector<MemoryUnit>>           based_on;
    std::vector<MentalModel>                                 mental_models_applied;
    json                                                     structured_output;
};

// ─── small helpers ────────────────────────────────────────────────────────────

// Generate a UUID v4 string (no external deps).
std::string newUuid();

// Stable content hash (SHA1-ish via std::hash + length) — used for dedup.
std::string contentHash(const std::string& s);

} // namespace agentcpp::memory
