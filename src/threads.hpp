#pragma once

#include "claude.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace raven {

struct ThreadMessage {
    std::string role;
    std::string text;
    bool user = false;
};

struct ConversationThread {
    std::string id;
    std::string title;
    std::string session_id;
    std::string transcript;
    Harness harness = Harness::KimiCode;
    std::int64_t updated_at = 0;
    bool archived = false;
    std::vector<ThreadMessage> messages;
};

class ThreadStore {
public:
    explicit ThreadStore(std::filesystem::path workspace);

    void load();
    void save() const;
    void upsert(ConversationThread thread);
    ConversationThread* find(const std::string& id);
    const ConversationThread* find(const std::string& id) const;
    const ConversationThread* latest() const;
    const std::vector<ConversationThread>& threads() const;

private:
    std::filesystem::path path_;
    std::vector<ConversationThread> threads_;
};

} // namespace raven
