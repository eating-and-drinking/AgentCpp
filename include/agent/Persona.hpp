#pragma once
#include <nlohmann/json.hpp>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace agentcpp::agent {

// ── Persona ───────────────────────────────────────────────────────────────────
//
// A Persona is a reusable "stance" for the agent: who it is (mission), how it
// speaks (style), which tool groups it has access to, and which skills it
// loads by default. Personas turn `agentcpp` from a single coding assistant
// into a multi-role runtime — `agentcpp researcher`, `agentcpp office`, etc.
//
// Source of truth: 5 hardcoded builtins in src/agent/Persona.cpp.
// Overlay path:    <persona-dir>/*.yaml (last-write-wins; same id replaces
//                  the builtin). The overlay is parsed by a tiny YAML subset
//                  parser embedded in Persona.cpp — no external dependency.
//
struct Persona {
    std::string              id;              // stable key: "general", "researcher", ...
    std::string              display_name;    // human label: "Research Analyst"
    std::string              mission;         // first paragraph of the system prompt
    std::string              style;           // tone / formatting guidance
    std::vector<std::string> toolsets;        // e.g. {"core","web","doc","memory"}
    std::vector<std::string> default_skills;  // skill ids to mount when this persona is active
    nlohmann::json           extras = nlohmann::json::object();  // persona-specific config

    // Render the persona section of a system prompt (mission + style headings).
    // Returns the empty string if the persona is empty (defensive).
    std::string toSystemPromptSection() const;

    // For --list-personas. Single-line summary.
    std::string oneLineSummary() const;
};

// ── PersonaRegistry ──────────────────────────────────────────────────────────
//
// Holds every known persona. Construction registers the 5 builtins.
// `loadFromDir(p)` overlays YAML files from p, replacing builtins on id clash.
//
class PersonaRegistry {
public:
    PersonaRegistry();                         // registers builtins
    void                          registerBuiltins();    // idempotent
    void                          registerPersona(Persona p);  // upsert by id
    const Persona*                find(std::string_view id) const;
    std::vector<Persona>          all() const;
    std::size_t                   size() const { return personas_.size(); }

    // Load <dir>/*.yaml (non-recursive). Silently no-ops if dir doesn't exist.
    // Returns the number of personas that were added or overwritten.
    std::size_t                   loadFromDir(const std::filesystem::path& dir);

    // Default overlay search path:
    //   $AGENTCPP_PERSONA_DIR (if set)
    //   else  $XDG_CONFIG_HOME/agentcpp/personas
    //   else  $HOME/.agentcpp/personas
    //   on Windows: %APPDATA%/agentcpp/personas
    static std::filesystem::path  defaultPersonaDir();

    // Returns "general" — the fallback when no persona is selected.
    static std::string            defaultPersonaId() { return "general"; }

private:
    std::vector<Persona> personas_;
};

// ── Free helpers ─────────────────────────────────────────────────────────────
//
// Parse a single .yaml file with the limited subset of YAML we accept:
//   id: foo
//   display_name: Foo Bar
//   mission: |
//     multi-line
//     paragraph
//   style: "single line"
//   toolsets: [core, web]
//   default_skills:
//     - docx
//     - pdf
//
// Returns nullopt on parse failure (caller logs).
std::optional<Persona> parsePersonaYaml(const std::string& yaml_text);

} // namespace agentcpp::agent
