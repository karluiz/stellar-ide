#include "composer.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>

namespace raven {
namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string trim(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string shortened(const std::string& value, size_t limit) {
    return value.size() <= limit ? value : value.substr(0, limit - 1) + "…";
}

CompletionItem read_skill(const std::filesystem::path& path, bool directory_form) {
    std::ifstream input(path);
    std::string line;
    std::string name;
    std::string description;
    bool frontmatter = false;
    bool closed = false;
    while (std::getline(input, line)) {
        const auto clean = trim(line);
        if (!frontmatter && clean == "---") {
            frontmatter = true;
            continue;
        }
        if (frontmatter && !closed && clean == "---") {
            closed = true;
            continue;
        }
        if (frontmatter && !closed) {
            const auto colon = clean.find(':');
            if (colon == std::string::npos) continue;
            const auto key = trim(clean.substr(0, colon));
            const auto value = trim(clean.substr(colon + 1));
            if (key == "name") name = value;
            else if (key == "description") description = value;
            continue;
        }
        if (!directory_form && description.empty() && !clean.empty()) description = clean;
    }
    if (!directory_form && name.empty()) name = path.stem().string();
    if (name.empty() || description.empty()) return {};
    return {name, shortened(description, 72)};
}

void scan_skills(const std::filesystem::path& root, std::map<std::string, CompletionItem>& result) {
    std::error_code error;
    if (!std::filesystem::is_directory(root, error)) return;
    for (const auto& entry : std::filesystem::directory_iterator(root, error)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".md") continue;
        auto skill = read_skill(entry.path(), false);
        if (!skill.text.empty()) result[lower(skill.text)] = std::move(skill);
    }
    for (const auto& entry : std::filesystem::directory_iterator(root, error)) {
        if (!entry.is_directory()) continue;
        const auto path = entry.path() / "SKILL.md";
        if (!std::filesystem::is_regular_file(path, error)) continue;
        auto skill = read_skill(path, true);
        if (!skill.text.empty()) result[lower(skill.text)] = std::move(skill);
    }
}

} // namespace

void ComposerIndex::set_workspace(std::filesystem::path root) {
    root_ = std::move(root);
}

void ComposerIndex::refresh() {
    skills_.clear();
    files_.clear();
    file_set_.clear();
    std::map<std::string, CompletionItem> skills;
    if (const auto* home = std::getenv("HOME")) {
        scan_skills(std::filesystem::path(home) / ".kimi-code" / "skills", skills);
        scan_skills(std::filesystem::path(home) / ".agents" / "skills", skills);
    }
    scan_skills(root_ / ".kimi-code" / "skills", skills);
    scan_skills(root_ / ".agents" / "skills", skills);
    for (auto& [name, skill] : skills) skills_.push_back(std::move(skill));
    std::sort(skills_.begin(), skills_.end(), [](const auto& a, const auto& b) {
        return lower(a.text) < lower(b.text);
    });

    std::error_code error;
    std::filesystem::recursive_directory_iterator iterator(root_, std::filesystem::directory_options::skip_permission_denied, error);
    const std::filesystem::recursive_directory_iterator end;
    while (iterator != end) {
        if (error) {
            error.clear();
            iterator.increment(error);
            continue;
        }
        const auto& entry = *iterator;
        if (entry.is_directory()) {
            const auto name = entry.path().filename();
            if (name == ".git" || name == ".stellar" || name == "build" || name == "node_modules") iterator.disable_recursion_pending();
        } else if (entry.is_regular_file() && !entry.is_symlink()) {
            auto relative = std::filesystem::relative(entry.path(), root_, error).generic_string();
            if (!error && file_set_.insert(relative).second) files_.push_back(std::move(relative));
        }
        iterator.increment(error);
        if (files_.size() >= 8000) break;
    }
    std::sort(files_.begin(), files_.end());
}

std::vector<CompletionItem> ComposerIndex::skills(const std::string& prefix, bool namespaced) const {
    std::vector<CompletionItem> result;
    const auto query = lower(prefix);
    for (const auto& skill : skills_) {
        if (lower(skill.text).rfind(query, 0) != 0) continue;
        auto text = namespaced || !query.empty() ? skill.text : "skill:" + skill.text;
        result.push_back({std::move(text), skill.detail});
        if (result.size() >= 400) break;
    }
    return result;
}

std::vector<CompletionItem> ComposerIndex::files(const std::string& directory, const std::string& prefix) const {
    std::vector<CompletionItem> result;
    const auto query = lower(prefix);
    for (const auto& file : files_) {
        if (file.rfind(directory, 0) != 0) continue;
        const auto remaining = file.substr(directory.size());
        if (lower(remaining).rfind(query, 0) != 0) continue;
        const auto parent = std::filesystem::path(file).parent_path().generic_string();
        result.push_back({remaining, parent.empty() ? "Workspace root" : parent});
        if (result.size() >= 400) break;
    }
    return result;
}

std::vector<CompletionItem> ComposerIndex::search_files(const std::string& query, size_t limit) const {
    std::vector<CompletionItem> result;
    const auto needle = lower(query);
    for (const auto& file : files_) {
        if (!needle.empty() && lower(file).find(needle) == std::string::npos) continue;
        const auto parent = std::filesystem::path(file).parent_path().generic_string();
        result.push_back({file, parent.empty() ? "Workspace root" : parent});
        if (result.size() >= limit) break;
    }
    return result;
}

std::vector<ContentMatch> ComposerIndex::search_content(const std::string& query, size_t limit) const {
    std::vector<ContentMatch> result;
    const auto needle = lower(trim(query));
    if (needle.empty() || limit == 0) return result;
    for (const auto& relative : files_) {
        const auto path = root_ / relative;
        std::error_code error;
        if (std::filesystem::file_size(path, error) > 2 * 1024 * 1024 || error) continue;
        std::ifstream input(path, std::ios::binary);
        if (!input) continue;
        const std::string content{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        if (content.find('\0') != std::string::npos) continue;
        int line_number = 1;
        size_t begin = 0;
        while (begin <= content.size()) {
            const auto end = content.find('\n', begin);
            auto line = content.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (lower(line).find(needle) != std::string::npos) {
                result.push_back({relative, line_number, shortened(trim(line), 160)});
                if (result.size() >= limit) return result;
            }
            if (end == std::string::npos) break;
            begin = end + 1;
            ++line_number;
        }
    }
    return result;
}

std::vector<std::filesystem::path> ComposerIndex::mentions(const std::string& text) const {
    std::vector<std::filesystem::path> result;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '@' || (i > 0 && !std::isspace(static_cast<unsigned char>(text[i - 1])))) continue;
        size_t begin = ++i;
        size_t end = begin;
        if (begin < text.size() && text[begin] == '"') {
            ++begin;
            end = text.find('"', begin);
            if (end == std::string::npos) continue;
        } else {
            end = text.find_first_of(" \t\r\n", begin);
            if (end == std::string::npos) end = text.size();
        }
        const auto relative = text.substr(begin, end - begin);
        if (has_file(relative)) result.push_back(root_ / std::filesystem::path(relative));
        i = end;
    }
    return result;
}

std::vector<ContextAttachment> ComposerIndex::mention_details(const std::string& text) const {
    std::vector<ContextAttachment> result;
    for (const auto& path : mentions(text)) {
        std::error_code error;
        const auto bytes = std::filesystem::file_size(path, error);
        if (error) continue;
        result.push_back({path, bytes, std::max<std::uintmax_t>(1, (bytes + 3) / 4)});
    }
    return result;
}

std::string ComposerIndex::strip_mention(const std::string& text, const std::string& relative) const {
    std::string result = text;
    for (size_t i = 0; i < result.size(); ++i) {
        if (result[i] != '@' || (i > 0 && !std::isspace(static_cast<unsigned char>(result[i - 1])))) continue;
        const size_t token = i;
        size_t begin = ++i;
        size_t end = begin;
        bool quoted = false;
        if (begin < result.size() && result[begin] == '"') {
            quoted = true;
            ++begin;
            end = result.find('"', begin);
            if (end == std::string::npos) continue;
        } else {
            end = result.find_first_of(" \t\r\n", begin);
            if (end == std::string::npos) end = result.size();
        }
        if (result.substr(begin, end - begin) != relative) continue;
        result.erase(token, end - token + (quoted ? 1 : 0));
        i = token;
    }
    return result;
}

std::string ComposerIndex::strip_mentions(const std::string& text) const {
    std::string result = text;
    for (size_t i = 0; i < result.size(); ++i) {
        if (result[i] != '@' || (i > 0 && !std::isspace(static_cast<unsigned char>(result[i - 1])))) continue;
        const size_t token = i;
        size_t begin = ++i;
        size_t end = begin;
        bool quoted = false;
        if (begin < result.size() && result[begin] == '"') {
            quoted = true;
            ++begin;
            end = result.find('"', begin);
            if (end == std::string::npos) continue;
        } else {
            end = result.find_first_of(" \t\r\n", begin);
            if (end == std::string::npos) end = result.size();
        }
        if (!has_file(result.substr(begin, end - begin))) continue;
        result.erase(token, end - token + (quoted ? 1 : 0));
        i = token;
    }
    return result;
}

bool ComposerIndex::has_skill(const std::string& name) const {
    const auto query = lower(name);
    return std::any_of(skills_.begin(), skills_.end(), [&](const auto& skill) {
        return lower(skill.text) == query;
    });
}

bool ComposerIndex::has_file(const std::string& relative) const {
    return file_set_.count(relative) > 0;
}

} // namespace raven
