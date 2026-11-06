#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace raven {

struct RecallPrompt {
    std::string id;
    std::string text;
    std::uint64_t uses = 1;
    std::int64_t last_used = 0;
    bool pinned = false;
};

class RecallStore {
public:
    explicit RecallStore(std::filesystem::path workspace_root);

    void load();
    void save() const;
    bool storage_exists() const;
    void remember(const std::string& text, std::int64_t timestamp = 0, bool persist = true);
    bool toggle_pin(const std::string& id);
    bool erase(const std::string& id);
    const RecallPrompt* find(const std::string& id) const;
    std::vector<const RecallPrompt*> recommend(const std::string& query = {}, std::size_t limit = 30) const;
    const std::vector<RecallPrompt>& entries() const;

private:
    std::filesystem::path path_;
    std::vector<RecallPrompt> prompts_;
};

} // namespace raven
