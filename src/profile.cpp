#include "profile.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
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

WorkspaceProfileStore::WorkspaceProfileStore(std::filesystem::path workspace_root)
    : path_(std::move(workspace_root) / ".stellar" / "profile.json") {
    load();
}

void WorkspaceProfileStore::load() {
    data = WorkspaceProfile{};
    if (!std::filesystem::exists(path_)) return;
    const auto root = json::parse(read_text(path_));
    if (!root.is_object()) throw std::runtime_error("Invalid workspace profile");
    const int version = root.value("version", 1);
    data.interface_zoom = std::clamp(root.value("interface_zoom", data.interface_zoom), 0.5f, 2.0f);
    data.editor_zoom = std::clamp(root.value("editor_zoom", data.editor_zoom), 0.5f, 2.5f);
    data.theme = root.value("theme", data.theme);
    const int themes[] = {3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21};
    if (std::find(std::begin(themes), std::end(themes), data.theme) == std::end(themes)) data.theme = 12;
    data.harness = std::clamp(root.value("harness", data.harness), 0, 1);
    data.run_mode = std::clamp(root.value("run_mode", data.run_mode), 0, 2);
    data.claude_model = root.value("claude_model", data.claude_model);
    data.claude_effort = root.value("claude_effort", data.claude_effort);
    data.kimi_model = root.value("kimi_model", data.kimi_model);
    data.kimi_effort = root.value("kimi_effort", data.kimi_effort);
    data.left_sidebar = root.value("left_sidebar", data.left_sidebar);
    if (version >= 2) data.right_sidebar = root.value("right_sidebar", data.right_sidebar);
    data.vector_recall = root.value("vector_recall", data.vector_recall);
    data.watch_checks = root.value("watch_checks", data.watch_checks);
    data.opening_sound = root.value("opening_sound", data.opening_sound);
    data.opening_seen = root.value("opening_seen", data.opening_seen);
    if (root.contains("shortcuts")) {
        if (!root["shortcuts"].is_object()) throw std::runtime_error("Invalid custom shortcuts");
        for (const auto& [action, combination] : root["shortcuts"].items()) {
            if (combination.is_string() && combination.get<std::string>().size() <= 64) {
                data.shortcuts[action] = combination.get<std::string>();
            }
        }
    }
}

void WorkspaceProfileStore::save() const {
    json shortcuts = json::object();
    for (const auto& [action, combination] : data.shortcuts) shortcuts[action] = combination;
    const json root = {
        {"version", 2}, {"interface_zoom", data.interface_zoom}, {"editor_zoom", data.editor_zoom},
        {"theme", data.theme}, {"harness", data.harness}, {"run_mode", data.run_mode},
        {"claude_model", data.claude_model}, {"claude_effort", data.claude_effort},
        {"kimi_model", data.kimi_model}, {"kimi_effort", data.kimi_effort},
        {"left_sidebar", data.left_sidebar}, {"right_sidebar", data.right_sidebar},
        {"vector_recall", data.vector_recall}, {"watch_checks", data.watch_checks},
        {"opening_sound", data.opening_sound}, {"opening_seen", data.opening_seen}, {"shortcuts", shortcuts}
    };
    write_text(path_, root.dump(2) + "\n");
}

} // namespace raven
