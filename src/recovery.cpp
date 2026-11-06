#include "recovery.hpp"

#include "workspace.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace raven {

RecoveryStore::RecoveryStore(std::filesystem::path workspace)
    : path_(std::move(workspace) / ".stellar" / "recovery.json") {
    load();
}

void RecoveryStore::load() {
    draft_.clear();
    editors_.clear();
    std::error_code error;
    if (!std::filesystem::is_regular_file(path_, error)) return;
    const auto data = nlohmann::json::parse(read_file(path_));
    draft_ = data.value("draft", "");
    for (const auto& item : data.at("editors")) {
        const auto kind = item.at("kind").get<std::string>();
        if (kind != "code" && kind != "agent" && kind != "note") continue;
        editors_.push_back({kind, item.at("path").get<std::string>(), item.at("content").get<std::string>(),
            item.at("saved_content").get<std::string>()});
    }
}

void RecoveryStore::save() const {
    nlohmann::json editors = nlohmann::json::array();
    for (const auto& editor : editors_) {
        editors.push_back({{"kind", editor.kind}, {"path", editor.path.string()}, {"content", editor.content},
            {"saved_content", editor.saved_content}});
    }
    const nlohmann::json data = {{"version", 1}, {"draft", draft_}, {"editors", std::move(editors)}};
    write_file(path_, data.dump(2) + "\n");
}

void RecoveryStore::set_draft(const std::string& text) {
    draft_ = text;
    save();
}

void RecoveryStore::clear_draft() {
    if (draft_.empty()) return;
    draft_.clear();
    save();
}

const std::string& RecoveryStore::draft() const { return draft_; }

void RecoveryStore::set_editor(const std::string& kind, const std::filesystem::path& path,
                               const std::string& content, const std::string& saved_content) {
    const auto found = std::find_if(editors_.begin(), editors_.end(), [&](const auto& editor) {
        return editor.kind == kind && editor.path == path;
    });
    if (found == editors_.end()) editors_.push_back({kind, path, content, saved_content});
    else {
        found->content = content;
        found->saved_content = saved_content;
    }
    save();
}

void RecoveryStore::clear_editor(const std::string& kind, const std::filesystem::path& path) {
    const auto previous = editors_.size();
    editors_.erase(std::remove_if(editors_.begin(), editors_.end(), [&](const auto& editor) {
        return editor.kind == kind && editor.path == path;
    }), editors_.end());
    if (editors_.size() != previous) save();
}

const std::vector<EditorRecovery>& RecoveryStore::editors() const { return editors_; }

} // namespace raven
