#include <memory/MemoryStorage.hpp>
#include <utils/Logger.hpp>

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace agentcpp::memory {

namespace fs = std::filesystem;

namespace {

std::string envOr(const char* name, std::string fallback = "") {
    const char* v = std::getenv(name);
    if (v && *v) return std::string(v);
    return fallback;
}

} // namespace

// ─── default root resolution ──────────────────────────────────────────────────

fs::path MemoryStorage::defaultRoot() {
    // Honour both the legacy env var (back-compat) and a hindsight-style one.
    std::string explicit_dir = envOr("AGENTCPP_MEMORY_DIR");
    if (explicit_dir.empty()) explicit_dir = envOr("HINDSIGHT_MEMORY_DIR");
    if (!explicit_dir.empty()) return fs::path(explicit_dir);

#ifdef _WIN32
    std::string appdata = envOr("APPDATA");
    if (!appdata.empty()) return fs::path(appdata) / "agentcpp" / "memory";
    std::string home = envOr("USERPROFILE");
    if (!home.empty()) return fs::path(home) / ".agentcpp" / "memory";
#else
    std::string xdg = envOr("XDG_DATA_HOME");
    if (!xdg.empty()) return fs::path(xdg) / "agentcpp" / "memory";
    std::string home = envOr("HOME");
    if (!home.empty()) return fs::path(home) / ".agentcpp" / "memory";
#endif
    return fs::path(".agentcpp") / "memory";
}

// ─── ctor / readiness ─────────────────────────────────────────────────────────

MemoryStorage::MemoryStorage(fs::path root, bool create) {
    root_ = root.empty() ? defaultRoot() : std::move(root);

    std::error_code ec;
    if (create && !fs::exists(root_, ec)) {
        fs::create_directories(root_, ec);
        if (ec) {
            LOG_WARN("Could not create memory dir " + root_.string() + ": " + ec.message());
            ready_ = false;
            return;
        }
    }
    if (!fs::exists(root_, ec) || !fs::is_directory(root_, ec)) {
        LOG_WARN("Memory root not usable: " + root_.string());
        ready_ = false;
        return;
    }
    // banks/ subdir is created on demand.
    root_ = fs::weakly_canonical(root_, ec);
    ready_ = true;
    LOG_DEBUG("Memory storage ready at " + root_.string());
}

// ─── path helpers ─────────────────────────────────────────────────────────────

bool MemoryStorage::idIsSafe(const std::string& s) {
    if (s.empty() || s.size() > 256) return false;
    for (char c : s) {
        if (c == '/' || c == '\\' || c == '\0' || c < 0x20) return false;
    }
    if (s == "." || s == "..") return false;
    if (s.front() == '.') return false;
    return true;
}

fs::path MemoryStorage::bankDir(const std::string& bank_id) const {
    return root_ / "banks" / bank_id;
}
fs::path MemoryStorage::unitsDir       (const std::string& b) const { return bankDir(b) / "units"; }
fs::path MemoryStorage::entitiesDir    (const std::string& b) const { return bankDir(b) / "entities"; }
fs::path MemoryStorage::linksDir       (const std::string& b) const { return bankDir(b) / "links"; }
fs::path MemoryStorage::documentsDir   (const std::string& b) const { return bankDir(b) / "documents"; }
fs::path MemoryStorage::mentalModelsDir(const std::string& b) const { return bankDir(b) / "mental_models"; }

bool MemoryStorage::ensureBankDirs(const std::string& bank_id) {
    if (!ready_ || !idIsSafe(bank_id)) return false;
    std::error_code ec;
    fs::create_directories(unitsDir(bank_id),        ec);
    fs::create_directories(entitiesDir(bank_id),     ec);
    fs::create_directories(linksDir(bank_id),        ec);
    fs::create_directories(documentsDir(bank_id),    ec);
    fs::create_directories(mentalModelsDir(bank_id), ec);
    return !ec;
}

// ─── JSON I/O ─────────────────────────────────────────────────────────────────

bool MemoryStorage::writeJsonFile(const fs::path& p, const json& j) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << j.dump(2);
    return static_cast<bool>(out);
}

std::optional<json> MemoryStorage::readJsonFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return std::nullopt;
    try {
        json j;
        in >> j;
        return j;
    } catch (const std::exception& e) {
        LOG_WARN("memory: failed to parse " + p.string() + ": " + e.what());
        return std::nullopt;
    }
}

// ─── Bank ─────────────────────────────────────────────────────────────────────

std::vector<std::string> MemoryStorage::listBankIds() const {
    std::vector<std::string> out;
    if (!ready_) return out;
    auto banks_root = root_ / "banks";
    std::error_code ec;
    if (!fs::exists(banks_root, ec)) return out;
    for (auto& entry : fs::directory_iterator(banks_root, ec)) {
        if (ec) break;
        if (!entry.is_directory()) continue;
        out.push_back(entry.path().filename().string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool MemoryStorage::bankExists(const std::string& bank_id) const {
    if (!ready_ || !idIsSafe(bank_id)) return false;
    std::error_code ec;
    return fs::exists(bankDir(bank_id) / "bank.json", ec);
}

std::optional<Bank> MemoryStorage::readBank(const std::string& bank_id) const {
    if (!ready_ || !idIsSafe(bank_id)) return std::nullopt;
    auto j = readJsonFile(bankDir(bank_id) / "bank.json");
    if (!j) return std::nullopt;
    return j->get<Bank>();
}

bool MemoryStorage::writeBank(const Bank& b) {
    if (!ready_ || !idIsSafe(b.bank_id)) return false;
    if (!ensureBankDirs(b.bank_id)) return false;
    json j = b;
    return writeJsonFile(bankDir(b.bank_id) / "bank.json", j);
}

bool MemoryStorage::deleteBank(const std::string& bank_id) {
    if (!ready_ || !idIsSafe(bank_id)) return false;
    std::error_code ec;
    fs::remove_all(bankDir(bank_id), ec);
    return !ec;
}

// ─── MemoryUnit ───────────────────────────────────────────────────────────────

std::optional<MemoryUnit> MemoryStorage::readUnit(const std::string& bank_id,
                                                  const std::string& unit_id) const {
    if (!idIsSafe(bank_id) || !idIsSafe(unit_id)) return std::nullopt;
    auto j = readJsonFile(unitsDir(bank_id) / (unit_id + ".json"));
    if (!j) return std::nullopt;
    return j->get<MemoryUnit>();
}

bool MemoryStorage::writeUnit(const MemoryUnit& u) {
    if (!idIsSafe(u.bank_id) || !idIsSafe(u.id)) return false;
    if (!ensureBankDirs(u.bank_id)) return false;
    json j = u;
    return writeJsonFile(unitsDir(u.bank_id) / (u.id + ".json"), j);
}

bool MemoryStorage::deleteUnit(const std::string& bank_id, const std::string& unit_id) {
    if (!idIsSafe(bank_id) || !idIsSafe(unit_id)) return false;
    std::error_code ec;
    return fs::remove(unitsDir(bank_id) / (unit_id + ".json"), ec);
}

std::vector<MemoryUnit> MemoryStorage::listUnits(const std::string& bank_id) const {
    std::vector<MemoryUnit> out;
    if (!idIsSafe(bank_id)) return out;
    auto dir = unitsDir(bank_id);
    std::error_code ec;
    if (!fs::exists(dir, ec)) return out;
    for (auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".json") continue;
        auto j = readJsonFile(entry.path());
        if (!j) continue;
        out.push_back(j->get<MemoryUnit>());
    }
    return out;
}

// ─── Entity ───────────────────────────────────────────────────────────────────

std::optional<Entity> MemoryStorage::readEntity(const std::string& bank_id,
                                                const std::string& entity_id) const {
    if (!idIsSafe(bank_id) || !idIsSafe(entity_id)) return std::nullopt;
    auto j = readJsonFile(entitiesDir(bank_id) / (entity_id + ".json"));
    if (!j) return std::nullopt;
    return j->get<Entity>();
}

bool MemoryStorage::writeEntity(const Entity& e) {
    if (!idIsSafe(e.bank_id) || !idIsSafe(e.id)) return false;
    if (!ensureBankDirs(e.bank_id)) return false;
    json j = e;
    return writeJsonFile(entitiesDir(e.bank_id) / (e.id + ".json"), j);
}

bool MemoryStorage::deleteEntity(const std::string& bank_id, const std::string& entity_id) {
    if (!idIsSafe(bank_id) || !idIsSafe(entity_id)) return false;
    std::error_code ec;
    return fs::remove(entitiesDir(bank_id) / (entity_id + ".json"), ec);
}

std::vector<Entity> MemoryStorage::listEntities(const std::string& bank_id) const {
    std::vector<Entity> out;
    if (!idIsSafe(bank_id)) return out;
    auto dir = entitiesDir(bank_id);
    std::error_code ec;
    if (!fs::exists(dir, ec)) return out;
    for (auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".json") continue;
        auto j = readJsonFile(entry.path());
        if (!j) continue;
        out.push_back(j->get<Entity>());
    }
    return out;
}

// ─── MemoryLink ───────────────────────────────────────────────────────────────

bool MemoryStorage::writeLink(const std::string& bank_id, const MemoryLink& l) {
    if (!idIsSafe(bank_id)) return false;
    if (!ensureBankDirs(bank_id)) return false;
    json j = l;
    return writeJsonFile(linksDir(bank_id) / (l.compositeKey() + ".json"), j);
}

bool MemoryStorage::deleteLink(const std::string& bank_id, const MemoryLink& l) {
    if (!idIsSafe(bank_id)) return false;
    std::error_code ec;
    return fs::remove(linksDir(bank_id) / (l.compositeKey() + ".json"), ec);
}

std::vector<MemoryLink> MemoryStorage::listLinks(const std::string& bank_id) const {
    std::vector<MemoryLink> out;
    if (!idIsSafe(bank_id)) return out;
    auto dir = linksDir(bank_id);
    std::error_code ec;
    if (!fs::exists(dir, ec)) return out;
    for (auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".json") continue;
        auto j = readJsonFile(entry.path());
        if (!j) continue;
        out.push_back(j->get<MemoryLink>());
    }
    return out;
}

// ─── Document ─────────────────────────────────────────────────────────────────

std::optional<Document> MemoryStorage::readDocument(const std::string& bank_id,
                                                    const std::string& doc_id) const {
    if (!idIsSafe(bank_id) || !idIsSafe(doc_id)) return std::nullopt;
    auto j = readJsonFile(documentsDir(bank_id) / (doc_id + ".json"));
    if (!j) return std::nullopt;
    return j->get<Document>();
}

bool MemoryStorage::writeDocument(const Document& d) {
    if (!idIsSafe(d.bank_id) || !idIsSafe(d.id)) return false;
    if (!ensureBankDirs(d.bank_id)) return false;
    json j = d;
    return writeJsonFile(documentsDir(d.bank_id) / (d.id + ".json"), j);
}

bool MemoryStorage::deleteDocument(const std::string& bank_id, const std::string& doc_id) {
    if (!idIsSafe(bank_id) || !idIsSafe(doc_id)) return false;
    std::error_code ec;
    return fs::remove(documentsDir(bank_id) / (doc_id + ".json"), ec);
}

std::vector<Document> MemoryStorage::listDocuments(const std::string& bank_id) const {
    std::vector<Document> out;
    if (!idIsSafe(bank_id)) return out;
    auto dir = documentsDir(bank_id);
    std::error_code ec;
    if (!fs::exists(dir, ec)) return out;
    for (auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".json") continue;
        auto j = readJsonFile(entry.path());
        if (!j) continue;
        out.push_back(j->get<Document>());
    }
    return out;
}

// ─── MentalModel ──────────────────────────────────────────────────────────────

std::optional<MentalModel> MemoryStorage::readMentalModel(const std::string& bank_id,
                                                          const std::string& mm_id) const {
    if (!idIsSafe(bank_id) || !idIsSafe(mm_id)) return std::nullopt;
    auto j = readJsonFile(mentalModelsDir(bank_id) / (mm_id + ".json"));
    if (!j) return std::nullopt;
    return j->get<MentalModel>();
}

bool MemoryStorage::writeMentalModel(const MentalModel& m) {
    if (!idIsSafe(m.bank_id) || !idIsSafe(m.id)) return false;
    if (!ensureBankDirs(m.bank_id)) return false;
    json j = m;
    return writeJsonFile(mentalModelsDir(m.bank_id) / (m.id + ".json"), j);
}

bool MemoryStorage::deleteMentalModel(const std::string& bank_id,
                                      const std::string& mm_id) const {
    if (!idIsSafe(bank_id) || !idIsSafe(mm_id)) return false;
    std::error_code ec;
    return fs::remove(mentalModelsDir(bank_id) / (mm_id + ".json"), ec);
}

std::vector<MentalModel> MemoryStorage::listMentalModels(const std::string& bank_id) const {
    std::vector<MentalModel> out;
    if (!idIsSafe(bank_id)) return out;
    auto dir = mentalModelsDir(bank_id);
    std::error_code ec;
    if (!fs::exists(dir, ec)) return out;
    for (auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".json") continue;
        auto j = readJsonFile(entry.path());
        if (!j) continue;
        out.push_back(j->get<MentalModel>());
    }
    return out;
}

} // namespace agentcpp::memory
