#pragma once
#include <memory/MemoryProviders.hpp>
#include <memory/MemoryStorage.hpp>
#include <memory/MemoryTypes.hpp>
#include <filesystem>
#include <string>
#include <vector>

namespace agentcpp::memory {

class MemoryEngine {
public:
    explicit MemoryEngine(std::filesystem::path root = {}, bool create = true);

    MemoryEngine(std::filesystem::path root,
                 MemoryProviders       providers,
                 bool                  create = true);

    void setProviders(MemoryProviders providers);

    std::string factExtractorName()    const;
    std::string embedderName()         const;
    std::string rerankerName()         const;
    std::string reflectComposerName()  const;

    bool                            isReady() const { return storage_.isReady(); }
    const std::filesystem::path&    root()    const { return storage_.root(); }
    MemoryStorage&                  storage()       { return storage_; }
    const MemoryStorage&            storage() const { return storage_; }

    RetainResult retain(const std::string& bank_id,
                        const std::vector<RetainContent>& contents);
    RetainResult retain(const std::string& bank_id, const RetainContent& content);
    RecallResult recall(const std::string& bank_id, const RecallQuery& query);
    ReflectResult reflect(const std::string& bank_id, const ReflectQuery& query);

    std::vector<std::string>  listBanks() const;
    Bank                      getOrCreateBank(const std::string& bank_id);
    std::optional<Bank>       getBank(const std::string& bank_id) const;
    bool                      setBankMission(const std::string& bank_id, const std::string& mission);
    bool                      setBankDisposition(const std::string& bank_id, const DispositionTraits& d);
    bool                      deleteBank(const std::string& bank_id);

    std::vector<MemoryUnit>   listUnits(const std::string& bank_id,
                                        std::optional<FactType> filter = std::nullopt,
                                        std::size_t limit = 100,
                                        std::size_t offset = 0) const;
    std::vector<Entity>       listEntities(const std::string& bank_id,
                                           std::size_t limit = 100,
                                           std::size_t offset = 0) const;
    std::vector<MemoryLink>   listLinks(const std::string& bank_id) const;
    std::vector<MentalModel>  listMentalModels(const std::string& bank_id) const;
    std::vector<Document>     listDocuments(const std::string& bank_id) const;

    std::optional<MemoryUnit> getUnit(const std::string& bank_id, const std::string& unit_id) const;
    std::optional<Entity>     getEntity(const std::string& bank_id, const std::string& entity_id) const;

    struct BankStats {
        std::size_t units = 0;
        std::size_t world = 0;
        std::size_t experience = 0;
        std::size_t observation = 0;
        std::size_t entities = 0;
        std::size_t links = 0;
        std::size_t documents = 0;
    };
    BankStats getBankStats(const std::string& bank_id) const;

    MentalModel upsertMentalModel(const std::string& bank_id, MentalModel model);
    bool        deleteMentalModel(const std::string& bank_id, const std::string& mm_id);

private:
    MemoryStorage   storage_;
    MemoryProviders providers_;

    void fillMissingProviders();
};

} // namespace agentcpp::memory
