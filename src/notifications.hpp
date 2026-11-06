#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace raven {

struct NotificationEntry {
    std::string id;
    std::string title;
    std::string body;
    std::string kind;
    std::string action;
    std::string target;
    std::int64_t created_at = 0;
    bool error = false;
};

class NotificationStore {
public:
    NotificationStore() = default;
    explicit NotificationStore(std::filesystem::path workspace_root);

    void load();
    NotificationEntry add(NotificationEntry entry);
    void update_body(const std::string& id, const std::string& body);
    void clear();
    const std::vector<NotificationEntry>& entries() const;
    std::optional<NotificationEntry> find(const std::string& id) const;

private:
    std::filesystem::path path_;
    std::vector<NotificationEntry> entries_;
    std::uint64_t next_id_ = 1;

    void save() const;
};

} // namespace raven
