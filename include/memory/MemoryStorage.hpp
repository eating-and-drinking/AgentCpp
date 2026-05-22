#pragma once
//
// File-backed persistence for the hindsight-style memory subsystem.
//
// hindsight stores everything in PostgreSQL with pgvector. AgentCpp targets a
// single-binary CLI agent, so we replace the DB with a directory of plain
// JSON files. Layout:
//
//     <root>/
//       banks/
//         <bank_id>/
//           bank.json
//           documents/<doc_id>.json
//           units/<unit_id>.json
//           entities/<entity_id>.json
//           links/<link_key>.json
//           mental_models/<mm_id>.json
//
// All names are validated against the same traversal rules as the legacy
// MemoryStore (no `..`, no leading dot, no absolutes, no control chars).
//
#include <memory/MemoryTypes.hpp>
#include <filesystem>
#include <optional>
#include <vector>

namespace agentcpp::memory {

class MemoryStorage {
public:
    // root may be empty — the default location is resolved.
    explicit MemoryStorage(std::filesystem::path root = {}, bool create = true);

    bool                            isReady() const { return ready_; }
    const std::filesystem::path&    root()    const { return root_; }

    // ── Bank ──────────────────────────────────────────────────────────────
    std::vector<std::string>        listBankIds() const;
    bool                            bankExists(const std::string& bank_id) const;
    std::optional<Bank>             readBank   (const std::string& bank_id) const;
    bool                            writeBank  (const Bank& bank);
    bool                            deleteBank (const std::string& bank_id);

    // ── MemoryUnit ────────────────────────────────────────────────────────
    std::optional<MemoryUnit>       readUnit  (const std::string& bank_id,
                                               const std::string& unit_id) const;
    bool                            writeUnit (const MemoryUnit& unit);
    bool                            deleteUnit(const std::string& bank_id,
                                               const std::string& unit_id);
    std::vector<MemoryUnit>         listUnits (const std::string& bank_id) const;

    // ── Entity ────────────────────────────────────────────────────────────
    std::optional<Entity>           readEntity   (const std::string& bank_id,
                                                  const std::string& entity_id) const;
    bool                            writeEntity  (const Entity& e);
    bool                            deleteEntity (const std::string& bank_id,
                                                  const std::string& entity_id);
    std::vector<Entity>             listEntities (const std::string& bank_id) const;

    // ── MemoryLink ────────────────────────────────────────────────────────
    bool                            writeLink (const std::string& bank_id,
                                               const MemoryLink&  link);
    bool                            deleteLink(const std::string& bank_id,
                                               const MemoryLink&  link);
    std::vector<MemoryLink>         listLinks (const std::string& bank_id) const;

    // ── Document ──────────────────────────────────────────────────────────
    std::optional<Document>         readDocument  (const std::string& bank_id,
                                                   const std::string& doc_id) const;
    bool                            writeDocument (const Document& d);
    bool                            deleteDocument(const std::string& bank_id,
                                                   const std::string& doc_id);
    std::vector<Document>           listDocuments (const std::string& bank_id) const;

    // ── MentalModel ───────────────────────────────────────────────────────
    std::optional<MentalModel>      readMentalModel  (const std::string& bank_id,
                                                      const std::string& mm_id) const;
    bool                            writeMentalModel (const MentalModel& m);
    bool                            deleteMentalModel(const std::string& bank_id,
                                                      const std::string& mm_id) const;
    std::vector<MentalModel>        listMentalModels (const std::string& bank_id) const;

    // Resolve and return the default root directory.
    static std::filesystem::path    defaultRoot();

private:
    std::filesystem::path           root_;
    bool                            ready_ = false;

    std::filesystem::path bankDir(const std::string& bank_id) const;
    std::filesystem::path unitsDir(const std::string& bank_id) const;
    std::filesystem::path entitiesDir(const std::string& bank_id) const;
    std::filesystem::path linksDir(const std::string& bank_id) const;
    std::filesystem::path documentsDir(const std::string& bank_id) const;
    std::filesystem::path mentalModelsDir(const std::string& bank_id) const;

    bool ensureBankDirs(const std::string& bank_id);

    static bool          idIsSafe(const std::string& s);
    static bool          writeJsonFile(const std::filesystem::path& p, const json& j);
    static std::optional<json> readJsonFile(const std::filesystem::path& p);
};

} // namespace agentcpp::memory
