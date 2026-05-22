#include <agent/SelfModelMemoryAdapter.hpp>

#include <memory/MemoryEngine.hpp>
#include <memory/MemoryTypes.hpp>
#include <utils/Logger.hpp>

#include <nlohmann/json.hpp>

namespace agentcpp::agent {

namespace {

using nlohmann::json;
using agentcpp::memory::MentalModel;

// Encode the structured fields we cannot store in MentalModel's plain text
// fields into a JSON blob placed in the `summary` field.  Lossless round-trip
// with fromMentalModel().
std::string encodeSummary(const SelfProposition& p) {
    json j;
    j["tags"]           = p.tags;
    j["confidence"]     = p.confidence;
    j["evidence_count"] = p.evidence_count;
    j["created_at_ms"]  = p.created_at;
    j["updated_at_ms"]  = p.updated_at;
    return j.dump();
}

void decodeSummary(const std::string& s, SelfProposition& p) {
    if (s.empty()) return;
    try {
        auto j = json::parse(s);
        if (j.contains("tags") && j["tags"].is_array()) {
            p.tags = j["tags"].get<std::vector<std::string>>();
        }
        if (j.contains("confidence"))     p.confidence     = j["confidence"].get<double>();
        if (j.contains("evidence_count")) p.evidence_count = j["evidence_count"].get<int>();
        if (j.contains("created_at_ms"))  p.created_at     = j["created_at_ms"].get<std::int64_t>();
        if (j.contains("updated_at_ms"))  p.updated_at     = j["updated_at_ms"].get<std::int64_t>();
    } catch (const std::exception& e) {
        // Tolerate corrupt rows — drop the structured part, keep the text.
        LOG_WARN(std::string("SelfModelMemoryAdapter: bad summary JSON: ") + e.what());
    }
}

} // anonymous namespace

MentalModel toMentalModel(const SelfProposition& p, const std::string& bank_id) {
    MentalModel m;
    m.id          = p.id;
    m.bank_id     = bank_id;
    m.name        = "selfprop";
    m.description = p.text;
    m.summary     = encodeSummary(p);
    return m;
}

SelfProposition fromMentalModel(const MentalModel& m) {
    SelfProposition p;
    p.id   = m.id;
    p.text = m.description;
    decodeSummary(m.summary, p);
    return p;
}

SelfModelMemoryAdapter::SelfModelMemoryAdapter(
    agentcpp::memory::MemoryEngine& engine,
    std::string bank_id)
    : engine_(engine), bank_id_(std::move(bank_id)) {
    // Ensure the bank exists so subsequent upserts always succeed.
    engine_.getOrCreateBank(bank_id_);
}

std::vector<SelfProposition> SelfModelMemoryAdapter::load() const {
    std::vector<SelfProposition> out;
    auto models = engine_.listMentalModels(bank_id_);
    out.reserve(models.size());
    for (auto& m : models) {
        if (m.name == "selfprop") out.push_back(fromMentalModel(m));
    }
    return out;
}

void SelfModelMemoryAdapter::persist(const std::vector<SelfProposition>& props) const {
    for (auto& p : props) {
        engine_.upsertMentalModel(bank_id_, toMentalModel(p, bank_id_));
    }
}

void SelfModelMemoryAdapter::wire(SelfModelStore& store) const {
    // Capture by value of `this` — adapter must outlive the store, which is
    // guaranteed when both are owned by QueryEngine.
    store.setLoadFn   ([this]{ return this->load(); });
    store.setPersistFn([this](const std::vector<SelfProposition>& props) {
        this->persist(props);
    });
    // Immediately populate the in-memory store from persistent storage so
    // the very first turn after wire-up sees cross-episode knowledge.
    store.loadFromExternal();
}

} // namespace agentcpp::agent
