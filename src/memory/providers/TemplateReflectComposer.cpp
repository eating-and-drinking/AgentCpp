#include <memory/providers/TemplateReflectComposer.hpp>

#include <sstream>

namespace agentcpp::memory {

std::string TemplateReflectComposer::compose(
    const Bank& bank,
    const ReflectQuery& rq,
    const std::map<std::string, std::vector<MemoryUnit>>& based_on,
    const std::vector<MentalModel>& mental_models)
{
    std::ostringstream txt;
    txt << "Reflection on: " << rq.query << "\n";
    if (!rq.context.empty()) txt << "Context: " << rq.context << "\n";
    txt << "Bank: " << bank.bank_id;
    if (!bank.mission.empty()) txt << " — mission: " << bank.mission;
    txt << "\n\n";

    auto dump = [&](const char* label, const std::vector<MemoryUnit>& units) {
        if (units.empty()) return;
        txt << "## " << label << " (" << units.size() << ")\n";
        for (const auto& u : units) {
            txt << "- " << u.text;
            if (!u.context.empty()) txt << "  (" << u.context << ")";
            txt << "\n";
        }
        txt << "\n";
    };

    auto get = [&](const char* key) -> const std::vector<MemoryUnit>& {
        static const std::vector<MemoryUnit> empty;
        auto it = based_on.find(key);
        return it == based_on.end() ? empty : it->second;
    };

    const auto& world       = get("world");
    const auto& experience  = get("experience");
    const auto& observation = get("observation");

    dump("World facts",        world);
    dump("Experiential facts", experience);
    dump("Observations",       observation);

    if (!mental_models.empty()) {
        txt << "## Mental models in scope\n";
        for (const auto& m : mental_models) {
            txt << "- " << m.name;
            if (!m.summary.empty()) txt << ": " << m.summary;
            txt << "\n";
        }
        txt << "\n";
    }
    if (world.empty() && experience.empty() && observation.empty()) {
        txt << "(no matching memories found)\n";
    }
    return txt.str();
}

} // namespace agentcpp::memory
