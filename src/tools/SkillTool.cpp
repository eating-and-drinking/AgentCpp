#include <tools/SkillTool.hpp>
#include <sstream>

namespace agentcpp::tools {

std::string SkillTool::description() const {
    std::ostringstream ss;
    ss << "Invoke a skill by name. Skills are reusable instructions/playbooks "
          "for specific tasks (creating PDFs, slides, spreadsheets, etc.). "
          "The tool returns the SKILL.md content for the requested skill — "
          "read and follow the instructions inside. ";
    if (registry_.size() == 0) {
        ss << "No skills are currently available.";
    } else {
        ss << "Available skills: ";
        bool first = true;
        for (const auto& s : registry_.all()) {
            if (!first) ss << ", ";
            ss << s.name;
            first = false;
        }
        ss << ".";
    }
    return ss.str();
}

json SkillTool::inputSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"name", {
                {"type", "string"},
                {"description", "Name of the skill to invoke (e.g. \"pdf\", \"xlsx\")."}
            }}
        }},
        {"required", json::array({"name"})}
    };
}

ToolCallResult SkillTool::execute(const json& input, const ToolContext& /*ctx*/) {
    std::string name = input.value("name", "");
    if (name.empty()) {
        return ToolCallResult::error("'name' is required");
    }

    const agentcpp::skills::Skill* s = registry_.find(name);
    if (!s) {
        std::ostringstream ss;
        ss << "Skill not found: '" << name << "'. ";
        if (registry_.size() == 0) {
            ss << "No skills are loaded. Set AGENTCPP_SKILLS_DIR or pass --skills-dir.";
        } else {
            ss << "Available: ";
            bool first = true;
            for (const auto& sk : registry_.all()) {
                if (!first) ss << ", ";
                ss << sk.name;
                first = false;
            }
            ss << ".";
        }
        return ToolCallResult::error(ss.str());
    }

    // Return the skill body verbatim, prefixed with a small header so the
    // model knows what just happened.
    std::ostringstream out;
    out << "# Skill: " << s->name << "\n";
    out << "# Source: " << s->skill_md_path.string() << "\n";
    out << "# Skill directory (other files/scripts here): " << s->dir.string() << "\n\n";
    out << s->body;
    return ToolCallResult::ok(out.str());
}

} // namespace agentcpp::tools
