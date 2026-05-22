#pragma once
//
// TemplateReflectComposer — default IReflectComposer. Renders a deterministic
// markdown-style report. No model required.
//
#include <memory/providers/IReflectComposer.hpp>

namespace agentcpp::memory {

class TemplateReflectComposer final : public IReflectComposer {
public:
    bool        available() const override { return true; }
    std::string name()      const override { return "template"; }

    std::string compose(const Bank& bank,
                        const ReflectQuery& query,
                        const std::map<std::string, std::vector<MemoryUnit>>& based_on,
                        const std::vector<MentalModel>& mental_models) override;
};

} // namespace agentcpp::memory
