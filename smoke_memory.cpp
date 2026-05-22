// Smoke test for the hindsight-style memory subsystem.
//
// Verifies:
//   1. Default (all-heuristic) providers: retain → recall → reflect end-to-end
//      and disk persistence across engine open/close.
//   2. Provider injection: a mock IFactExtractor produces the units MemoryEngine
//      stores, proving the pluggable-interface refactor is wired.
//
// Build (sandbox-friendly — no libcurl / ClaudeClient): see project README.
// Compiles only the heuristic providers; the Claude/HTTP providers belong
// to the full CMake build because they depend on libcurl + api::ClaudeClient.
//
#include <memory/MemoryEngine.hpp>
#include <memory/MemoryProviders.hpp>
#include <memory/providers/HeuristicFactExtractor.hpp>
#include <memory/providers/HeuristicReranker.hpp>
#include <memory/providers/IFactExtractor.hpp>
#include <memory/providers/NullEmbedder.hpp>
#include <memory/providers/TemplateReflectComposer.hpp>

#include <filesystem>
#include <iostream>
#include <memory>

namespace mem = agentcpp::memory;

// A tiny IFactExtractor that just turns each content into ONE fact whose
// text is "MOCK:<original>". Used to prove the engine dispatches to whatever
// provider it's given.
class MockFactExtractor final : public mem::IFactExtractor {
public:
    bool        available() const override { return true; }
    std::string name()      const override { return "mock"; }

    std::vector<mem::ExtractedFact>
    extract(const mem::RetainContent& c, int content_index) override {
        ++calls;
        mem::ExtractedFact f;
        f.text          = "MOCK:" + c.content;
        f.fact_type     = c.fact_type_override;
        f.context       = c.context;
        f.tags          = c.tags;
        f.content_index = content_index;
        f.chunk_index   = 0;
        f.mentioned_at  = mem::nowUtc();
        return {f};
    }

    int calls = 0;
};

int main() {
    auto tmp = std::filesystem::temp_directory_path() / "agentcpp_memory_smoke";
    std::filesystem::remove_all(tmp);

    // ── Part 1: defaults end-to-end ─────────────────────────────────────
    mem::MemoryEngine engine(tmp);
    if (!engine.isReady()) { std::cerr << "engine not ready\n"; return 1; }
    std::cout << "Providers (default): fact="    << engine.factExtractorName()
              << "  embed="                      << engine.embedderName()
              << "  rerank="                     << engine.rerankerName()
              << "  reflect="                    << engine.reflectComposerName() << "\n";

    std::vector<mem::RetainContent> batch;
    {
        mem::RetainContent c;
        c.content = "Alice works at Google on the AI team. She joined in 2024.";
        c.fact_type_override = mem::FactType::World;
        batch.push_back(c);
    }
    {
        mem::RetainContent c;
        c.content = "Bob also works at Google but on the search infrastructure team.";
        c.fact_type_override = mem::FactType::World;
        batch.push_back(c);
    }
    {
        mem::RetainContent c;
        c.content = "I met Alice at a conference in Tokyo last month and we discussed neural networks.";
        c.fact_type_override = mem::FactType::Experience;
        batch.push_back(c);
    }
    auto r = engine.retain("default", batch);
    std::cout << "Retained: facts=" << r.facts_extracted
              << " units="          << r.unit_ids.size()
              << " entities="       << r.entity_ids.size()
              << " links="          << r.links_created << "\n";

    mem::RecallQuery q;
    q.query = "Where does Alice work?";
    q.k     = 5;
    auto rc = engine.recall("default", q);
    std::cout << "Recall: " << rc.results.size() << " results\n";
    int rank = 1;
    for (const auto& s : rc.results) {
        std::cout << "  #" << rank++ << " [" << mem::factTypeToString(s.retrieval().fact_type) << "] "
                  << s.retrieval().text << "  combined=" << s.combined_score << "\n";
    }

    mem::ReflectQuery rq;
    rq.query = "What do we know about Alice?";
    auto refl = engine.reflect("default", rq);
    std::cout << "Reflect based_on world=" << refl.based_on["world"].size()
              << " experience=" << refl.based_on["experience"].size() << "\n";

    // ── Part 2: provider swap ────────────────────────────────────────────
    auto tmp2 = std::filesystem::temp_directory_path() / "agentcpp_memory_swap";
    std::filesystem::remove_all(tmp2);

    mem::MemoryProviders custom;
    auto mock = std::make_unique<MockFactExtractor>();
    auto* mock_ptr = mock.get();  // keep a back-channel pointer to inspect calls
    custom.fact_extractor = std::move(mock);
    // Other slots intentionally left null — fillMissingProviders() supplies defaults.

    mem::MemoryEngine engine2(tmp2, std::move(custom));
    std::cout << "\nProviders (custom): fact=" << engine2.factExtractorName()
              << "  embed="                    << engine2.embedderName() << "\n";

    mem::RetainContent c;
    c.content = "Carol leads product at OpenAI.";
    auto r2 = engine2.retain("custom", c);
    std::cout << "Mock retain: calls=" << mock_ptr->calls
              << " units=" << r2.unit_ids.size() << "\n";

    auto units = engine2.listUnits("custom");
    bool mock_text_seen = false;
    for (const auto& u : units) {
        std::cout << "  unit text: " << u.text << "\n";
        if (u.text.rfind("MOCK:", 0) == 0) mock_text_seen = true;
    }

    // ── Persistence check (re-open default engine) ───────────────────────
    mem::MemoryEngine engine3(tmp, /*create=*/false);
    auto stats3 = engine3.getBankStats("default");
    std::cout << "\nReopen default: units=" << stats3.units
              << " entities=" << stats3.entities
              << " links="    << stats3.links << "\n";

    bool ok =
        r.facts_extracted > 0 &&
        !rc.results.empty() &&
        mock_ptr->calls == 1 &&
        r2.unit_ids.size() == 1 &&
        mock_text_seen &&
        stats3.units > 0;

    std::cout << (ok ? "\n[OK] smoke test passed\n" : "\n[FAIL] smoke test failed\n");
    return ok ? 0 : 1;
}
remove_all(tmp2);

    mem::MemoryProviders custom;
    auto mock = std::make_unique<MockFactExtractor>();
    auto* mock_ptr = mock.get();
    custom.fact_extractor = std::move(mock);
    mem::MemoryEngine engine2(tmp2, std::move(custom));
    std::cout << "\nProviders (custom): fact=" << engine2.factExtractorName()
              << "  embed="                    << engine2.embedderName() << "\n";

    mem::RetainContent c;
    c.content = "Carol leads product at OpenAI.";
    auto r2 = engine2.retain("custom", c);
    std::cout << "Mock retain: calls=" << mock_ptr->calls
              << " units=" << r2.unit_ids.size() << "\n";

    auto units = engine2.listUnits("custom");
    bool mock_text_seen = false;
    for (const auto& u : units) {
        std::cout << "  unit text: " << u.text << "\n";
        if (u.text.rfind("MOCK:", 0) == 0) mock_text_seen = true;
    }

    mem::MemoryEngine engine3(tmp, /*create=*/false);
    auto stats3 = engine3.getBankStats("default");
    std::cout << "\nReopen default: units=" << stats3.units
              << " entities=" << stats3.entities
              << " links="    << stats3.links << "\n";

    bool ok =
        r.facts_extracted > 0 &&
        !rc.results.empty() &&
        mock_ptr->calls == 1 &&
        r2.unit_ids.size() == 1 &&
        mock_text_seen &&
        stats3.units > 0;

    std::cout << (ok ? "\n[OK] smoke test passed\n" : "\n[FAIL] smoke test failed\n");
    return ok ? 0 : 1;
}
&&
        !rc.results.empty() &&
        mock_ptr->calls == 1 &&
        r2.unit_ids.size() == 1 &&
        mock_text_seen &&
        stats3.units > 0;

    std::cout << (ok ? "\n[OK] smoke test passed\n" : "\n[FAIL] smoke test failed\n");
    return ok ? 0 : 1;
}
