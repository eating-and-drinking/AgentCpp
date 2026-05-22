#pragma once
#include <skills/Skill.hpp>
#include <string>
#include <vector>
#include <filesystem>
#include <optional>
#include <unordered_map>

namespace agentcpp::skills {

// Discovers and serves skills found under one or more skill root directories.
//
// Layout expected:
//   <root>/
//     <skill-name>/
//       SKILL.md      <-- required, YAML frontmatter + markdown body
//       <other files> <-- ignored by the registry, available to the skill at runtime
//
// SKILL.md format:
//   ---
//   name: pdf
//   description: |
//     Use this skill when the user asks about PDF files...
//   ---
//   # Body markdown follows...
//
// Notes:
// - Only `name` and `description` are read from frontmatter.
// - Multi-line `description: |` values are supported.
// - Discovery is non-recursive past the first level (skills are flat).
// - If a skill's frontmatter is malformed it is silently skipped (warning logged).
class SkillRegistry {
public:
    SkillRegistry() = default;

    // Scan the given root directory and populate the registry.
    // Multiple roots can be added by calling addRoot() repeatedly.
    // Returns the number of skills successfully loaded (across this call).
    int addRoot(const std::filesystem::path& root);

    // All loaded skills, in discovery order.
    const std::vector<Skill>& all() const { return skills_; }

    // Look up a skill by name (returns nullptr if not found).
    const Skill* find(const std::string& name) const;

    // Number of skills currently loaded.
    std::size_t size() const { return skills_.size(); }

    // Parse a SKILL.md file at the given path. Public so tests can use it.
    // Returns std::nullopt on parse failure.
    static std::optional<Skill> parseSkillFile(const std::filesystem::path& skill_md);

private:
    std::vector<Skill>                              skills_;
    std::unordered_map<std::string, std::size_t>    by_name_; // name -> index in skills_
};

} // namespace agentcpp::skills
