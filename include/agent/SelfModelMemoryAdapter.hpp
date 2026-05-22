#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  SelfModelMemoryAdapter — bridges Layer 3 (SelfModelStore) to MemoryEngine.
//
//  Persists each SelfProposition as one MentalModel row in a dedicated bank
//  (default "metacog_self_model"). Reverse direction: loadFromExternal()
//  reads all rows back into the in-memory store on adapter wire-up.
//
//  Forward-declares MemoryEngine to keep this header free of memory headers,
//  which themselves pull in a fair amount.
// ─────────────────────────────────────────────────────────────────────────────
#include <agent/SelfModelStore.hpp>
#include <agent/SelfProposition.hpp>

#include <string>
#include <vector>

namespace agentcpp::memory { class MemoryEngine; struct MentalModel; }

namespace agentcpp::agent {

class SelfModelMemoryAdapter {
public:
    // Engine reference must outlive this adapter (typically owned by the
    // caller — e.g. QueryEngine). The dedicated bank is auto-created if it
    // does not exist.
    SelfModelMemoryAdapter(agentcpp::memory::MemoryEngine& engine,
                           std::string bank_id = "metacog_self_model");

    // Load all selfprop rows from the bank.
    std::vector<SelfProposition> load() const;

    // Upsert each proposition as a MentalModel row.
    void persist(const std::vector<SelfProposition>& props) const;

    // Wire `store`'s persist/load callbacks to this adapter and immediately
    // populate it from MemoryEngine.
    void wire(SelfModelStore& store) const;

    const std::string& bankId() const { return bank_id_; }

private:
    agentcpp::memory::MemoryEngine& engine_;
    std::string                     bank_id_;
};

// Pure converter helpers (declared here for testing; defined in the .cpp so
// MentalModel definitions only need to be pulled in there).
agentcpp::memory::MentalModel toMentalModel(const SelfProposition& p,
                                            const std::string& bank_id);
SelfProposition               fromMentalModel(const agentcpp::memory::MentalModel& m);

} // namespace agentcpp::agent
