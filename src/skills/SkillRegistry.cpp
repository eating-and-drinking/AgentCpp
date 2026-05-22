#include <skills/SkillRegistry.hpp>
#include <utils/Logger.hpp>
#include <utils/StringUtils.hpp>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace agentcpp::skills {

namespace {

// Returns true if `line` is a frontmatter delimiter (line containing only "---").
bool isDelimiter(const std::string& line) {
    auto trimmed = utils::trim(line);
    return trimmed == "---";
}

// Read everything from an open stream into a string.
std::string slurp(std::ifstream& in) {
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Split a string into lines, preserving order but stripping the trailing '\n'.
std::vector<std::string> splitLines(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == '\n') { out.push_back(cur); cur.clear(); }
        else if (c != '\r') { cur += c; }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// Parse a very small subset of YAML frontmatter:
//   key: value
//   key: |
//     multi-line value
//     continued
// Returns map of key -> value (values trimmed).
std::unordered_map<std::string, std::string>
parseFrontmatter(const std::vector<std::string>& lines) {
    std::unordered_map<std::string, std::string> result;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string& line = lines[i];
        // Find first ':' that isn't inside a value
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = utils::trim(line.substr(0, colon));
        std::string val = utils::trim(line.substr(colon + 1));
        if (key.empty()) continue;

        // Block scalar: `key: |` (literal) or `key: >` (folded).
        // Both are treated as literal here — we don't need exact YAML semantics.
        if (val == "|" || val == ">") {
            std::string acc;
            // Continuation lines are those indented more than the key line.
            // We take any line that starts with whitespace or is empty.
            ++i;
            while (i < lines.size()) {
                const std::string& cont = lines[i];
                if (!cont.empty() && cont[0] != ' ' && cont[0] != '\t') break;
                if (!acc.empty()) acc += "\n";
                acc += utils::trim(cont);
                ++i;
            }
            --i; // step back; the outer loop will ++ again
            result[key] = acc;
        } else {
            // Strip optional surrounding quotes
            if (val.size() >= 2 &&
                ((val.front() == '"'  && val.back() == '"') ||
                 (val.front() == '\'' && val.back() == '\''))) {
                val = val.substr(1, val.size() - 2);
            }
            result[key] = val;
        }
    }
    return result;
}

} // namespace

std::optional<Skill>
SkillRegistry::parseSkillFile(const std::filesystem::path& skill_md) {
    std::ifstream in(skill_md);
    if (!in) {
        LOG_WARN("Cannot open SKILL.md: " + skill_md.string());
        return std::nullopt;
    }
    std::string raw = slurp(in);
    auto lines = splitLines(raw);

    // Find first non-empty, non-comment line — must be `---`
    std::size_t i = 0;
    while (i < lines.size() && utils::trim(lines[i]).empty()) ++i;
    if (i >= lines.size() || !isDelimiter(lines[i])) {
        LOG_WARN("Missing frontmatter opener in " + skill_md.string());
        return std::nullopt;
    }
    std::size_t fm_start = i + 1;

    // Find closing `---`
    std::size_t fm_end = fm_start;
    while (fm_end < lines.size() && !isDelimiter(lines[fm_end])) ++fm_end;
    if (fm_end >= lines.size()) {
        LOG_WARN("Missing frontmatter closer in " + skill_md.string());
        return std::nullopt;
    }

    std::vector<std::string> fm_lines(lines.begin() + fm_start, lines.begin() + fm_end);
    auto fields = parseFrontmatter(fm_lines);

    auto name_it = fields.find("name");
    auto desc_it = fields.find("description");
    if (name_it == fields.end() || name_it->second.empty()) {
        LOG_WARN("SKILL.md missing 'name' field: " + skill_md.string());
        return std::nullopt;
    }

    // Body is everything after the closing `---`
    std::ostringstream body_ss;
    for (std::size_t j = fm_end + 1; j < lines.size(); ++j) {
        body_ss << lines[j] << "\n";
    }

    Skill s;
    s.name          = name_it->second;
    s.description   = (desc_it != fields.end()) ? desc_it->second : "";
    s.body          = body_ss.str();
    s.skill_md_path = skill_md;
    s.dir           = skill_md.parent_path();
    return s;
}

int SkillRegistry::addRoot(const std::filesystem::path& root) {
    namespace fs = std::filesystem;

    std::error_code ec;
    if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) {
        LOG_DEBUG("Skills root not present, skipping: " + root.string());
        return 0;
    }

    int loaded = 0;
    for (auto& entry : fs::directory_iterator(root, ec)) {
        if (!entry.is_directory()) continue;
        auto skill_md = entry.path() / "SKILL.md";
        if (!fs::exists(skill_md, ec)) continue;

        auto parsed = parseSkillFile(skill_md);
        if (!parsed) continue;

        // Collision check: first one wins, later same-named skills are skipped.
        if (by_name_.count(parsed->name)) {
            LOG_WARN("Duplicate skill name '" + parsed->name +
                     "' at " + skill_md.string() + " — keeping first");
            continue;
        }
        by_name_[parsed->name] = skills_.size();
        skills_.push_back(std::move(*parsed));
        ++loaded;
    }
    LOG_DEBUG("Loaded " + std::to_string(loaded) + " skill(s) from " + root.string());
    return loaded;
}

const Skill* SkillRegistry::find(const std::string& name) const {
    auto it = by_name_.find(name);
    if (it == by_name_.end()) return nullptr;
    return &skills_[it->second];
}

} // namespace agentcpp::skills
