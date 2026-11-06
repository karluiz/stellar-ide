#pragma once

#include <filesystem>
#include <map>
#include <string>

namespace raven {

struct WorkspaceProfile {
    float interface_zoom = 1.15f;
    float editor_zoom = 1.0f;
    int theme = 12;
    int harness = 0;
    int run_mode = 0;
    std::string claude_model;
    std::string claude_effort;
    std::string kimi_model;
    std::string kimi_effort;
    bool left_sidebar = true;
    bool right_sidebar = true;
    bool vector_recall = true;
    bool watch_checks = false;
    bool do_not_disturb = false;
    bool opening_sound = true;
    std::map<std::string, bool> notification_mutes;
    std::map<std::string, std::string> shortcuts;
};

class WorkspaceProfileStore {
public:
    WorkspaceProfileStore() = default;
    explicit WorkspaceProfileStore(std::filesystem::path workspace_root);

    void load();
    void save() const;
    WorkspaceProfile data;

private:
    std::filesystem::path path_;
};

} // namespace raven
