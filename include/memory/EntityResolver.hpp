#pragma once
//
// Entity resolution. Mirrors hindsight_api/engine/entity_resolver.py.
//
// hindsight uses string similarity + LLM disambiguation to merge variants
// like "Alice", "alice", "Alice Smith". We approximate with canonicalisation
// (lowercase + trim) and exact-name lookup against the bank's existing
// entities. New names are inserted as new entities.
//
#include <memory/MemoryStorage.hpp>
#include <memory/MemoryTypes.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace agentcpp::memory {

class EntityResolver {
public:
    explicit EntityResolver(MemoryStorage& storage) : storage_(storage) {}

    // Resolve one entity name within a bank. Returns the (possibly newly
    // created) entity. Persists creation/update of mention_count + last_seen.
    Entity resolveOrCreate(const std::string& bank_id,
                           const std::string& name);

    // Resolve a batch of names. Names that canonicalise to the same key are
    // deduplicated. The returned vector is parallel to `names`.
    std::vector<Entity> resolveBatch(const std::string& bank_id,
                                     const std::vector<std::string>& names);

    static std::string canonicalize(const std::string& name);

private:
    MemoryStorage& storage_;

    // bank_id -> (canonical_name -> entity_id), built lazily.
    std::unordered_map<std::string,
        std::unordered_map<std::string, std::string>> index_;

    void primeBankIndex(const std::string& bank_id);
};

} // namespace agentcpp::memory
