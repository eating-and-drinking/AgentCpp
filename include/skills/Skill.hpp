#pragma once
#include <string>
#include <filesystem>

namespace agentcpp::skills {

// A single skill discovered on disk.
// Each skill lives in its own directory with a SKILL.md file containing
// YAML-frontmatter (name + description) followed by markdown body.
struct Skill {
    std::string           name;          // from frontmatter, e.g. "pdf"
    std::string           description;   // from frontmatter
    std::string           body;          // SKILL.md body (everything after second `---`)
    std::filesystem::path skill_md_path; // absolute path to SKILL.md
    std::filesystem::path dir;           // absolute path to the skill directory
};

} // namespace agentcpp::skills
