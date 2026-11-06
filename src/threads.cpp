#include "threads.hpp"

#include "workspace.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace raven {

ThreadStore::ThreadStore(std::filesystem::path workspace)
    : path_(std::move(workspace) / ".stellar" / "threads.json") {}

void ThreadStore::load() {
    threads_.clear();
    std::error_code error;
    if (!std::filesystem::is_regular_file(path_, error)) return;
    const auto data = nlohmann::json::parse(read_file(path_));
    for (const auto& item : data.at("threads")) {
        ConversationThread thread;
        thread.id = item.at("id").get<std::string>();
        thread.title = item.at("title").get<std::string>();
        thread.session_id = item.value("session_id", "");
        thread.transcript = item.value("transcript", "");
        thread.harness = item.value("harness", "kimi") == "kimi" ? Harness::KimiCode : Harness::ClaudeCode;
        thread.updated_at = item.value("updated_at", std::int64_t{0});
        thread.archived = item.value("archived", false);
        for (const auto& message : item.at("messages")) {
            thread.messages.push_back({message.at("role"), message.at("text"), message.value("user", false)});
        }
        threads_.push_back(std::move(thread));
    }
    std::sort(threads_.begin(), threads_.end(), [](const auto& a, const auto& b) {
        return a.updated_at > b.updated_at;
    });
}

void ThreadStore::save() const {
    nlohmann::json data;
    data["version"] = 1;
    data["threads"] = nlohmann::json::array();
    for (const auto& thread : threads_) {
        nlohmann::json item = {
            {"id", thread.id}, {"title", thread.title}, {"session_id", thread.session_id},
            {"transcript", thread.transcript}, {"harness", thread.harness == Harness::KimiCode ? "kimi" : "claude"},
            {"updated_at", thread.updated_at}, {"archived", thread.archived}, {"messages", nlohmann::json::array()}
        };
        for (const auto& message : thread.messages) {
            item["messages"].push_back({{"role", message.role}, {"text", message.text}, {"user", message.user}});
        }
        data["threads"].push_back(std::move(item));
    }
    write_file(path_, data.dump(2) + "\n");
}

void ThreadStore::upsert(ConversationThread thread) {
    const auto found = std::find_if(threads_.begin(), threads_.end(), [&](const auto& current) {
        return current.id == thread.id;
    });
    if (found == threads_.end()) threads_.push_back(std::move(thread));
    else *found = std::move(thread);
    std::sort(threads_.begin(), threads_.end(), [](const auto& a, const auto& b) {
        return a.updated_at > b.updated_at;
    });
}

ConversationThread* ThreadStore::find(const std::string& id) {
    const auto found = std::find_if(threads_.begin(), threads_.end(), [&](const auto& thread) {
        return thread.id == id;
    });
    return found == threads_.end() ? nullptr : &*found;
}

const ConversationThread* ThreadStore::find(const std::string& id) const {
    const auto found = std::find_if(threads_.begin(), threads_.end(), [&](const auto& thread) {
        return thread.id == id;
    });
    return found == threads_.end() ? nullptr : &*found;
}

const ConversationThread* ThreadStore::latest() const {
    const auto found = std::find_if(threads_.begin(), threads_.end(), [](const auto& thread) {
        return !thread.archived;
    });
    return found == threads_.end() ? nullptr : &*found;
}

const std::vector<ConversationThread>& ThreadStore::threads() const {
    return threads_;
}

} // namespace raven
