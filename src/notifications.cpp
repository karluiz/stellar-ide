#include "notifications.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <stdexcept>

namespace raven {
namespace {

using json = nlohmann::json;

std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot read " + path.string());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void write_text(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    const auto temporary = path.parent_path() / ("." + path.filename().string() + ".tmp");
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("Cannot write " + path.string());
        output << content;
        output.close();
        if (!output) throw std::runtime_error("Cannot write " + path.string());
    }
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(temporary);
        throw std::runtime_error("Cannot replace " + path.string() + ": " + error.message());
    }
}

} // namespace

NotificationStore::NotificationStore(std::filesystem::path workspace_root)
    : path_(std::move(workspace_root) / ".stellar" / "notifications.json") {
    load();
}

void NotificationStore::load() {
    entries_.clear();
    next_id_ = 1;
    if (!std::filesystem::exists(path_)) return;
    const auto data = json::parse(read_text(path_));
    if (!data.is_object() || !data.contains("notifications") || !data["notifications"].is_array()) {
        throw std::runtime_error("Invalid notification history");
    }
    for (const auto& item : data["notifications"]) {
        NotificationEntry entry;
        entry.id = item.value("id", "");
        entry.title = item.value("title", "");
        entry.body = item.value("body", "");
        entry.kind = item.value("kind", "run");
        entry.action = item.value("action", "");
        entry.target = item.value("target", "");
        entry.created_at = item.value("created_at", std::int64_t{0});
        entry.error = item.value("error", false);
        if (entry.id.empty() || entry.title.empty()) throw std::runtime_error("Invalid notification entry");
        entries_.push_back(std::move(entry));
    }
    if (entries_.size() > 50) entries_.erase(entries_.begin(), entries_.end() - 50);
    for (const auto& entry : entries_) {
        const auto separator = entry.id.rfind('-');
        if (separator == std::string::npos) continue;
        try {
            next_id_ = std::max(next_id_, static_cast<std::uint64_t>(std::stoull(entry.id.substr(separator + 1)) + 1));
        } catch (const std::exception&) {
        }
    }
}

NotificationEntry NotificationStore::add(NotificationEntry entry) {
    if (entry.title.empty()) throw std::runtime_error("Notification title is required");
    if (entry.created_at == 0) {
        entry.created_at = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }
    if (entry.id.empty()) entry.id = std::to_string(entry.created_at) + "-" + std::to_string(next_id_++);
    entries_.push_back(entry);
    if (entries_.size() > 50) entries_.erase(entries_.begin(), entries_.end() - 50);
    save();
    return entry;
}

void NotificationStore::update_body(const std::string& id, const std::string& body) {
    const auto found = std::find_if(entries_.begin(), entries_.end(), [&](const auto& entry) { return entry.id == id; });
    if (found == entries_.end()) return;
    found->body = body;
    save();
}

void NotificationStore::clear() {
    entries_.clear();
    save();
}

const std::vector<NotificationEntry>& NotificationStore::entries() const { return entries_; }

std::optional<NotificationEntry> NotificationStore::find(const std::string& id) const {
    const auto found = std::find_if(entries_.begin(), entries_.end(), [&](const auto& entry) { return entry.id == id; });
    return found == entries_.end() ? std::nullopt : std::optional<NotificationEntry>(*found);
}

void NotificationStore::save() const {
    json notifications = json::array();
    for (const auto& entry : entries_) {
        notifications.push_back({
            {"id", entry.id}, {"title", entry.title}, {"body", entry.body}, {"kind", entry.kind},
            {"action", entry.action}, {"target", entry.target}, {"created_at", entry.created_at}, {"error", entry.error}
        });
    }
    write_text(path_, json{{"version", 1}, {"notifications", notifications}}.dump(2) + "\n");
}

} // namespace raven
