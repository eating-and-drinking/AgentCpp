#include <memory/EntityResolver.hpp>
#include <utils/StringUtils.hpp>

#include <algorithm>
#include <cctype>

namespace agentcpp::memory {

std::string EntityResolver::canonicalize(const std::string& name) {
    std::string s = utils::trim(name);
    // collapse internal whitespace + lowercase
    std::string out;
    out.reserve(s.size());
    bool last_space = false;
    for (char c : s) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (std::isspace(uc)) {
            if (!last_space && !out.empty()) out += ' ';
            last_space = true;
        } else {
            out += static_cast<char>(std::tolower(uc));
            last_space = false;
        }
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

void EntityResolver::primeBankIndex(const std::string& bank_id) {
    if (index_.count(bank_id)) return;
    auto& m = index_[bank_id];
    for (const auto& e : storage_.listEntities(bank_id)) {
        m[canonicalize(e.canonical_name)] = e.id;
    }
}

Entity EntityResolver::resolveOrCreate(const std::string& bank_id,
                                       const std::string& name) {
    primeBankIndex(bank_id);
    auto& m = index_[bank_id];
    auto key = canonicalize(name);

    if (auto it = m.find(key); it != m.end()) {
        // Existing entity: bump mention_count + last_seen, persist, return.
        auto existing = storage_.readEntity(bank_id, it->second);
        if (existing) {
            existing->mention_count += 1;
            existing->last_seen      = nowUtc();
            storage_.writeEntity(*existing);
            return *existing;
        }
        // Stale index — drop and fall through to create.
        m.erase(it);
    }

    Entity e;
    e.id             = newUuid();
    e.bank_id        = bank_id;
    e.canonical_name = utils::trim(name);
    e.first_seen     = nowUtc();
    e.last_seen      = e.first_seen;
    e.mention_count  = 1;
    storage_.writeEntity(e);
    m[key] = e.id;
    return e;
}

std::vector<Entity>
EntityResolver::resolveBatch(const std::string& bank_id,
                             const std::vector<std::string>& names) {
    std::vector<Entity> out;
    out.reserve(names.size());
    for (const auto& n : names) {
        out.push_back(resolveOrCreate(bank_id, n));
    }
    return out;
}

} // namespace agentcpp::memory
