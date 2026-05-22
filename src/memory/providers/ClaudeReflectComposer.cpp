#include <memory/providers/ClaudeReflectComposer.hpp>

#include <api/ClaudeClient.hpp>
#include <api/Types.hpp>
#include <utils/Logger.hpp>

#include <sstream>

namespace agentcpp::memory {

namespace {

std::string dispositionLine(const DispositionTraits& d) {
    std::ostringstream s;
    s << "skepticism=" << d.skepticism
      << " literalism=" << d.literalism
      << " empathy="    << d.empathy;
    return s.str();
}

std::string buildPrompt(const Bank& bank,
                        const ReflectQuery& rq,
                        const std::map<std::string, std::vector<MemoryUnit>>& based_on,
                        const std::vector<MentalModel>& mental_models)
{
    std::ostringstream p;
    p << "You are answering on behalf of memory bank '" << bank.bank_id << "'.\n";
    if (!bank.mission.empty()) p << "Bank mission: " << bank.mission << "\n";
    p << "Disposition (1-5): " << dispositionLine(bank.disposition) << "\n\n";

    p << "Question: " << rq.query << "\n";
    if (!rq.context.empty()) p << "Extra context: " << rq.context << "\n";
    p << "\n";

    auto dump = [&](const char* label, const std::vector<MemoryUnit>* units) {
        if (!units || units->empty()) return;
        p << "## " << label << "\n";
        for (const auto& u : *units) {
            p << "- " << u.text;
            if (!u.context.empty()) p << "  (context: " << u.context << ")";
            p << "\n";
        }
        p << "\n";
    };
    auto get = [&](const char* k) -> const std::vector<MemoryUnit>* {
        auto it = based_on.find(k);
        return it == based_on.end() ? nullptr : &it->second;
    };
    dump("World facts",        get("world"));
    dump("Experiential facts", get("experience"));
    dump("Observations",       get("observation"));

    if (!mental_models.empty()) {
        p << "## Mental models in scope\n";
        for (const auto& m : mental_models) {
            p << "- " << m.name;
            if (!m.summary.empty()) p << ": " << m.summary;
            p << "\n";
        }
        p << "\n";
    }

    p << "Respond with a concise, well-grounded answer that cites the facts above. "
         "Do not invent facts beyond what is shown.";
    return p.str();
}

std::string textFrom(const agentcpp::api::ApiResponse& resp) {
    std::string out;
    for (const auto& blk : resp.content) {
        if (auto* t = std::get_if<agentcpp::api::TextBlock>(&blk)) out += t->text;
    }
    return out;
}

} // namespace

ClaudeReflectComposer::ClaudeReflectComposer(
        std::shared_ptr<agentcpp::api::ClaudeClient> client,
        std::string model)
    : client_(std::move(client))
    , model_(std::move(model)) {
    name_ = "claude";
    if (!model_.empty()) { name_ += '/'; name_ += model_; }
}

bool ClaudeReflectComposer::available() const {
    return client_ && !client_->config().api_key.empty();
}

std::string ClaudeReflectComposer::compose(
        const Bank& bank,
        const ReflectQuery& rq,
        const std::map<std::string, std::vector<MemoryUnit>>& based_on,
        const std::vector<MentalModel>& mental_models)
{
    if (!available()) return fallback_.compose(bank, rq, based_on, mental_models);

    agentcpp::api::ApiRequest req;
    req.model      = model_.empty() ? client_->config().default_model : model_;
    req.max_tokens = 2048;
    req.stream     = false;
    req.system     = "You synthesize memory-grounded answers. "
                     "Be precise and faithful to the supplied facts.";
    req.messages.push_back(agentcpp::api::Message::userText(
        buildPrompt(bank, rq, based_on, mental_models)));

    try {
        auto resp = client_->request(req);
        auto text = textFrom(resp);
        if (text.empty()) return fallback_.compose(bank, rq, based_on, mental_models);
        return text;
    } catch (const std::exception& e) {
        LOG_WARN(std::string("ClaudeReflectComposer: API call failed: ") + e.what());
        return fallback_.compose(bank, rq, based_on, mental_models);
    }
}

} // namespace agentcpp::memory
