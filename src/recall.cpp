#include "recall.hpp"

#include "workspace.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <iomanip>
#include <sstream>

namespace raven {
namespace {

std::string trimmed(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string prompt_id(const std::string& text) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char c : text) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    std::ostringstream id;
    id << "prompt-" << std::hex << std::setw(16) << std::setfill('0') << hash;
    return id.str();
}

std::int64_t now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

} // namespace

RecallStore::RecallStore(std::filesystem::path workspace_root)
    : path_(std::move(workspace_root) / ".stellar" / "recall.json") {
    load();
}

void RecallStore::load() {
    prompts_.clear();
    std::error_code error;
    if (!std::filesystem::is_regular_file(path_, error)) return;
    const auto root = nlohmann::json::parse(read_file(path_));
    if (!root.is_object() || !root.contains("prompts") || !root["prompts"].is_array()) {
        throw std::runtime_error("Invalid Recall library");
    }
    for (const auto& item : root["prompts"]) {
        const auto text = trimmed(item.value("text", ""));
        if (text.empty() || text.size() > 20'000) continue;
        RecallPrompt prompt;
        prompt.id = prompt_id(text);
        prompt.text = text;
        prompt.uses = std::max<std::uint64_t>(1, item.value("uses", std::uint64_t{1}));
        prompt.last_used = item.value("last_used", std::int64_t{0});
        prompt.pinned = item.value("pinned", false);
        const auto existing = std::find_if(prompts_.begin(), prompts_.end(), [&](const auto& current) {
            return current.id == prompt.id;
        });
        if (existing == prompts_.end()) prompts_.push_back(std::move(prompt));
        else {
            existing->uses += prompt.uses;
            existing->last_used = std::max(existing->last_used, prompt.last_used);
            existing->pinned = existing->pinned || prompt.pinned;
        }
    }
}

void RecallStore::save() const {
    nlohmann::json prompts = nlohmann::json::array();
    for (const auto& prompt : prompts_) {
        prompts.push_back({
            {"id", prompt.id}, {"text", prompt.text}, {"uses", prompt.uses},
            {"last_used", prompt.last_used}, {"pinned", prompt.pinned}
        });
    }
    write_file(path_, nlohmann::json{{"version", 1}, {"prompts", prompts}}.dump(2) + "\n");
}

bool RecallStore::storage_exists() const {
    std::error_code error;
    return std::filesystem::is_regular_file(path_, error);
}

void RecallStore::remember(const std::string& text, std::int64_t timestamp, bool persist) {
    const auto clean = trimmed(text);
    if (clean.empty() || clean.size() > 20'000) return;
    if (timestamp == 0) timestamp = now();
    const auto id = prompt_id(clean);
    const auto existing = std::find_if(prompts_.begin(), prompts_.end(), [&](const auto& prompt) {
        return prompt.id == id;
    });
    if (existing == prompts_.end()) prompts_.push_back({id, clean, 1, timestamp, false});
    else {
        ++existing->uses;
        existing->last_used = std::max(existing->last_used, timestamp);
    }
    if (prompts_.size() > 200) {
        const auto oldest = std::min_element(prompts_.begin(), prompts_.end(), [](const auto& a, const auto& b) {
            if (a.pinned != b.pinned) return a.pinned < b.pinned;
            return a.last_used < b.last_used;
        });
        if (!oldest->pinned) prompts_.erase(oldest);
    }
    if (persist) save();
}

bool RecallStore::toggle_pin(const std::string& id) {
    const auto existing = std::find_if(prompts_.begin(), prompts_.end(), [&](const auto& prompt) {
        return prompt.id == id;
    });
    if (existing == prompts_.end()) return false;
    existing->pinned = !existing->pinned;
    save();
    return existing->pinned;
}

bool RecallStore::erase(const std::string& id) {
    const auto existing = std::find_if(prompts_.begin(), prompts_.end(), [&](const auto& prompt) {
        return prompt.id == id;
    });
    if (existing == prompts_.end()) return false;
    prompts_.erase(existing);
    save();
    return true;
}

const RecallPrompt* RecallStore::find(const std::string& id) const {
    const auto existing = std::find_if(prompts_.begin(), prompts_.end(), [&](const auto& prompt) {
        return prompt.id == id;
    });
    return existing == prompts_.end() ? nullptr : &*existing;
}

std::vector<const RecallPrompt*> RecallStore::recommend(const std::string& query, std::size_t limit) const {
    const auto needle = lowercase(trimmed(query));
    std::vector<const RecallPrompt*> result;
    for (const auto& prompt : prompts_) {
        if (!needle.empty() && lowercase(prompt.text).find(needle) == std::string::npos) continue;
        result.push_back(&prompt);
    }
    std::sort(result.begin(), result.end(), [](const auto* a, const auto* b) {
        if (a->pinned != b->pinned) return a->pinned > b->pinned;
        if (a->uses != b->uses) return a->uses > b->uses;
        if (a->last_used != b->last_used) return a->last_used > b->last_used;
        return a->text < b->text;
    });
    if (result.size() > limit) result.erase(result.begin() + limit, result.end());
    return result;
}

const std::vector<RecallPrompt>& RecallStore::entries() const { return prompts_; }

} // namespace raven
