//
// MemoryEngine implementation. Wires together fact extraction, entity
// resolution, storage, BM25 + graph retrieval, RRF fusion, and reranking.
//
// Public API parallels hindsight_api/engine/memory_engine.MemoryEngine.
//
#include <memory/MemoryEngine.hpp>

#include <memory/BM25Index.hpp>
#include <memory/EntityResolver.hpp>
#include <memory/FactExtractor.hpp>
#include <memory/Fusion.hpp>
#include <memory/GraphRetrieval.hpp>
#include <memory/Reranker.hpp>

#include <memory/providers/HeuristicFactExtractor.hpp>
#include <memory/providers/HeuristicReranker.hpp>
#include <memory/providers/IEmbedder.hpp>
#include <memory/providers/NullEmbedder.hpp>
#include <memory/providers/TemplateReflectComposer.hpp>

#include <utils/Logger.hpp>
#include <utils/StringUtils.hpp>

#include <algorithm>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace agentcpp::memory {

namespace {

std::size_t budgetFanout(Budget b) {
    // hindsight maps Budget to per-strategy k. We mirror the spirit:
    switch (b) {
        case Budget::Low:  return 20;
        case Budget::Mid:  return 50;
        case Budget::High: return 120;
    }
    return 50;
}

} // namespace

// ─── ctor ─────────────────────────────────────────────────────────────────────

MemoryEngine::MemoryEngine(std::filesystem::path root, bool create)
    : storage_(std::move(root), create) {
    fillMissingProviders();
}

MemoryEngine::MemoryEngine(std::filesystem::path root,
                           MemoryProviders       providers,
                           bool                  create)
    : storage_(std::move(root), create)
    , providers_(std::move(providers)) {
    fillMissingProviders();
}

void MemoryEngine::setProviders(MemoryProviders providers) {
    providers_ = std::move(providers);
    fillMissingProviders();
}

void MemoryEngine::fillMissingProviders() {
    if (!providers_.fact_extractor)
        providers_.fact_extractor = std::make_unique<HeuristicFactExtractor>();
    if (!providers_.embedder)
        providers_.embedder = std::make_unique<NullEmbedder>();
    if (!providers_.reranker)
        providers_.reranker = std::make_unique<HeuristicReranker>();
    if (!providers_.reflect_composer)
        providers_.reflect_composer = std::make_unique<TemplateReflectComposer>();
}

std::string MemoryEngine::factExtractorName()   const { return providers_.fact_extractor->name(); }
std::string MemoryEngine::embedderName()        const { return providers_.embedder->name(); }
std::string MemoryEngine::rerankerName()        const { return providers_.reranker->name(); }
std::string MemoryEngine::reflectComposerName() const { return providers_.reflect_composer->name(); }

// ─── Bank management ─────────────────────────────────────────────────────────

std::vector<std::string> MemoryEngine::listBanks() const {
    return storage_.listBankIds();
}

Bank MemoryEngine::getOrCreateBank(const std::string& bank_id) {
    if (auto existing = storage_.readBank(bank_id)) return *existing;
    Bank b;
    b.bank_id    = bank_id;
    b.created_at = nowUtc();
    b.updated_at = b.created_at;
    storage_.writeBank(b);
    return b;
}

std::optional<Bank> MemoryEngine::getBank(const std::string& bank_id) const {
    return storage_.readBank(bank_id);
}

bool MemoryEngine::setBankMission(const std::string& bank_id,
                                  const std::string& mission) {
    auto bank = storage_.readBank(bank_id).value_or(Bank{});
    if (bank.bank_id.empty()) {
        bank.bank_id    = bank_id;
        bank.created_at = nowUtc();
    }
    bank.mission    = mission;
    bank.updated_at = nowUtc();
    return storage_.writeBank(bank);
}

bool MemoryEngine::setBankDisposition(const std::string& bank_id,
                                      const DispositionTraits& d) {
    auto bank = storage_.readBank(bank_id).value_or(Bank{});
    if (bank.bank_id.empty()) {
        bank.bank_id    = bank_id;
        bank.created_at = nowUtc();
    }
    bank.disposition = d;
    bank.updated_at  = nowUtc();
    return storage_.writeBank(bank);
}

bool MemoryEngine::deleteBank(const std::string& bank_id) {
    return storage_.deleteBank(bank_id);
}

// ─── Listing ─────────────────────────────────────────────────────────────────

std::vector<MemoryUnit>
MemoryEngine::listUnits(const std::string& bank_id,
                        std::optional<FactType> filter,
                        std::size_t limit,
                        std::size_t offset) const {
    auto all = storage_.listUnits(bank_id);
    if (filter) {
        all.erase(std::remove_if(all.begin(), all.end(),
                  [&](const MemoryUnit& u) { return u.fact_type != *filter; }),
                  all.end());
    }
    std::sort(all.begin(), all.end(),
              [](const MemoryUnit& a, const MemoryUnit& b) {
                  return a.event_date > b.event_date;
              });
    if (offset >= all.size()) return {};
    auto begin = all.begin() + offset;
    auto end   = (limit == 0 || offset + limit >= all.size()) ? all.end()
                                                              : begin + limit;
    return {begin, end};
}

std::vector<Entity> MemoryEngine::listEntities(const std::string& bank_id,
                                               std::size_t limit,
                                               std::size_t offset) const {
    auto all = storage_.listEntities(bank_id);
    std::sort(all.begin(), all.end(),
              [](const Entity& a, const Entity& b) {
                  return a.mention_count > b.mention_count;
              });
    if (offset >= all.size()) return {};
    auto begin = all.begin() + offset;
    auto end   = (limit == 0 || offset + limit >= all.size()) ? all.end()
                                                              : begin + limit;
    return {begin, end};
}

std::vector<MemoryLink> MemoryEngine::listLinks(const std::string& bank_id) const {
    return storage_.listLinks(bank_id);
}

std::vector<MentalModel>
MemoryEngine::listMentalModels(const std::string& bank_id) const {
    return storage_.listMentalModels(bank_id);
}

std::vector<Document> MemoryEngine::listDocuments(const std::string& bank_id) const {
    return storage_.listDocuments(bank_id);
}

std::optional<MemoryUnit> MemoryEngine::getUnit(const std::string& bank_id,
                                                const std::string& unit_id) const {
    return storage_.readUnit(bank_id, unit_id);
}

std::optional<Entity> MemoryEngine::getEntity(const std::string& bank_id,
                                              const std::string& entity_id) const {
    return storage_.readEntity(bank_id, entity_id);
}

MemoryEngine::BankStats MemoryEngine::getBankStats(const std::string& bank_id) const {
    BankStats s;
    auto units = storage_.listUnits(bank_id);
    s.units = units.size();
    for (auto& u : units) {
        switch (u.fact_type) {
            case FactType::World:       ++s.world; break;
            case FactType::Experience:  ++s.experience; break;
            case FactType::Observation: ++s.observation; break;
        }
    }
    s.entities  = storage_.listEntities(bank_id).size();
    s.links     = storage_.listLinks(bank_id).size();
    s.documents = storage_.listDocuments(bank_id).size();
    return s;
}

// ─── Mental models ───────────────────────────────────────────────────────────

MentalModel MemoryEngine::upsertMentalModel(const std::string& bank_id,
                                            MentalModel        model) {
    if (model.id.empty()) model.id = newUuid();
    if (model.bank_id.empty()) model.bank_id = bank_id;
    if (model.created_at.time_since_epoch().count() == 0) model.created_at = nowUtc();
    storage_.writeMentalModel(model);
    return model;
}

bool MemoryEngine::deleteMentalModel(const std::string& bank_id,
                                     const std::string& mm_id) {
    return storage_.deleteMentalModel(bank_id, mm_id);
}

// ─── retain ──────────────────────────────────────────────────────────────────

RetainResult MemoryEngine::retain(const std::string& bank_id,
                                  const RetainContent& content) {
    return retain(bank_id, std::vector<RetainContent>{content});
}

RetainResult MemoryEngine::retain(const std::string& bank_id,
                                  const std::vector<RetainContent>& contents) {
    RetainResult out;
    if (!storage_.isReady()) return out;
    getOrCreateBank(bank_id);

    EntityResolver  resolver(storage_);

    out.unit_ids_by_content.resize(contents.size());

    // ── Phase 1: fact extraction (via provider) ───────────────────────────
    std::vector<ExtractedFact> all_facts;
    for (std::size_t i = 0; i < contents.size(); ++i) {
        const auto& c     = contents[i];
        auto        facts = providers_.fact_extractor->extract(c, static_cast<int>(i));
        for (auto& f : facts) all_facts.push_back(std::move(f));
    }
    out.facts_extracted = all_facts.size();

    // ── Phase 1.5: batch embed (optional, when provider is available) ─────
    std::vector<std::vector<float>> embeddings;
    if (providers_.embedder->available() && !all_facts.empty()) {
        std::vector<std::string> texts;
        texts.reserve(all_facts.size());
        for (const auto& f : all_facts) texts.push_back(f.text);
        embeddings = providers_.embedder->embed(texts);
        if (embeddings.size() != all_facts.size()) embeddings.clear();
    }

    // ── Document creation (one per RetainContent) ─────────────────────────
    std::vector<std::string> document_ids(contents.size());
    for (std::size_t i = 0; i < contents.size(); ++i) {
        const auto& c = contents[i];
        Document doc;
        doc.id            = c.document_id.empty() ? newUuid() : c.document_id;
        doc.bank_id       = bank_id;
        doc.original_text = c.content;
        doc.content_hash  = contentHash(c.content);
        doc.tags          = c.tags;
        doc.created_at    = nowUtc();
        doc.updated_at    = doc.created_at;
        storage_.writeDocument(doc);
        document_ids[i] = doc.id;
        out.document_ids.push_back(doc.id);
    }

    // ── Phase 2: entity resolution + unit creation ────────────────────────
    // entity_id -> list of unit_ids that mention this entity (for entity-link
    // generation across units within this retain batch).
    std::unordered_map<std::string, std::vector<std::string>> entity_to_units;
    std::set<std::string> entity_ids_seen;

    std::vector<MemoryUnit> created_units;
    created_units.reserve(all_facts.size());

    for (std::size_t fact_idx = 0; fact_idx < all_facts.size(); ++fact_idx) {
        const auto& f = all_facts[fact_idx];
        MemoryUnit u;
        u.id            = newUuid();
        u.bank_id       = bank_id;
        u.document_id   = document_ids[f.content_index];
        u.text          = f.text;
        if (fact_idx < embeddings.size()) u.embedding = embeddings[fact_idx];
        u.context       = f.context;
        u.fact_type     = f.fact_type;
        u.event_date    = f.mentioned_at.value_or(nowUtc());
        u.occurred_start = f.occurred_start;
        u.occurred_end   = f.occurred_end;
        u.mentioned_at   = f.mentioned_at;
        u.metadata       = f.metadata;
        u.tags           = f.tags;
        u.created_at     = nowUtc();
        u.updated_at     = u.created_at;
        {
            std::ostringstream cid;
            cid << u.document_id << '_' << f.chunk_index;
            u.chunk_id = cid.str();
        }

        // resolve entities → store ids on unit
        auto entities = resolver.resolveBatch(bank_id, f.entity_names);
        for (auto& e : entities) {
            u.entity_ids.push_back(e.id);
            entity_to_units[e.id].push_back(u.id);
            entity_ids_seen.insert(e.id);
        }

        storage_.writeUnit(u);
        created_units.push_back(u);
        out.unit_ids.push_back(u.id);
        out.unit_ids_by_content[f.content_index].push_back(u.id);
    }
    out.entity_ids.assign(entity_ids_seen.begin(), entity_ids_seen.end());

    // ── Phase 3: link creation ────────────────────────────────────────────
    //
    // Mirrors hindsight: shared-entity links + intra-document temporal links.
    auto link_now = nowUtc();
    std::size_t link_count = 0;

    // entity-shared links
    for (auto& [entity_id, units] : entity_to_units) {
        if (units.size() < 2) continue;
        for (std::size_t i = 0; i < units.size(); ++i) {
            for (std::size_t j = i + 1; j < units.size(); ++j) {
                MemoryLink l;
                l.from_unit_id = units[i];
                l.to_unit_id   = units[j];
                l.link_type    = LinkType::Entity;
                l.entity_id    = entity_id;
                l.weight       = 1.0;
                l.created_at   = link_now;
                if (storage_.writeLink(bank_id, l)) ++link_count;
            }
        }
    }
    // intra-content temporal links (consecutive units from same content)
    for (std::size_t ci = 0; ci < contents.size(); ++ci) {
        const auto& ids = out.unit_ids_by_content[ci];
        for (std::size_t i = 1; i < ids.size(); ++i) {
            MemoryLink l;
            l.from_unit_id = ids[i - 1];
            l.to_unit_id   = ids[i];
            l.link_type    = LinkType::Temporal;
            l.weight       = 0.5;
            l.created_at   = link_now;
            if (storage_.writeLink(bank_id, l)) ++link_count;
        }
    }
    out.links_created = link_count;

    LOG_DEBUG("retain: bank=" + bank_id +
              " facts=" + std::to_string(out.facts_extracted) +
              " units=" + std::to_string(out.unit_ids.size()) +
              " entities=" + std::to_string(out.entity_ids.size()) +
              " links=" + std::to_string(out.links_created));
    return out;
}

// ─── recall ──────────────────────────────────────────────────────────────────

RecallResult MemoryEngine::recall(const std::string& bank_id,
                                  const RecallQuery& query) {
    RecallResult out;
    if (!storage_.isReady() || query.query.empty()) return out;

    // ── Load corpus ───────────────────────────────────────────────────────
    auto units = storage_.listUnits(bank_id);
    if (units.empty()) return out;

    // Optional fact-type filter applied at corpus level so BM25 doesn't waste
    // budget on facts we'll throw away.
    if (!query.fact_type_filter.empty()) {
        std::set<FactType> allow(query.fact_type_filter.begin(),
                                 query.fact_type_filter.end());
        units.erase(std::remove_if(units.begin(), units.end(),
                    [&](const MemoryUnit& u) { return !allow.count(u.fact_type); }),
                    units.end());
        if (units.empty()) return out;
    }
    if (!query.tags_any.empty()) {
        std::set<std::string> wanted(query.tags_any.begin(), query.tags_any.end());
        units.erase(std::remove_if(units.begin(), units.end(),
                    [&](const MemoryUnit& u) {
                        for (auto& t : u.tags) if (wanted.count(t)) return false;
                        return true;
                    }),
                    units.end());
        if (units.empty()) return out;
    }

    std::unordered_map<std::string, MemoryUnit> unit_index;
    unit_index.reserve(units.size());
    for (const auto& u : units) unit_index[u.id] = u;

    const auto fanout = budgetFanout(query.budget);

    // ── 1. BM25 retrieval (substitute for semantic+bm25 in hindsight) ────
    BM25Index bm25;
    bm25.build(units);
    auto bm25_results = bm25.search(query.query, fanout);

    // Semantic channel: real cosine over embeddings if provider available,
    // otherwise a BM25-alt-params placeholder so the RRF merger always sees
    // a 4th input list (matches hindsight's pipeline shape).
    std::vector<RetrievalResult> semantic_results;
    if (providers_.embedder->available()) {
        auto qvecs = providers_.embedder->embed({query.query});
        if (!qvecs.empty() && !qvecs.front().empty()) {
            const auto& qv = qvecs.front();
            std::vector<std::pair<double, const MemoryUnit*>> scored;
            scored.reserve(units.size());
            for (const auto& u : units) {
                if (u.embedding.empty()) continue;
                double sim = cosineSimilarity(qv, u.embedding);
                if (sim > 0.0) scored.emplace_back(sim, &u);
            }
            std::sort(scored.begin(), scored.end(),
                      [](const auto& a, const auto& b) { return a.first > b.first; });
            if (scored.size() > fanout) scored.resize(fanout);
            for (auto& [sim, up] : scored) {
                RetrievalResult r;
                r.id            = up->id;
                r.text          = up->text;
                r.fact_type     = up->fact_type;
                r.context       = up->context.empty() ? std::optional<std::string>{}
                                                      : std::optional<std::string>{up->context};
                r.event_date    = up->event_date;
                r.occurred_start = up->occurred_start;
                r.mentioned_at   = up->mentioned_at;
                r.document_id    = up->document_id;
                r.chunk_id       = up->chunk_id;
                r.tags           = up->tags;
                r.metadata       = up->metadata;
                r.similarity     = sim;
                semantic_results.push_back(std::move(r));
            }
        }
    } else {
        BM25Index alt;
        alt.k1 = 0.9; alt.b = 0.4;
        alt.build(units);
        for (auto& r : alt.search(query.query, fanout)) {
            r.similarity = r.bm25_score;
            r.bm25_score.reset();
            semantic_results.push_back(std::move(r));
        }
    }

    // ── 2. Graph retrieval (link expansion) ──────────────────────────────
    std::vector<std::string> seeds;
    seeds.reserve(bm25_results.size());
    for (auto& r : bm25_results) seeds.push_back(r.id);
    auto bank_links = storage_.listLinks(bank_id);  // must outlive `graph`
    GraphRetrieval graph(bank_links, unit_index);
    auto graph_results = graph.expand(seeds, fanout);

    // ── 3. Temporal retrieval ────────────────────────────────────────────
    //
    // hindsight extracts dates from the query and pulls matching units. We
    // do the simple version: if question_date is set, prefer units whose
    // mentioned_at/occurred_start are close to it.
    std::vector<RetrievalResult> temporal_results;
    if (query.question_date) {
        std::vector<std::pair<double, const MemoryUnit*>> scored;
        scored.reserve(units.size());
        for (const auto& u : units) {
            TimePoint t{};
            if (u.occurred_start)      t = *u.occurred_start;
            else if (u.mentioned_at)   t = *u.mentioned_at;
            else                       t = u.event_date;
            if (t.time_since_epoch().count() == 0) continue;
            auto delta = std::abs(std::chrono::duration_cast<std::chrono::seconds>(
                                      *query.question_date - t).count());
            scored.emplace_back(static_cast<double>(delta), &u);
        }
        std::sort(scored.begin(), scored.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        if (scored.size() > fanout) scored.resize(fanout);
        for (auto& [delta, up] : scored) {
            RetrievalResult r;
            r.id           = up->id;
            r.text         = up->text;
            r.fact_type    = up->fact_type;
            r.context      = up->context.empty() ? std::optional<std::string>{}
                                                 : std::optional<std::string>{up->context};
            r.event_date   = up->event_date;
            r.occurred_start = up->occurred_start;
            r.mentioned_at   = up->mentioned_at;
            r.document_id    = up->document_id;
            r.chunk_id       = up->chunk_id;
            r.tags           = up->tags;
            r.metadata       = up->metadata;
            r.temporal_score = 1.0 / (1.0 + delta / 86400.0);
            temporal_results.push_back(std::move(r));
        }
    }

    // ── 4. RRF merge in (semantic, bm25, graph, temporal) order ──────────
    auto merged = reciprocalRankFusion(
        { semantic_results, bm25_results, graph_results, temporal_results });

    // Wrap into ScoredResult for the reranker.
    std::vector<ScoredResult> scored;
    scored.reserve(merged.size());
    for (auto& m : merged) {
        ScoredResult s;
        s.candidate = std::move(m);
        scored.push_back(std::move(s));
    }

    providers_.reranker->rerank(scored, query, nowUtc());
    if (query.k > 0 && scored.size() > query.k) scored.resize(query.k);

    out.results = std::move(scored);

    if (query.enable_trace) {
        out.trace = {
            {"query",         query.query},
            {"num_results",   out.results.size()},
            {"corpus_size",   units.size()},
            {"fanout",        fanout},
            {"sources",       json::array({"semantic", "bm25", "graph", "temporal"})},
            {"per_source_counts", {
                {"semantic", semantic_results.size()},
                {"bm25",     bm25_results.size()},
                {"graph",    graph_results.size()},
                {"temporal", temporal_results.size()},
            }},
        };
    }
    return out;
}

// ─── reflect ─────────────────────────────────────────────────────────────────

ReflectResult MemoryEngine::reflect(const std::string& bank_id,
                                    const ReflectQuery& rq) {
    ReflectResult out;
    if (!storage_.isReady()) return out;

    // ── 1. Recall the most relevant facts via the recall pipeline ────────
    RecallQuery rc;
    rc.query        = rq.query;
    rc.budget       = rq.budget;
    rc.k            = rq.max_facts;
    auto recall_out = recall(bank_id, rc);

    // ── 2. Group based_on by fact_type for hindsight-compatible output ──
    std::vector<MemoryUnit>& world       = out.based_on["world"];
    std::vector<MemoryUnit>& experience  = out.based_on["experience"];
    std::vector<MemoryUnit>& observation = out.based_on["observation"];

    for (const auto& s : recall_out.results) {
        const auto& r = s.retrieval();
        auto u = storage_.readUnit(bank_id, r.id);
        if (!u) continue;
        switch (u->fact_type) {
            case FactType::World:       world.push_back(*u); break;
            case FactType::Experience:  experience.push_back(*u); break;
            case FactType::Observation: observation.push_back(*u); break;
        }
    }

    // Suppress unused-variable warnings if reflect_composer ignores them
    (void)world; (void)experience; (void)observation;

    // ── 3. Mental models (declared focus areas) ──────────────────────────
    out.mental_models_applied = storage_.listMentalModels(bank_id);

    // ── 4. Delegate answer composition to the pluggable provider ─────────
    auto bank = getOrCreateBank(bank_id);
    out.text = providers_.reflect_composer->compose(
        bank, rq, out.based_on, out.mental_models_applied);
    return out;
}

} // namespace agentcpp::memory
