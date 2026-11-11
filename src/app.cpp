#include "app.hpp"
#include "app_icon.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace raven {
namespace {

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

// Shortens to at most `length` characters, counting UTF-8 code points so a multi-byte character is
// never split (a split sequence is invalid UTF-8 and cannot be saved as JSON), and preferring a word
// boundary so titles do not end mid-word.
std::string shortened(const std::string& value, size_t length) {
    const auto continuation = [](char byte) { return (static_cast<unsigned char>(byte) & 0xC0) == 0x80; };
    size_t characters = 0;
    for (const char byte : value) characters += continuation(byte) ? 0 : 1;
    if (characters <= length || length == 0) return value;
    size_t cut = 0;
    for (size_t seen = 0; cut < value.size(); ++cut) {
        if (continuation(value[cut])) continue;
        if (seen++ == length - 1) break;
    }
    const auto space = value.rfind(' ', cut);
    if (space != std::string::npos && space >= cut * 2 / 3) cut = space;
    while (cut > 0 && (value[cut - 1] == ' ' || value[cut - 1] == ',' || value[cut - 1] == ';' || value[cut - 1] == ':')) --cut;
    return value.substr(0, cut) + "…";
}

// "1 file", "3 files": status and review copy never shows a parenthesised plural.
// A readable conversation title from a prompt: first sentence of the first line, collapsed
// whitespace, at most six words, capitalised, without trailing punctuation.
std::string conversation_title(const std::string& prompt) {
    auto line = prompt.substr(0, prompt.find('\n'));
    std::string collapsed;
    for (const char c : line) {
        const bool space = c == ' ' || c == '\t' || c == '\r';
        if (space && (collapsed.empty() || collapsed.back() == ' ')) continue;
        collapsed.push_back(space ? ' ' : c);
    }
    for (const char* stop : {". ", "? ", "! ", ": "}) {
        const auto end = collapsed.find(stop);
        if (end != std::string::npos && end >= 12) collapsed.resize(end);
    }
    std::string title;
    int words = 0;
    std::istringstream stream(collapsed);
    for (std::string word; stream >> word && words < 6; ++words) title += (title.empty() ? "" : " ") + word;
    while (!title.empty() && std::string(".,;:!?-").find(title.back()) != std::string::npos) title.pop_back();
    if (!title.empty() && title[0] >= 'a' && title[0] <= 'z') title[0] = static_cast<char>(title[0] - 'a' + 'A');
    return title.empty() ? "New conversation" : title;
}

// Compact age for list rows: "now", "5m", "3h", "2d".
std::string relative_age(std::int64_t timestamp) {
    if (timestamp <= 0) return "";
    const auto seconds = std::max<std::int64_t>(0, static_cast<std::int64_t>(std::time(nullptr)) - timestamp);
    return seconds < 60 ? "now" : seconds < 3600 ? std::to_string(seconds / 60) + "m"
        : seconds < 86400 ? std::to_string(seconds / 3600) + "h" : std::to_string(seconds / 86400) + "d";
}

// Agents Stellar ships and relies on; they can be edited but not deleted.
bool builtin_agent(const std::filesystem::path& path) {
    const auto filename = path.filename();
    return filename == "orchestrator.md" || filename == "code-reviewer.md" || filename == "wiki-curator.md";
}

std::string counted(size_t count, const char* noun) {
    return std::to_string(count) + " " + noun + (count == 1 ? "" : "s");
}

int theme_family(int index) {
    if (index >= 12 && index <= 14) return 0;
    if (index == 3 || index == 8 || index == 15) return 1;
    if (index == 4 || index == 9 || index == 16) return 2;
    if (index == 5 || index == 10 || index == 17) return 3;
    return 4;
}

int theme_finish(int index) {
    if (index >= 14 && index <= 18) return 2;
    return index == 4 || index == 5 || index == 8 || index == 11 || index == 13 ? 1 : 0;
}

int theme_index(int family, int finish) {
    static const int indices[5][3] = {
        {12, 13, 14}, {3, 8, 15}, {9, 4, 16}, {10, 5, 17}, {6, 11, 18}
    };
    return indices[std::clamp(family, 0, 4)][std::clamp(finish, 0, 2)];
}

std::string join(const std::vector<std::string>& values, const std::string& separator) {
    std::string result;
    for (const auto& value : values) {
        if (!result.empty()) result += separator;
        result += value;
    }
    return result;
}

struct ShortcutDefinition {
    const char* action;
    const char* combination;
};

const std::vector<ShortcutDefinition>& shortcut_definitions() {
    static const std::vector<ShortcutDefinition> definitions = {
        {"run.send", "Ctrl+Enter"}, {"file.save", "Ctrl+S"}, {"thread.new", "Ctrl+N"},
        {"agent.new", "Ctrl+Alt+A"}, {"wiki.new", "Ctrl+Shift+N"}, {"proposal.open", "Ctrl+Shift+R"},
        {"checkpoint.review", "Ctrl+Shift+D"}, {"proposal.accept", "Ctrl+Shift+Enter"},
        {"wiki.preview", "Ctrl+P"}, {"palette.commands", "Ctrl+Shift+P"}, {"palette.files", "Ctrl+Shift+O"},
        {"palette.search", "Ctrl+Shift+F"}, {"composer.focus", "Ctrl+L"}, {"recall.open", "Ctrl+R"}, {"run.setup.toggle", "Ctrl+Shift+E"}, {"knowledge.open_tab", "Ctrl+K"},
        {"memory.open", "Ctrl+M"}, {"theme.browser", "Ctrl+Shift+T"}, {"sidebar.left", "Ctrl+B"},
        {"sidebar.right", "Ctrl+Shift+B"}, {"settings.toggle", "Ctrl+,"}
    };
    return definitions;
}

std::string key_name(RKey key) {
    if ((key >= R_KEY_A && key <= R_KEY_Z) || (key >= R_KEY_0 && key <= R_KEY_9)) {
        return std::string(1, static_cast<char>(key));
    }
    if (key >= R_KEY_F1 && key <= R_KEY_F12) return "F" + std::to_string(key - R_KEY_F1 + 1);
    switch (key) {
        case R_KEY_ENTER: return "Enter";
        case R_KEY_KP_ENTER: return "Enter";
        case R_KEY_COMMA: return ",";
        case R_KEY_MINUS: return "Minus";
        case R_KEY_EQUAL: return "Plus";
        case R_KEY_SPACE: return "Space";
        case R_KEY_TAB: return "Tab";
        case R_KEY_PERIOD: return ".";
        case R_KEY_SLASH: return "/";
        default: return {};
    }
}

bool parse_shortcut(const std::string& combination, RKey& key, Ruint& modifiers) {
    key = R_KEY_UNKNOWN;
    modifiers = 0;
    std::stringstream input(combination);
    std::string part;
    while (std::getline(input, part, '+')) {
        const auto name = lowercase(part);
        if (name == "ctrl" || name == "control") modifiers |= R_MOD_CTRL;
        else if (name == "shift") modifiers |= R_MOD_SHIFT;
        else if (name == "alt") modifiers |= R_MOD_ALT;
        else if (name == "super") modifiers |= R_MOD_SUPER;
        else if (part.size() == 1 && std::isalnum(static_cast<unsigned char>(part[0]))) {
            key = static_cast<RKey>(std::toupper(static_cast<unsigned char>(part[0])));
        } else if (name == "enter") key = R_KEY_ENTER;
        else if (name == ",") key = R_KEY_COMMA;
        else if (name == "minus") key = R_KEY_MINUS;
        else if (name == "plus" || name == "equal") key = R_KEY_EQUAL;
        else if (name == "space") key = R_KEY_SPACE;
        else if (name == "tab") key = R_KEY_TAB;
        else if (name == ".") key = R_KEY_PERIOD;
        else if (name == "/") key = R_KEY_SLASH;
        else if (name.size() >= 2 && name[0] == 'f') {
            const int function = std::atoi(name.c_str() + 1);
            if (function < 1 || function > 12) return false;
            key = static_cast<RKey>(R_KEY_F1 + function - 1);
        } else return false;
    }
    return key != R_KEY_UNKNOWN;
}

std::string event_shortcut(const REvent* event) {
    std::string result;
    const auto modifier = [&](Ruint flag, const char* name) {
        if (!(event->modifiers & flag)) return;
        if (!result.empty()) result += "+";
        result += name;
    };
    modifier(R_MOD_CTRL, "Ctrl");
    modifier(R_MOD_SHIFT, "Shift");
    modifier(R_MOD_ALT, "Alt");
    modifier(R_MOD_SUPER, "Super");
    const auto key = key_name(static_cast<RKey>(event->key));
    if (key.empty()) return {};
    if (!result.empty()) result += "+";
    return result + key;
}

std::string agent_body(const std::string& content) {
    if (content.rfind("---", 0) != 0) return content;
    const auto end = content.find("\n---", 3);
    return end == std::string::npos ? content : content.substr(end + 4);
}

std::string shell_quote(std::string value) {
    std::string result = "'";
    for (const char c : value) result += c == '\'' ? "'\\''" : std::string(1, c);
    return result + "'";
}

std::string tool_action(const ClaudeEvent& event) {
    if (event.kind == EventKind::SubagentStarted) return "Delegate to " + event.name;
    const auto name = lowercase(event.name);
    if (name == "read") return "Read";
    if (name == "edit" || name == "multiedit") return "Edit";
    if (name == "write") return "Write";
    if (name == "bash") return "Run";
    if (name == "grep") return "Search code";
    if (name == "glob") return "Find files";
    if (name == "websearch") return "Search the web";
    if (name == "fetchurl" || name == "webfetch") return "Fetch";
    if (name == "todolist") return "Update plan";
    return event.name.empty() ? "Use tool" : event.name;
}

std::filesystem::path tool_icon(const ClaudeEvent& event) {
    const auto name = lowercase(event.name);
    std::string icon = "tool";
    if (event.kind == EventKind::SubagentStarted) icon = "delegate";
    else if (name == "read") icon = "read";
    else if (name == "write") icon = "write";
    else if (name == "edit" || name == "multiedit") icon = "edit";
    else if (name == "bash") icon = "run";
    else if (name == "grep" || name == "glob" || name == "websearch") icon = "search";
    const auto source = std::filesystem::path(RAVEN_IDE_UI_DIR) / "icons" / (icon + ".png");
    return std::filesystem::exists(source) ? source
        : std::filesystem::path(RAVEN_IDE_INSTALLED_UI_DIR) / "icons" / (icon + ".png");
}

std::string url_encode(const std::string& value) {
    std::string encoded;
    const char* hex = "0123456789ABCDEF";
    for (const unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') encoded.push_back(static_cast<char>(c));
        else {
            encoded.push_back('%');
            encoded.push_back(hex[c >> 4]);
            encoded.push_back(hex[c & 15]);
        }
    }
    return encoded;
}

std::string url_decode(const std::string& value) {
    std::string decoded;
    const auto hex = [](char c) {
        return c <= '9' ? c - '0' : std::tolower(static_cast<unsigned char>(c)) - 'a' + 10;
    };
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size() && std::isxdigit(static_cast<unsigned char>(value[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(value[i + 2]))) {
            decoded.push_back(static_cast<char>((hex(value[i + 1]) << 4) | hex(value[i + 2])));
            i += 2;
        } else decoded.push_back(value[i]);
    }
    return decoded;
}

struct EditorReference {
    std::filesystem::path path;
    int line = 0;
};

std::optional<EditorReference> editor_reference(const std::filesystem::path& workspace, std::string detail) {
    const auto first = detail.find_first_not_of(" \t\r\n");
    const auto last = detail.find_last_not_of(" \t\r\n");
    if (first == std::string::npos) return std::nullopt;
    detail = detail.substr(first, last - first + 1);
    if (detail.size() > 4096) return std::nullopt;
    int line = 0;
    const auto colon = detail.rfind(':');
    if (colon != std::string::npos && colon + 1 < detail.size() &&
        std::all_of(detail.begin() + colon + 1, detail.end(), [](unsigned char c) { return std::isdigit(c); })) {
        try {
            line = std::clamp(std::stoi(detail.substr(colon + 1)), 1, 1'000'000);
            detail.erase(colon);
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }
    if (detail.empty()) return std::nullopt;
    std::error_code error;
    const auto root = std::filesystem::weakly_canonical(workspace, error);
    if (error) return std::nullopt;
    const auto input = std::filesystem::path(detail);
    const auto candidate = std::filesystem::weakly_canonical(input.is_absolute() ? input : root / input, error);
    if (error || !std::filesystem::is_regular_file(candidate) ||
        (candidate != root && std::mismatch(root.begin(), root.end(), candidate.begin()).first != root.end())) {
        return std::nullopt;
    }
    return EditorReference{candidate, line};
}

std::string activity_detail(const std::filesystem::path& workspace, const std::string& detail) {
    if (!editor_reference(workspace, detail)) return detail;
    std::string label;
    for (const char c : detail) {
        if (c == '\\' || c == '[' || c == ']') label.push_back('\\');
        label.push_back(c);
    }
    return "[" + label + "](file:" + url_encode(detail) + ")";
}

const std::vector<CompletionItem>& system_commands() {
    static const std::vector<CompletionItem> commands = {
        {"new", "Start a fresh session"}, {"compact", "Compact session context"},
        {"plan", "Toggle plan mode"}, {"init", "Generate AGENTS.md"},
        {"model", "Choose a model"}, {"permission", "Choose permission mode"},
        {"usage", "Show session usage"}, {"status", "Show session status"},
        {"help", "Show Kimi Code help"}
    };
    return commands;
}

bool is_system_command(const std::string& name) {
    const auto& commands = system_commands();
    return std::any_of(commands.begin(), commands.end(), [&](const auto& command) {
        return command.text == name;
    });
}

struct PaletteCommand {
    const char* name;
    const char* detail;
    const char* action;
};

const std::vector<PaletteCommand>& palette_commands() {
    static const std::vector<PaletteCommand> commands = {
        {"New chat", "Start an empty conversation", "thread.new"},
        {"Archive chat", "Move the selected conversation to Archived", "thread.archive"},
        {"Show archived chats", "Switch active and archived conversation views", "thread.view"},
        {"Restore archived chat", "Return the selected archived conversation to active chats", "thread.archive"},
        {"Open workspace", "Choose another workspace root", "workspace.open"},
        {"Build mode", "Implement, verify, and review changes", "mode.build"},
        {"Ask mode", "Read-only answers without side effects", "mode.ask"},
        {"Review mode", "Read-only findings without side effects", "mode.review"},
        {"Create agent", "Add a shared agent definition", "agent.new"},
        {"Create wiki note", "Add a workspace Markdown note", "wiki.new"},
        {"Save active editor", "Write the current file, agent, or note", "file.save"},
        {"Compact conversation", "Keep task and recent messages for the next session", "context.compact"},
        {"Recall prompts", "Reuse and pin saved prompts", "recall.open"},
        {"Review code changes", "Open checkpoints and verification", "checkpoint.review"},
        {"Worktrees", "Create, review, and apply isolated Git tasks", "mission.open"},
        {"Memory Universe", "Inspect reviewed episodic memory", "memory.open"},
        {"Tesseract UI", "Create and refine interfaces with the agent", "tesseract.open"},
        {"Workflows · Endurance", "Orchestrate recurring multi-agent workflows", "endurance.open"},
        {"Knowledge", "Inspect notes, entities, relations, and memory", "knowledge.open_tab"},
        {"Activity", "Inspect live agent work and run details", "activity.open"},
        {"Alerts", "Open notification history", "alerts.view"},
        {"Choose your world", "Open the planet theme gallery", "theme.browser"},
        {"Settings", "Appearance, harness, zoom, and keyboard", "settings.toggle"},
        {"Show or fold navigation", "Toggle the left sidebar", "sidebar.left"},
        {"Show or fold context panel", "Toggle the right sidebar", "sidebar.right"},
        {"Stop current run", "Cancel active work and queued follow-up", "run.stop"},
        {"Retry last run", "Repeat the previous prompt", "run.retry"}
    };
    return commands;
}

} // namespace

App::App(std::filesystem::path workspace, bool demo)
    : workspace_(std::move(workspace)), thread_store_(workspace_.root()), recall_store_(workspace_.root()), checkpoint_store_(workspace_.root()), mission_store_(workspace_.root()),
      endurance_(workspace_, checkpoint_store_, mission_store_,
                 std::filesystem::exists(RAVEN_IDE_INSTALLED_KNOWLEDGE_BIN) ? RAVEN_IDE_INSTALLED_KNOWLEDGE_BIN : RAVEN_IDE_KNOWLEDGE_BIN, {}),
      endurance_store_(workspace_.root()), recovery_store_(workspace_.root()), demo_(demo) {}

App::~App() {
    try {
        if (!active_checkpoint_id_.empty()) checkpoint_store_.finish(active_checkpoint_id_, "interrupted", active_checks_);
        flush_autosave();
        save_thread();
        save_profile();
    } catch (const std::exception&) {
    }
    Rset_frame_callback(nullptr, nullptr);
    lead_process_.stop();
    wiki_process_.stop();
    if (Rtexture_is_valid(composer_texture_)) Rtexture_destroy(composer_texture_);
    if (Rtexture_is_valid(tesseract_texture_)) Rtexture_destroy(tesseract_texture_);
}

Rid App::id(const char* name) const { return Rwidget_find(window_, name); }

Rid App::window() const { return window_; }

bool App::create() {
    load_profile();
    static const char* actions[] = {
        "run.send", "run.stop", "run.retry", "run.setup.toggle", "run.setup.close", "queue.cancel", "workspace.open", "thread.new", "thread.open", "thread.view", "thread.archive", "agent.new", "agent.open", "agent.save", "agent.delete",
        "wiki.new", "wiki.open", "wiki.save", "proposal.open", "proposal.accept",
        "proposal.discard", "checkpoint.review", "checkpoint.history_changed", "checkpoint.file_changed", "checkpoint.open", "checkpoint.restore", "checkpoint.restore_hunk",
        "verification.open", "verification.open_diagnostic", "verification.rerun", "verification.watch",
        "graph.fit", "graph.toggle", "file.save", "wiki.view", "wiki.preview", "harness.changed", "run.mode.changed", "model.changed", "effort.changed", "context.clear", "context.remove", "context.compact", "composer.focus", "composer.image.remove", "tesseract.image.remove",
        "palette.commands", "palette.files", "palette.search", "palette.changed", "palette.open", "palette.close",
        "recall.open", "recall.close", "recall.changed", "recall.use", "recall.pin", "recall.erase",
        "knowledge.search", "knowledge.open", "knowledge.open_tab", "navigation.search", "code.save", "sidebar.left", "sidebar.right",
        "nav.chat", "nav.canvas", "nav.workflows", "nav.files", "nav.agents", "nav.review", "nav.knowledge", "nav.worktrees", "nav.settings", "activity.open", "alerts.view", "context.close",
        "mission.create", "mission.review", "mission.apply", "mission.apply_hunk", "mission.discard", "mission.return", "mission.open",
        "settings.toggle", "settings.close", "settings.harness.changed", "settings.vector", "appearance.toggle",
        "settings.interface.decrease", "settings.interface.reset", "settings.interface.increase",
        "settings.editor.decrease", "settings.editor.reset", "settings.editor.increase",
        "alerts.open", "alerts.clear",
        "shortcut.changed", "shortcut.rebind", "shortcut.reset",
        "theme.earth", "theme.earth.light", "theme.earth.glass", "theme.miller", "theme.miller.light", "theme.miller.glass", "theme.mann", "theme.mann.dark", "theme.mann.glass",
        "theme.edmunds", "theme.edmunds.dark", "theme.edmunds.glass", "theme.gargantua", "theme.gargantua.light", "theme.gargantua.glass",
        "theme.browser", "theme.gallery.close", "theme.gallery.previous", "theme.gallery.next",
        "theme.gallery.dark", "theme.gallery.light", "theme.gallery.glass", "memory.open", "memory.fit", "memory.forget", "splash.skip", "splash.sound",
        "tesseract.open", "tesseract.fit", "tesseract.zoom.in", "tesseract.zoom.out", "tesseract.selection.clear",
        "endurance.open", "endurance.workflow", "endurance.new", "endurance.duplicate", "endurance.validate", "endurance.run", "endurance.view",
        "endurance.recurrence.toggle", "endurance.schedule.enabled", "endurance.schedule.frequency", "endurance.schedule.save",
        "endurance.graph.fit", "endurance.graph.layout", "endurance.step.add", "endurance.step.duplicate", "endurance.step.delete", "endurance.step.changed",
        "endurance.run.open", "endurance.run.select", "endurance.run.stop", "endurance.run.resume", "endurance.approval.accept", "endurance.approval.reject"
    };
    for (const auto* name : actions) Raction_bind(name, action, this);

    Rset_ui_scale(interface_zoom_);
    const char* main_ui = std::filesystem::exists(RAVEN_IDE_UI_DIR "/main.xml")
        ? RAVEN_IDE_UI_DIR "/main.xml" : RAVEN_IDE_INSTALLED_UI_DIR "/main.xml";
    window_ = Rwindow_create_from_xml_file(main_ui);
    if (window_ == R_INVALID_ID) return false;
    theme_gallery_.load_palettes(std::filesystem::path(main_ui).parent_path() / "themes");
    const auto ui_directory = std::filesystem::path(main_ui).parent_path();
    splash_.load(ui_directory);
    opening_audio_.load(ui_directory / "audio" / "do-not-go-gentle.wav");
    update_splash_sound_control();
    Rwindow_set_icon(window_, app_icon::rgba, app_icon::width, app_icon::height);
    update_interface_density();

    Rwindow_set_event_hook(window_, window_event, this);
    Rlist_view_set_selection_changed_callback(id("threads"), list_selection, this);
    Rlist_view_set_selection_changed_callback(id("agents"), list_selection, this);
    Rlist_view_set_selection_changed_callback(id("wiki"), list_selection, this);
    Rlist_view_set_selection_changed_callback(id("knowledge.results"), list_selection, this);
    Rlist_view_set_selection_changed_callback(id("verification.results"), list_selection, this);
    Rlist_view_set_selection_changed_callback(id("mission.list"), list_selection, this);
    Rlist_view_set_selection_changed_callback(id("alerts.list"), list_selection, this);
    Rlist_view_set_selection_changed_callback(id("recall.results"), list_selection, this);
    Rfile_browser_set_accepted_callback(id("workspace.files"), file_accepted, this);
    Rnode_graph_set_selection_callback(id("graph"), [](Rid graph, int node, void* user) {
        auto& app = *static_cast<App*>(user);
        if (node == 0) {
            Rlabel_set_text(app.id("graph.selection"), "Select a mission to inspect its role and status");
            return;
        }
        const auto content = Rnode_graph_get_node_content(graph, node);
        const std::string title = Rnode_graph_get_node_title(graph, node);
        const std::string status = Rwidget_is_valid(content) ? Rlabel_get_text(Rwidget_get_child(content, 0)) : "Mission";
        Rlabel_set_text(app.id("graph.selection"), (title + "\n" + status).c_str());
    }, this);
    const auto splash = id("splash.screen");
    Rcustom_set_paint_callback(splash, [](Rid widget, RPainter* painter, float width, float height, void* user) {
        static_cast<App*>(user)->splash_.paint(widget, painter, width, height);
    }, this);
    Rwidget_set_event_callback(splash, [](Rid, const REvent* event, void* user) -> int {
        auto& app = *static_cast<App*>(user);
        if (app.splash_.event(event)) app.dismiss_splash();
        return R_TRUE;
    }, this);
    Rwidget_set_focusable(splash, R_TRUE);
    const auto current_preview = id("theme.current.art");
    Rwidget_set_user_handle(current_preview, reinterpret_cast<void*>(static_cast<intptr_t>(13)));
    Rcustom_set_paint_callback(current_preview, [](Rid widget, RPainter* painter, float width, float height, void* user) {
        static_cast<App*>(user)->theme_gallery_.paint(widget, painter, width, height);
    }, this);
    const auto carousel = id("theme.carousel");
    Rcustom_set_paint_callback(carousel, [](Rid widget, RPainter* painter, float width, float height, void* user) {
        static_cast<App*>(user)->theme_gallery_.paint_carousel(widget, painter, width, height);
    }, this);
    Rwidget_set_event_callback(carousel, [](Rid widget, const REvent* event, void* user) -> int {
        auto& app = *static_cast<App*>(user);
        const int direction = app.theme_gallery_.event(widget, event);
        if (direction != 0) app.move_theme_gallery(direction);
        return direction != 0 || event->type == R_EVENT_MOUSE_DOWN || event->type == R_EVENT_MOUSE_MOVE ||
            event->type == R_EVENT_MOUSE_UP || event->type == R_EVENT_MOUSE_WHEEL ? R_TRUE : R_FALSE;
    }, this);
    Rwidget_set_focusable(carousel, R_TRUE);
    const auto memory_canvas = id("memory.canvas");
    Rcustom_set_paint_callback(memory_canvas, [](Rid widget, RPainter* painter, float width, float height, void* user) {
        static_cast<App*>(user)->memory_.paint(widget, painter, width, height);
    }, this);
    Rwidget_set_event_callback(memory_canvas, [](Rid widget, const REvent* event, void* user) -> int {
        auto& app = *static_cast<App*>(user);
        const int handled = app.memory_.event(widget, event);
        if (event->type == R_EVENT_MOUSE_UP) app.refresh_memory_details();
        return handled;
    }, this);
    Rwidget_set_focusable(memory_canvas, R_TRUE);
    Rwidget_set_accessible_name(memory_canvas, "Memory Universe");
    Rwidget_set_accessible_description(memory_canvas, "Reviewed memories as planets. Drag to pan, scroll to zoom, arrow keys to navigate, zero to fit, and select a planet for details.");
    const auto tesseract_canvas = id("tesseract.canvas");
    Rcustom_set_paint_callback(tesseract_canvas, [](Rid widget, RPainter* painter, float width, float height, void* user) {
        static_cast<App*>(user)->tesseract_.paint(widget, painter, width, height);
    }, this);
    Rwidget_set_event_callback(tesseract_canvas, [](Rid widget, const REvent* event, void* user) -> int {
        auto& app = *static_cast<App*>(user);
        if (event->type == R_EVENT_MOUSE_DOWN && Rwidget_get_visibility(app.id("tesseract.inline.editor")) == R_VISIBLE) {
            app.finish_tesseract_text_edit(true);
        }
        const int handled = app.tesseract_.event(widget, event);
        if (app.tesseract_.consume_text_edit_request()) app.begin_tesseract_text_edit();
        if (event->type == R_EVENT_MOUSE_UP || event->type == R_EVENT_KEY_DOWN) app.refresh_tesseract_chrome();
        return handled;
    }, this);
    Rwidget_set_focusable(tesseract_canvas, R_TRUE);
    Rtext_input_set_committed_callback(id("tesseract.inline.editor"), [](Rid, const char*, void* user) {
        static_cast<App*>(user)->finish_tesseract_text_edit(true);
    }, this);
    Rwidget_set_event_callback(id("tesseract.inline.editor"), [](Rid, const REvent* event, void* user) -> int {
        auto& app = *static_cast<App*>(user);
        if (event->type == R_EVENT_KEY_DOWN && event->key == R_KEY_ESCAPE) {
            app.finish_tesseract_text_edit(false);
            return R_TRUE;
        }
        if (event->type == R_EVENT_FOCUS_LOST && Rwidget_get_visibility(app.id("tesseract.inline.editor")) == R_VISIBLE) {
            app.finish_tesseract_text_edit(true);
            return R_TRUE;
        }
        return R_FALSE;
    }, this);
    Rwidget_set_color(id("threads"), R_COLOR_INPUT, 0.0f, 0.0f, 0.0f, 0.0f);
    Rwidget_set_color(id("composer"), R_COLOR_FOCUS, 0.0f, 0.0f, 0.0f, 0.0f);
    Rwidget_set_color(id("composer"), R_COLOR_ROW_HOVERED, 0.0f, 0.0f, 0.0f, 0.0f);
    Rwidget_set_color(id("tesseract.composer"), R_COLOR_FOCUS, 0.0f, 0.0f, 0.0f, 0.0f);
    Rwidget_set_color(id("tesseract.composer"), R_COLOR_ROW_HOVERED, 0.0f, 0.0f, 0.0f, 0.0f);
    Rwidget_set_event_callback(id("tesseract.composer"), [](Rid, const REvent* event, void* user) -> int {
        auto& app = *static_cast<App*>(user);
        if (event->type == R_EVENT_KEY_DOWN && (event->key == R_KEY_ENTER || event->key == R_KEY_KP_ENTER) &&
            !(event->modifiers & R_MOD_SHIFT)) {
            app.start_run();
            return R_TRUE;
        }
        return R_FALSE;
    }, this);
    Rbutton_set_clicked_callback(id("tesseract.send"), [](Rid, void* user) {
        static_cast<App*>(user)->start_run();
    }, this);
    Rtext_input_set_changed_callback(id("navigation.search"), [](Rid, const char*, void* user) {
        static_cast<App*>(user)->refresh_lists();
    }, this);
    Rtext_input_set_changed_callback(id("palette.search"), [](Rid, const char*, void* user) {
        static_cast<App*>(user)->palette_changed();
    }, this);
    Rtext_input_set_committed_callback(id("palette.search"), [](Rid, const char*, void* user) {
        static_cast<App*>(user)->open_palette_result();
    }, this);
    Rtext_input_set_changed_callback(id("recall.search"), [](Rid, const char*, void* user) {
        static_cast<App*>(user)->refresh_recall();
    }, this);
    Rtext_input_set_committed_callback(id("recall.search"), [](Rid, const char*, void* user) {
        static_cast<App*>(user)->use_recall();
    }, this);
    Rtext_input_set_committed_callback(id("mission.title"), [](Rid, const char*, void* user) {
        static_cast<App*>(user)->create_mission();
    }, this);
    Rtext_editor_set_changed_callback(id("composer"), [](Rid, void* user) {
        static_cast<App*>(user)->composer_changed();
    }, this);
    Rtext_editor_set_completion_callback(id("composer"), [](Rid, const char* prefix, void* user) {
        static_cast<App*>(user)->complete_composer(prefix);
    }, this);
    Rtext_editor_set_changed_callback(id("code.editor"), [](Rid, void* user) {
        static_cast<App*>(user)->code_modified();
    }, this);
    Rtext_editor_set_changed_callback(id("agent.editor"), [](Rid, void* user) {
        static_cast<App*>(user)->agent_modified();
    }, this);
    Rtext_editor_set_changed_callback(id("note.editor"), [](Rid, void* user) {
        static_cast<App*>(user)->note_modified();
    }, this);
    Rtab_view_set_changed_callback(id("wiki.tabs"), tab_changed, this);
    Rwidget_set_event_callback(id("composer"), [](Rid, const REvent* event, void* user) -> int {
        auto& app = *static_cast<App*>(user);
        if (event->type == R_EVENT_FOCUS_GAINED || event->type == R_EVENT_FOCUS_LOST) app.update_composer_focus();
        if (event->type == R_EVENT_KEY_DOWN && (event->key == R_KEY_ENTER || event->key == R_KEY_KP_ENTER) &&
            !(event->modifiers & R_MOD_SHIFT)) {
            app.start_run();
            return R_TRUE;
        }
        return R_FALSE;
    }, this);
    Rwidget_set_event_callback(id("palette.search"), [](Rid, const REvent* event, void* user) -> int {
        auto& app = *static_cast<App*>(user);
        if (event->type != R_EVENT_KEY_DOWN) return R_FALSE;
        if (event->key == R_KEY_ESCAPE) {
            app.close_palette();
            return R_TRUE;
        }
        if (event->key == R_KEY_DOWN && !app.palette_entries_.empty()) {
            Rlist_view_set_selected(app.id("palette.results"), 0);
            Rwidget_focus(app.id("palette.results"));
            return R_TRUE;
        }
        return R_FALSE;
    }, this);
    Rwidget_set_event_callback(id("recall.search"), [](Rid, const REvent* event, void* user) -> int {
        auto& app = *static_cast<App*>(user);
        if (event->type != R_EVENT_KEY_DOWN) return R_FALSE;
        if (event->key == R_KEY_ESCAPE) {
            app.close_recall();
            return R_TRUE;
        }
        if (event->key == R_KEY_DOWN && !app.recall_rows_.empty()) {
            Rlist_view_set_selected(app.id("recall.results"), 0);
            Rwidget_focus(app.id("recall.results"));
            return R_TRUE;
        }
        return R_FALSE;
    }, this);
    Rwidget_set_event_callback(id("proposal.diff"), [](Rid, const REvent* event, void* user) -> int {
        if (event->type == R_EVENT_KEY_DOWN && (event->key == R_KEY_ENTER || event->key == R_KEY_KP_ENTER) &&
            (event->modifiers & R_MOD_CTRL) && (event->modifiers & R_MOD_SHIFT)) {
            static_cast<App*>(user)->accept_proposal();
            return R_TRUE;
        }
        return R_FALSE;
    }, this);
    Rlabel_set_link_callback(id("note.preview"), wiki_link, this);
    update_context_summary();
    Rset_frame_callback(frame, this);

    try {
        workspace_.initialize();
        refresh_workspace();
        seed_recall();
        load_endurance();
        tesseract_.open(workspace_.root() / ".stellar" / "tesseract.json");
        refresh_tesseract_chrome();
        apply_profile();
        reset_session();
        if (demo_) load_demo();
        else {
            if (const auto* latest = thread_store_.latest()) open_thread(latest->id);
            open_agent(0);
            const auto home = std::find_if(notes_.begin(), notes_.end(), [](const auto& note) {
                return note.title == "Home";
            });
            open_note(home == notes_.end() ? 0 : static_cast<int>(home - notes_.begin()));
            recover();
            set_status(workspace_summary());
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Cannot open workspace: %s\n", error.what());
        set_status(error.what());
        Rmessage_box(window_, "Cannot open workspace", error.what(), R_MESSAGE_OK);
        return false;
    }

    Rwindow_set_title(window_, ("Stellar — " + workspace_.name()).c_str());
    Rwindow_show(window_);
    // The opening plays on a workspace's first launch; afterwards Stellar opens straight to work.
    if (opening_seen_) {
        Rwidget_set_visibility(id("splash.container"), R_COLLAPSED);
        Rwidget_focus(id("composer"));
    } else {
        splash_.begin();
        Rwidget_focus(id("splash.skip"));
    }
    return true;
}

void App::set_status(const std::string& text) { Rlabel_set_text(id("status"), text.c_str()); }

// The header and the Activity card show the same lead state; this is their only writer.
void App::set_run_state(const char* state) {
    Rlabel_set_text(id("run.state"), state);
    static const std::pair<const char*, const char*> lead_states[] = {
        {"READY", "Ready"}, {"RUNNING", "Running"}, {"COMPLETE", "Complete"}, {"ERROR", "Error"},
        {"STOPPED", "Stopped"}, {"KNOWLEDGE REVIEW", "Reviewing knowledge"}, {"KNOWLEDGE ERROR", "Knowledge review failed"},
        {"DEMO · ACTIVE", "Running · demo"}
    };
    const char* lead = state;
    for (const auto& [header, label] : lead_states) {
        if (std::strcmp(header, state) == 0) lead = label;
    }
    Rlabel_set_text(id("activity.lead"), lead);
    refresh_threads();
}

std::string App::workspace_summary() const {
    const auto repositories = repositories_.size();
    return "Workspace ready · " + std::to_string(repositories) + (repositories == 1 ? " repository · " : " repositories · ") +
        counted(agents_.size(), "agent");
}

void App::play_opening_score() {
    if (opening_sound_ && splash_.active()) opening_audio_.play();
}

void App::dismiss_splash() {
    splash_.dismiss();
    opening_audio_.stop();
    Rwidget_set_opacity(id("splash.container"), 1.0f);
    Rwidget_set_visibility(id("splash.container"), R_COLLAPSED);
    Rwidget_focus(id("composer"));
    if (!opening_seen_) {
        opening_seen_ = true;
        save_profile();
    }
}

void App::toggle_splash_sound() {
    opening_sound_ = !opening_sound_;
    if (opening_sound_) play_opening_score();
    else opening_audio_.stop();
    update_splash_sound_control();
    save_profile();
}

void App::update_splash_sound_control() {
    const bool audible = opening_sound_ && opening_audio_.available();
    Rwidget_set_visibility(id("splash.sound.on"), audible ? R_VISIBLE : R_COLLAPSED);
    Rwidget_set_visibility(id("splash.sound.off"), audible ? R_COLLAPSED : R_VISIBLE);
    Rwidget_set_enabled(id("splash.sound"), opening_audio_.available());
    const char* action = audible ? "Mute original score" : "Play original score";
    Rwidget_set_tooltip(id("splash.sound"), action);
    Rwidget_set_accessible_name(id("splash.sound"), action);
    Rlabel_set_text(id("splash.sound.title"), opening_audio_.available()
        ? "Do Not Go Gentle · Original score" : "Original score unavailable");
}

void App::refresh_workspace() {
    repositories_ = workspace_.repositories();
    agents_ = workspace_.agents();
    notes_ = workspace_.wiki_notes();
    Rbutton_set_text(id("workspace.name"), shortened(workspace_.name(), 18).c_str());
    Rwidget_set_tooltip(id("workspace.name"), workspace_.root().c_str());
    Rfile_browser_set_directory(id("workspace.files"), workspace_.root().c_str());
    thread_store_ = ThreadStore(workspace_.root());
    thread_store_.load();
    checkpoint_store_ = CheckpointStore(workspace_.root());
    checkpoint_store_.load();
    recovery_store_ = RecoveryStore(workspace_.root());
    notification_store_ = NotificationStore(workspace_.root());
    notification_store_.load();
    refresh_notifications();
    refresh_threads();
    refresh_checkpoint_history();
    refresh_composer_index();
    refresh_lists();
    refresh_knowledge();
    refresh_missions();
}

void App::refresh_composer_index() {
    composer_index_.set_workspace(workspace_.root());
    composer_index_.refresh();
}

void App::complete_composer(const char* prefix) {
    // @ file mentions work with both harnesses; / skills and commands exist only for Kimi Code.
    const auto editor = id("composer");
    int line_number = 0;
    int column = 0;
    Rtext_editor_get_cursor(editor, &line_number, &column);
    const char* content = Rtext_editor_get_line(editor, line_number);
    if (!content) return;
    const std::string line = content;
    const std::string query = prefix ? prefix : "";
    const int start = column - static_cast<int>(query.size());
    if (start < 0 || start > static_cast<int>(line.size())) return;

    const auto at = line.rfind('@', static_cast<size_t>(start));
    if (at != std::string::npos) {
        const auto token = line.substr(at + 1, static_cast<size_t>(start) - at - 1);
        if (token.find_first_of(" \t") == std::string::npos) {
            const auto slash = token.rfind('/');
            const auto directory = slash == std::string::npos ? "" : token.substr(0, slash + 1);
            const auto expected = static_cast<int>(at + 1 + directory.size());
            if (expected == start) {
                for (const auto& item : composer_index_.files(directory, query)) {
                    Rtext_editor_add_completion(editor, item.text.c_str(), item.detail.c_str());
                }
                return;
            }
        }
    }

    if (harness_ != Harness::KimiCode) return;
    const auto colon = start - 1;
    const bool namespaced = colon >= 6 && line[colon] == ':' && line.compare(static_cast<size_t>(colon - 6), 6, "/skill") == 0 &&
        (colon == 6 || std::isspace(static_cast<unsigned char>(line[colon - 7])));
    const bool shorthand = start > 0 && line[start - 1] == '/' &&
        (start == 1 || std::isspace(static_cast<unsigned char>(line[start - 2])));
    if (!namespaced && !shorthand) return;
    for (const auto& item : composer_index_.skills(query, namespaced)) {
        Rtext_editor_add_completion(editor, item.text.c_str(), item.detail.c_str());
    }
    if (!namespaced) {
        for (const auto& command : system_commands()) {
            if (command.text.rfind(query, 0) == 0) {
                Rtext_editor_add_completion(editor, command.text.c_str(), command.detail.c_str());
            }
        }
    }
}

bool App::composer_token_complete() const {
    const auto editor = id("composer");
    int line_number = 0;
    int column = 0;
    Rtext_editor_get_cursor(editor, &line_number, &column);
    const char* content = Rtext_editor_get_line(editor, line_number);
    if (!content) return false;
    const std::string before = std::string(content).substr(0, static_cast<size_t>(column));
    const auto space = before.find_last_of(" \t");
    const auto token = before.substr(space == std::string::npos ? 0 : space + 1);
    if (token.rfind("@", 0) == 0) return composer_index_.has_file(token.substr(1));
    if (token.rfind("/skill:", 0) == 0) return composer_index_.has_skill(token.substr(7));
    if (token.rfind("/", 0) == 0) {
        const auto name = token.substr(1);
        return composer_index_.has_skill(name) || is_system_command(name);
    }
    return false;
}

float composer_editor_height(Rid editor) {
    return std::clamp(28.0f + 20.0f * static_cast<float>(Rtext_editor_get_line_count(editor)), 48.0f, 88.0f);
}

void App::composer_changed() {
    Rwidget_set_size(id("composer"), -1.0f, composer_editor_height(id("composer")));
    update_context_summary();
    schedule_recovery_save();
    if (composer_token_complete()) {
        composer_completion_pending_ = false;
        Rwindow_close_popups(window_);
    } else {
        composer_completion_pending_ = true;
    }
}

void App::update_context_summary() {
    const auto attachments = composer_index_.mention_details(Rtext_editor_get_text(id("composer")));
    Rwidget_set_visibility(id("context.bar"), attachments.empty() ? R_COLLAPSED : R_VISIBLE);
    const auto previous = Rcombo_box_get_selected(id("context.files"));
    const auto previous_path = previous >= 0 && previous < static_cast<int>(context_attachment_paths_.size())
        ? context_attachment_paths_[previous] : std::string{};
    context_attachment_paths_.clear();
    Rcombo_box_clear_items(id("context.files"));
    std::uintmax_t total = 0;
    int selected = -1;
    for (const auto& attachment : attachments) {
        const auto relative = std::filesystem::relative(attachment.path, workspace_.root()).generic_string();
        if (relative == previous_path) selected = static_cast<int>(context_attachment_paths_.size());
        context_attachment_paths_.push_back(relative);
        total += attachment.tokens;
        Rcombo_box_add_item(id("context.files"), (relative + " · ~" + std::to_string(attachment.tokens) + " tokens").c_str());
    }
    Rcombo_box_set_selected(id("context.files"), selected >= 0 ? selected : attachments.empty() ? -1 : 0);
    if (!attachments.empty()) {
        Rlabel_set_text(id("context.summary"), (std::to_string(attachments.size()) +
            (attachments.size() == 1 ? " file · ~" : " files · ~") + std::to_string(total) + " tokens").c_str());
    }
    Rwidget_set_enabled(id("context.compact"), thread_messages_.size() > 4);
}

// Image bytes stay outside text editors and recovery JSON.
std::optional<App::ImageAttachment>& App::image_for(bool tesseract) {
    return tesseract ? tesseract_image_ : composer_image_;
}

Rid& App::texture_for(bool tesseract) {
    return tesseract ? tesseract_texture_ : composer_texture_;
}

void App::refresh_image_attachment(bool tesseract) {
    const std::string prefix = tesseract ? "tesseract.image" : "composer.image";
    auto& attachment = image_for(tesseract);
    auto& texture = texture_for(tesseract);
    if (Rtexture_is_valid(texture)) Rtexture_destroy(texture);
    texture = R_INVALID_ID;
    Rwidget_set_visibility(id((prefix + ".bar").c_str()), attachment ? R_VISIBLE : R_COLLAPSED);
    if (attachment) {
        texture = Rtexture_create_from_file_async(attachment->path.string().c_str());
        Rimage_set_texture(id((prefix + ".preview").c_str()), texture);
        const auto kilobytes = std::max<std::uintmax_t>(1, (attachment->bytes + 1023) / 1024);
        Rlabel_set_text(id((prefix + ".details").c_str()),
            (std::to_string(attachment->width) + " × " + std::to_string(attachment->height) + " · " +
             std::to_string(kilobytes) + " KB").c_str());
    } else {
        Rimage_set_texture(id((prefix + ".preview").c_str()), R_INVALID_ID);
    }
    if (tesseract) refresh_tesseract_chrome();
}

bool App::paste_image(bool tesseract) {
    size_t size = 0;
    auto* data = Rclipboard_get_image(&size);
    if (!data) return false;
    if (size > 20 * 1024 * 1024) {
        Rmemory_free(data);
        set_status("Image is too large · paste an image smaller than 20 MB");
        return true;
    }
    int width = 0;
    int height = 0;
    auto* pixels = Rimage_decode(data, size, &width, &height);
    if (!pixels || width <= 0 || height <= 0 || static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) > 40'000'000) {
        Rpixels_free(pixels);
        Rmemory_free(data);
        set_status("Clipboard image is not a supported PNG, JPEG, BMP, or TGA image");
        return true;
    }
    Rpixels_free(pixels);

    std::string extension = ".png";
    std::string mime_type = "image/png";
    if (size >= 3 && static_cast<unsigned char>(data[0]) == 0xff && static_cast<unsigned char>(data[1]) == 0xd8 &&
        static_cast<unsigned char>(data[2]) == 0xff) {
        extension = ".jpg";
        mime_type = "image/jpeg";
    } else if (size >= 2 && data[0] == 'B' && data[1] == 'M') {
        extension = ".bmp";
        mime_type = "image/bmp";
    } else if (!(size >= 8 && static_cast<unsigned char>(data[0]) == 0x89 &&
                 std::string_view(reinterpret_cast<const char*>(data) + 1, 3) == "PNG")) {
        extension = ".tga";
        mime_type = "image/tga";
    }
    static std::uint64_t sequence = 0;
    const auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
    const auto path = workspace_.root() / ".stellar" / "attachments" /
        ("pasted-" + std::to_string(stamp) + "-" + std::to_string(++sequence) + extension);
    try {
        write_file(path, std::string(reinterpret_cast<const char*>(data), size));
    } catch (const std::exception& error) {
        Rmemory_free(data);
        set_status(std::string("Could not save pasted image · ") + error.what());
        return true;
    }
    Rmemory_free(data);
    image_for(tesseract) = ImageAttachment{path, mime_type, width, height, size};
    refresh_image_attachment(tesseract);
    set_status("Image attached · add a prompt or send it as is");
    return true;
}

void App::remove_image(bool tesseract) {
    image_for(tesseract).reset();
    refresh_image_attachment(tesseract);
    Rwidget_focus(id(tesseract ? "tesseract.composer" : "composer"));
    set_status("Image attachment removed");
}

void App::clear_images() {
    composer_image_.reset();
    tesseract_image_.reset();
    queued_image_.reset();
    last_image_.reset();
    refresh_image_attachment(false);
    refresh_image_attachment(true);
}

void App::clear_context() {
    Rtext_editor_set_text(id("composer"), composer_index_.strip_mentions(Rtext_editor_get_text(id("composer"))).c_str());
    Rwindow_close_popups(window_);
    update_context_summary();
    Rwidget_focus(id("composer"));
    set_status("Workspace file attachments cleared");
}

void App::remove_context_file() {
    const auto index = Rcombo_box_get_selected(id("context.files"));
    if (index < 0 || index >= static_cast<int>(context_attachment_paths_.size())) return;
    const auto relative = context_attachment_paths_[index];
    Rtext_editor_set_text(id("composer"), composer_index_.strip_mention(Rtext_editor_get_text(id("composer")), relative).c_str());
    Rwindow_close_popups(window_);
    update_context_summary();
    Rwidget_focus(id("context.files"));
    set_status("Removed attachment · " + relative);
}

void App::compact_context() {
    if (thread_messages_.size() <= 4) {
        set_status("There is not enough conversation to compact");
        return;
    }
    std::vector<ThreadMessage> retained;
    for (const auto& message : thread_messages_) {
        if (message.user) {
            retained.push_back(message);
            break;
        }
    }
    const auto recent_start = thread_messages_.size() > 6 ? thread_messages_.size() - 6 : 0;
    for (size_t i = recent_start; i < thread_messages_.size(); ++i) {
        if (retained.empty() || thread_messages_[i].text != retained.front().text) retained.push_back(thread_messages_[i]);
    }
    transcript_text_ = "CONTEXT COMPACTED LOCALLY: original task and recent messages retained.\n";
    for (const auto& message : retained) transcript_text_ += "\n\n" + message.role + ":\n" + message.text;
    session_id_.clear();
    set_status("Context compacted · full thread preserved; next run uses the task and " + counted(retained.size(), "message"));
}

void App::seed_recall() {
    if (recall_store_.storage_exists()) return;
    for (const auto& thread : thread_store_.threads()) {
        for (const auto& message : thread.messages) {
            if (message.user) recall_store_.remember(message.text, thread.updated_at, false);
        }
    }
    recall_store_.save();
}

void App::open_recall() {
    if (active_view(id("center.tabs")) == 5) {
        set_status("Recall is available in Agent · Tesseract keeps its own UI-creation flow");
        return;
    }
    Rwidget_set_visibility(id("run.setup"), R_COLLAPSED);
    Rwidget_set_visibility(id("command.palette"), R_COLLAPSED);
    Rwidget_set_visibility(id("theme.gallery"), R_COLLAPSED);
    Rwindow_close_popups(window_);
    set_active_view(id("center.tabs"), 0);
    Rtext_input_set_text(id("recall.search"), "");
    refresh_recall();
    Rwidget_set_visibility(id("recall.palette"), R_VISIBLE);
    Rwidget_focus(id("recall.search"));
}

void App::close_recall() {
    if (Rwidget_get_visibility(id("recall.palette")) != R_VISIBLE) return;
    Rwidget_set_visibility(id("recall.palette"), R_COLLAPSED);
    Rwidget_focus(id("composer"));
}

void App::refresh_recall() {
    const auto selected = Rlist_view_get_selected(id("recall.results"));
    const std::string selected_id = selected >= 0 && selected < static_cast<int>(recall_rows_.size()) ? recall_rows_[selected] : "";
    const auto prompts = recall_store_.recommend(Rtext_input_get_text(id("recall.search")));
    recall_rows_.clear();
    Rlist_view_clear_items(id("recall.results"));
    int selection = prompts.empty() ? -1 : 0;
    for (std::size_t i = 0; i < prompts.size(); ++i) {
        const auto* prompt = prompts[i];
        recall_rows_.push_back(prompt->id);
        if (prompt->id == selected_id) selection = static_cast<int>(i);
        auto preview = prompt->text;
        std::replace(preview.begin(), preview.end(), '\n', ' ');
        // The prompt leads; pin state and reuse count follow, and a single use is not worth a label.
        std::string meta;
        if (prompt->pinned) meta = "pinned";
        if (prompt->uses > 1) meta += (meta.empty() ? "" : " · ") + std::to_string(prompt->uses) + " uses";
        const auto label = shortened(preview, 100) + (meta.empty() ? "" : "  ·  " + meta);
        Rlist_view_add_item(id("recall.results"), label.c_str());
    }
    Rlist_view_set_selected(id("recall.results"), selection);
    const auto* current = selection >= 0 ? prompts[selection] : nullptr;
    Rwidget_set_enabled(id("recall.pin"), current != nullptr);
    Rwidget_set_enabled(id("recall.erase"), current != nullptr);
    Rbutton_set_text(id("recall.pin"), current && current->pinned ? "Unpin" : "Pin");
    const auto status = recall_store_.entries().empty()
        ? std::string("Run a prompt once and it will appear here. Pin favorites to keep them first.")
        : prompts.empty() ? std::string("No saved prompts match this search.")
        : std::to_string(prompts.size()) + " saved prompt" + (prompts.size() == 1 ? "" : "s") + " · Enter to reuse";
    Rlabel_set_text(id("recall.status"), status.c_str());
}

void App::use_recall() {
    const auto selected = Rlist_view_get_selected(id("recall.results"));
    if (selected < 0 || selected >= static_cast<int>(recall_rows_.size())) return;
    const auto* prompt = recall_store_.find(recall_rows_[selected]);
    if (!prompt) return;
    Rtext_editor_set_text(id("composer"), prompt->text.c_str());
    composer_changed();
    close_recall();
    set_status("Prompt restored from Recall · review it, then send");
}

void App::toggle_recall_pin() {
    const auto selected = Rlist_view_get_selected(id("recall.results"));
    if (selected < 0 || selected >= static_cast<int>(recall_rows_.size())) return;
    const auto pinned = recall_store_.toggle_pin(recall_rows_[selected]);
    refresh_recall();
    set_status(pinned ? "Prompt pinned to the top of Recall" : "Prompt unpinned");
}

void App::erase_recall() {
    const auto selected = Rlist_view_get_selected(id("recall.results"));
    if (selected < 0 || selected >= static_cast<int>(recall_rows_.size())) return;
    const auto* prompt = recall_store_.find(recall_rows_[selected]);
    if (!prompt) return;
    if (Rmessage_box(window_, "Forget saved prompt", "Remove this prompt from Recall? The conversation history will not change.", R_MESSAGE_YES_NO) != R_MESSAGE_RESULT_YES) return;
    recall_store_.erase(prompt->id);
    refresh_recall();
    set_status("Prompt removed from Recall");
}

void App::open_palette(PaletteMode mode) {
    palette_return_tab_ = active_view(id("center.tabs"));
    palette_mode_ = mode;
    palette_search_pending_ = false;
    Rwidget_set_visibility(id("run.setup"), R_COLLAPSED);
    Rwidget_set_visibility(id("recall.palette"), R_COLLAPSED);
    Rwidget_set_visibility(id("theme.gallery"), R_COLLAPSED);
    Rwindow_close_popups(window_);
    static const char* titles[] = {"Command palette", "Open workspace file", "Search workspace"};
    static const char* placeholders[] = {"Type a command…", "Filter workspace files…", "Find text in workspace files…"};
    const auto index = static_cast<int>(palette_mode_);
    Rlabel_set_text(id("palette.title"), titles[index]);
    Rtext_input_set_placeholder(id("palette.search"), placeholders[index]);
    Rtext_input_set_text(id("palette.search"), "");
    Rwidget_set_visibility(id("command.palette"), R_VISIBLE);
    refresh_palette();
    Rwidget_focus(id("palette.search"));
    Rtext_input_select_all(id("palette.search"));
}

void App::close_palette() {
    if (Rwidget_get_visibility(id("command.palette")) != R_VISIBLE) return;
    palette_search_pending_ = false;
    Rwidget_set_visibility(id("command.palette"), R_COLLAPSED);
    switch (palette_return_tab_) {
    case 1: Rwidget_focus(id("code.editor")); break;
    case 2: Rwidget_focus(id("graph")); break;
    case 4: Rwidget_focus(id("proposal.diff")); break;
    case 5: Rwidget_focus(id("tesseract.composer")); break;
    case 3:
        switch (active_view(id("more.tabs"))) {
        case 0: Rwidget_focus(id("agent.editor")); break;
        case 1: Rwidget_focus(id("settings.close")); break;
        case 2: Rwidget_focus(id("memory.canvas")); break;
        case 3: Rwidget_focus(id("mission.list")); break;
        }
        break;
    default: Rwidget_focus(id("composer")); break;
    }
}

void App::palette_changed() {
    if (palette_mode_ != PaletteMode::Content) {
        refresh_palette();
        return;
    }
    palette_search_pending_ = true;
    palette_search_due_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
    Rlabel_set_text(id("palette.status"), "Searching…");
}

void App::refresh_palette() {
    palette_entries_.clear();
    Rlist_view_clear_items(id("palette.results"));
    const std::string query = Rtext_input_get_text(id("palette.search"));
    if (palette_mode_ == PaletteMode::Commands) {
        const auto needle = lowercase(query);
        for (const auto& command : palette_commands()) {
            if (!needle.empty() && lowercase(std::string(command.name) + " " + command.detail).find(needle) == std::string::npos) continue;
            palette_entries_.push_back({command.action, {}, 0});
            Rlist_view_add_item(id("palette.results"), (std::string(command.name) + " · " + command.detail).c_str());
        }
    } else if (palette_mode_ == PaletteMode::Files) {
        for (const auto& file : composer_index_.search_files(query)) {
            palette_entries_.push_back({"", workspace_.root() / file.text, 0});
            Rlist_view_add_item(id("palette.results"), (file.text + " · " + file.detail).c_str());
        }
    } else {
        const auto clean = query;
        if (clean.size() < 2) {
            Rlabel_set_text(id("palette.status"), "Type at least 2 characters · Enter opens the match · Esc closes");
            return;
        }
        for (const auto& match : composer_index_.search_content(clean)) {
            PaletteEntry entry;
            entry.path = workspace_.root() / match.path;
            entry.line = match.line;
            palette_entries_.push_back(std::move(entry));
            const auto label = match.path + ":" + std::to_string(match.line) + " · " + match.text;
            Rlist_view_add_item(id("palette.results"), label.c_str());
        }
    }
    Rlist_view_set_selected(id("palette.results"), palette_entries_.empty() ? -1 : 0);
    const auto status = palette_entries_.empty() ? "No matching results" :
        std::to_string(palette_entries_.size()) + " result" + (palette_entries_.size() == 1 ? "" : "s") + " · Enter to open · Esc to close";
    Rlabel_set_text(id("palette.status"), status.c_str());
}

void App::open_palette_result() {
    const auto index = Rlist_view_get_selected(id("palette.results"));
    if (index < 0 || index >= static_cast<int>(palette_entries_.size())) return;
    const auto entry = palette_entries_[index];
    close_palette();
    if (!entry.action.empty()) action(id("palette.results"), entry.action.c_str(), this);
    else open_code_file(entry.path, entry.line);
}

void App::pump_palette() {
    if (!palette_search_pending_ || std::chrono::steady_clock::now() < palette_search_due_) return;
    palette_search_pending_ = false;
    refresh_palette();
}

void App::refresh_lists() {
    const auto agent_query = lowercase(Rtext_input_get_text(id("agent.search")));
    const auto note_query = lowercase(Rtext_input_get_text(id("notes.search")));
    agent_rows_.clear();
    note_rows_.clear();
    Rlist_view_clear_items(id("agents"));
    for (int i = 0; i < static_cast<int>(agents_.size()); ++i) {
        const auto& agent = agents_[i];
        if (!agent_query.empty() && lowercase(agent.name + " " + agent.description).find(agent_query) == std::string::npos) continue;
        agent_rows_.push_back(i);
        Rlist_view_add_item(id("agents"), agent.name.c_str());
    }

    Rlist_view_clear_items(id("wiki"));
    for (int i = 0; i < static_cast<int>(notes_.size()); ++i) {
        const auto& note = notes_[i];
        if (!note_query.empty() && lowercase(note.title + " " + note.content).find(note_query) == std::string::npos) continue;
        note_rows_.push_back(i);
        Rlist_view_add_item(id("wiki"), note.title.c_str());
    }
    refresh_threads();
}

void App::refresh_threads() {
    if (!Rwidget_is_valid(id("threads"))) return;
    const auto query = lowercase(Rtext_input_get_text(id("navigation.search")));
    thread_rows_.clear();
    Rlist_view_clear_items(id("threads"));
    // The heading is shared with Agents and Notes; it only names chats while chats are shown.
    if (active_view(id("left.tabs")) == 0) Rlabel_set_text(id("threads.section"), show_archived_ ? "Archived chats" : "Recent chats");
    Rbutton_set_text(id("threads.view"), show_archived_ ? "Active" : "Archived");
    Rwidget_set_tooltip(id("threads.view"), show_archived_ ? "Show active conversations" : "Show archived conversations");
    Rbutton_set_text(id("threads.archive"), "");
    const auto action_name = show_archived_ ? "Restore selected conversation" : "Archive selected conversation";
    Rwidget_set_tooltip(id("threads.archive"), action_name);
    Rwidget_set_accessible_name(id("threads.archive"), action_name);
    int selected = -1;
    for (const auto& thread : thread_store_.threads()) {
        if (thread.archived != show_archived_) continue;
        if (!query.empty() && lowercase(thread.title).find(query) == std::string::npos) continue;
        if (thread.id == current_thread_id_) selected = static_cast<int>(thread_rows_.size());
        thread_rows_.push_back(thread.id);
        // Rows carry a short title plus the conversation's state or age, so a running or failed chat
        // can be told apart without opening it. The sidebar clips without an ellipsis.
        std::string suffix;
        if (thread.id == current_thread_id_ && (lead_process_.running() || wiki_process_.running())) suffix = "running";
        else if (thread.id == current_thread_id_ && last_run_failed_) suffix = "failed";
        else suffix = relative_age(thread.updated_at);
        const size_t width = suffix.empty() ? 30 : 30 - std::min<size_t>(suffix.size() + 3, 12);
        Rlist_view_add_item(id("threads"), (shortened(thread.title, width) + (suffix.empty() ? "" : " · " + suffix)).c_str());
    }
    if (selected < 0 && show_archived_ && !thread_rows_.empty()) selected = 0;
    Rlist_view_set_selected(id("threads"), selected);
    Rwidget_set_enabled(id("threads.archive"), !thread_rows_.empty());
    const bool empty = thread_rows_.empty();
    const auto empty_text = !query.empty() ? "No matching chats" : show_archived_ ? "No archived chats" : "No chats yet";
    Rlabel_set_text(id("threads.empty"), empty_text);
    Rwidget_set_visibility(id("threads.empty"), empty ? R_VISIBLE : R_COLLAPSED);
    Rwidget_set_visibility(id("threads"), empty ? R_COLLAPSED : R_VISIBLE);
}

void App::toggle_archived_threads() {
    show_archived_ = !show_archived_;
    refresh_threads();
    set_status(show_archived_ ? "Archived conversations · select one to restore" : "Active conversations");
}

void App::archive_selected_thread() {
    if (lead_process_.running() || wiki_process_.running()) {
        set_status("Finish the current run before archiving or restoring conversations");
        return;
    }
    const auto row = Rlist_view_get_selected(id("threads"));
    if (row < 0 || row >= static_cast<int>(thread_rows_.size())) {
        set_status(show_archived_ ? "Select an archived conversation to restore" : "Select a conversation to archive");
        return;
    }
    const auto thread_id = thread_rows_[row];
    if (thread_id == current_thread_id_) save_thread();
    auto* thread = thread_store_.find(thread_id);
    if (!thread) return;
    if (show_archived_) {
        thread->archived = false;
        thread_store_.save();
        show_archived_ = false;
        open_thread(thread_id);
        set_status("Conversation restored to active chats");
        return;
    }
    thread->archived = true;
    thread_store_.save();
    if (thread_id == current_thread_id_) {
        current_thread_id_.clear();
        thread_messages_.clear();
        recovery_store_.clear_draft();
        reset_session();
        set_active_view(id("center.tabs"), 0);
        Rwidget_focus(id("composer"));
    }
    refresh_threads();
    set_status("Conversation archived · open Archived to restore it");
}

// Replaces an automatic title with the one the knowledge curator proposed. A title the user has
// already changed is left alone: only titles still equal to the prompt-derived one are replaced.
void App::apply_agent_title(const std::string& proposed) {
    // The title comes from model output: keep one line, collapse spacing, and cap it at six words
    // through the same cleanup as prompt-derived titles.
    auto title = proposed;
    for (auto& c : title) {
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    }
    title = conversation_title(title);
    if (title == "New conversation") return;
    while (!title.empty() && (title.back() == '.' || title.back() == ' ' || title.back() == '"')) title.pop_back();
    while (!title.empty() && (title.front() == '"' || title.front() == ' ')) title.erase(0, 1);
    if (title.empty() || thread_messages_.empty()) return;
    const auto first = std::find_if(thread_messages_.begin(), thread_messages_.end(), [](const auto& message) { return message.user; });
    if (first == thread_messages_.end() || run_title_ != shortened(conversation_title(first->text), 48)) return;
    run_title_ = shortened(title, 48);
    Rlabel_set_text(id("run.title"), shortened(run_title_, 36).c_str());
    Rwidget_set_tooltip(id("run.title"), run_title_.c_str());
    save_thread();
    try {
        // Re-stage the episode under the new name; the curator result below reads staged writes.
        workspace_.stage_run_episode(session_id_, run_title_, harness_name(), transcript_text_, "completed");
    } catch (const std::exception&) {
    }
}

void App::save_thread() {
    if (thread_messages_.empty()) return;
    if (current_thread_id_.empty()) {
        static int sequence = 0;
        current_thread_id_ = "thread-" + std::to_string(std::time(nullptr)) + "-" + std::to_string(++sequence);
    }
    ConversationThread thread;
    thread.id = current_thread_id_;
    thread.title = run_title_.empty() ? "New conversation" : run_title_;
    thread.session_id = session_id_;
    thread.transcript = transcript_text_;
    thread.harness = harness_;
    thread.updated_at = std::time(nullptr);
    if (const auto* saved = thread_store_.find(current_thread_id_)) {
        thread.archived = saved->archived;
        // Saving on switch, close, or restore is not activity: keep the time of the last real change
        // so recency order and the "5m" age in the chat list stay truthful.
        const bool unchanged = saved->messages.size() == thread_messages_.size() && saved->title == thread.title &&
            saved->session_id == thread.session_id && saved->transcript == thread.transcript;
        if (unchanged) thread.updated_at = saved->updated_at;
    }
    thread.messages = thread_messages_;
    thread_store_.upsert(std::move(thread));
    thread_store_.save();
    refresh_threads();
}

void App::new_thread() {
    if (lead_process_.running() || wiki_process_.running()) {
        set_status("Finish the current run before starting a new chat");
        return;
    }
    save_thread();
    current_thread_id_.clear();
    thread_messages_.clear();
    recovery_store_.clear_draft();
    reset_session();
    show_archived_ = false;
    refresh_threads();
    set_active_view(id("center.tabs"), 0);
    Rwidget_focus(id("composer"));
    set_status("New chat · previous conversations are saved in Recent chats");
}

void App::open_thread(const std::string& thread_id) {
    if (lead_process_.running() || wiki_process_.running()) {
        set_status("Finish the current run before switching threads");
        return;
    }
    const auto* saved = thread_store_.find(thread_id);
    if (!saved) return;
    if (saved->archived) {
        set_status("Restore this conversation before opening it");
        return;
    }
    show_archived_ = false;
    const auto thread = *saved;
    save_thread();
    reset_session();
    current_thread_id_ = thread.id;
    session_id_ = thread.session_id;
    transcript_text_ = thread.transcript;
    run_title_ = thread.title;
    harness_ = thread.harness;
    thread_messages_ = thread.messages;
    Rcombo_box_set_selected(id("harness"), static_cast<int>(harness_));
    Rcombo_box_set_selected(id("settings.harness"), static_cast<int>(harness_));
    Rwidget_clear_children(id("transcript"));
    for (const auto& message : thread_messages_) add_message(message.role, message.text, message.user);
    // Message heights are only final after the next layout, so scroll to the end again then.
    transcript_follow_frames_ = 3;
    update_context_summary();
    Rlabel_set_text(id("run.title"), shortened(run_title_, 36).c_str());
    Rwidget_set_tooltip(id("run.title"), run_title_.c_str());
    Rlabel_set_text(id("run.subtitle"), ("Orchestrator · " + std::string(harness_name()) + " · resumable session").c_str());
    set_run_state(thread_messages_.empty() ? "READY" : "COMPLETE");
    if (!thread_messages_.empty()) {
        set_current_action("Last run complete · the next prompt resumes this session");
        // Usage is reported per run and is not stored with the thread.
        Rlabel_set_text(id("run.usage"), "Run cost · not recorded for earlier runs");
    }
    refresh_threads();
    set_active_view(id("center.tabs"), 0);
    if (!checkpoint_store_.checkpoints().empty() && checkpoint_store_.checkpoints().front().status == "interrupted") {
        for (auto it = thread_messages_.rbegin(); it != thread_messages_.rend(); ++it) {
            if (it->user) {
                last_prompt_ = it->text;
                break;
            }
        }
        Rwidget_set_visibility(id("run.retry"), last_prompt_.empty() ? R_COLLAPSED : R_VISIBLE);
        set_status("Interrupted run recovered · Retry when ready");
        return;
    }
    set_status("Conversation restored · the next prompt resumes this thread");
}

void App::reset_session() {
    lead_process_.stop();
    wiki_process_.stop();
    clear_images();
    queued_prompt_.clear();
    queued_tesseract_ = false;
    queued_tesseract_context_.clear();
    run_tesseract_ = false;
    run_tesseract_context_.clear();
    tesseract_context_override_.reset();
    last_prompt_.clear();
    last_run_failed_ = false;
    run_elapsed_seconds_ = 0.0;
    displayed_elapsed_seconds_ = -1;
    current_action_ = "Waiting for a task";
    Rwidget_set_visibility(id("run.retry"), R_COLLAPSED);
    Rwidget_set_visibility(id("run.timing"), R_COLLAPSED);
    update_queue();
    update_run_timer();
    session_id_.clear();
    transcript_text_.clear();
    run_title_.clear();
    current_thread_id_.clear();
    thread_messages_.clear();
    current_message_ = R_INVALID_ID;
    pending_message_ = R_INVALID_ID;
    activity_expander_ = R_INVALID_ID;
    activity_rows_ = R_INVALID_ID;
    current_text_.clear();
    tool_activity_status_.clear();
    active_checks_.clear();
    activity_count_ = 0;
    activity_failed_ = 0;
    saw_delta_ = false;
    // A pending knowledge proposal belongs to the workspace, not to the conversation: starting or
    // switching chats keeps it, including its unreviewed wiki edits (only graph writes are stored
    // on disk, so resetting it here silently dropped the Markdown changes).
    Rwidget_set_visibility(id("proposal.bar"), proposal_ ? R_VISIBLE : R_COLLAPSED);
    Rwidget_clear_children(id("transcript"));
    add_message("READY",
        "Describe a change or ask about this workspace. Stellar will inspect the code, implement what is needed, run relevant checks, and report the result.");
    Rlabel_set_text(id("run.title"), "New chat");
    // An empty conversation has not run yet; open_thread sets the restored state afterwards.
    set_run_state("READY");
    Rlabel_set_text(id("run.subtitle"), (std::string("Lead agent: orchestrator · ") + harness_name()).c_str());
    Rwidget_set_enabled(id("harness"), R_TRUE);
    Rwidget_set_enabled(id("run.mode"), R_TRUE); Rwidget_set_enabled(id("model.select"), R_TRUE); Rwidget_set_enabled(id("effort.select"), R_TRUE);
    reset_graph();
    if (!proposal_) {
        const auto staged = workspace_.knowledge().staged();
        // Unreviewed wiki edits saved by an earlier session come back with the staged graph writes.
        std::vector<std::pair<std::string, std::string>> saved_edits;
        std::string saved_summary;
        try {
            const auto path = workspace_.root() / ".stellar" / "pending-wiki-edits.json";
            if (std::filesystem::exists(path)) {
                const auto data = nlohmann::json::parse(read_file(path));
                saved_summary = data.value("summary", "");
                for (const auto& edit : data.at("edits")) saved_edits.emplace_back(edit.at("path"), edit.at("content"));
            }
        } catch (const std::exception&) {
            saved_edits.clear();
        }
        if (!saved_edits.empty()) {
            try {
                proposal_ = workspace_.validate_proposal(saved_summary.empty() ? "Suggested wiki updates" : saved_summary, saved_edits, staged);
                Rlabel_set_text(id("proposal.summary"), shortened(proposal_->summary, 140).c_str());
                Rwidget_set_visibility(id("proposal.bar"), R_VISIBLE);
            } catch (const std::exception&) {
                proposal_.reset();
            }
        }
        if (!proposal_ && !staged.empty()) {
            const auto episodes = static_cast<size_t>(std::count_if(staged.begin(), staged.end(), [](const auto& operation) {
                return operation.entity_type == "episode";
            }));
            const auto summary = episodes == staged.size()
                ? (episodes == 1 ? std::string("1 run memory") : std::to_string(episodes) + " run memories") + " from earlier work, waiting for your review"
                : counted(staged.size(), "knowledge record") + " from earlier work, waiting for your review";
            proposal_ = workspace_.validate_proposal(summary, {}, staged);
            Rlabel_set_text(id("proposal.summary"), shortened(proposal_->summary, 140).c_str());  // full text in Review
            Rwidget_set_visibility(id("proposal.bar"), R_VISIBLE);
        }
    }
}

void App::recover() {
    if (!recovery_store_.draft().empty()) {
        Rtext_editor_set_text(id("composer"), recovery_store_.draft().c_str());
        update_context_summary();
    }
    int recovered = 0;
    int conflicts = 0;
    const auto root = std::filesystem::weakly_canonical(workspace_.root());
    for (const auto& editor : recovery_store_.editors()) {
        const auto candidate = std::filesystem::weakly_canonical(editor.path);
        if (candidate != root && std::mismatch(root.begin(), root.end(), candidate.begin()).first != root.end()) {
            ++conflicts;
            continue;
        }
        if (editor.kind == "code") {
            if (!std::filesystem::is_regular_file(candidate) || read_file(candidate) != editor.saved_content) {
                ++conflicts;
                continue;
            }
            open_code_file(candidate);
            Rtext_editor_set_text(id("code.editor"), editor.content.c_str());
            current_code_modified_ = true;
            code_autosave_pending_ = true;
            code_autosave_due_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(900);
            Rlabel_set_text(id("code.path"), (std::filesystem::relative(candidate, root).string() + " •").c_str());
        } else {
            if (editor.kind == "agent") {
                const auto found = std::find_if(agents_.begin(), agents_.end(), [&](const auto& agent) { return agent.path == candidate; });
                if (found == agents_.end() || found->content != editor.saved_content) {
                    ++conflicts;
                    continue;
                }
                open_agent(static_cast<int>(found - agents_.begin()));
                Rtext_editor_set_text(id("agent.editor"), editor.content.c_str());
                current_agent_modified_ = true;
                agent_autosave_pending_ = true;
                agent_autosave_due_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(900);
                Rlabel_set_text(id("agent.path"), (std::filesystem::relative(candidate, root).string() + " •").c_str());
            } else {
                const auto found = std::find_if(notes_.begin(), notes_.end(), [&](const auto& note) { return note.path == candidate; });
                if (found == notes_.end() || found->content != editor.saved_content) {
                    ++conflicts;
                    continue;
                }
                open_note(static_cast<int>(found - notes_.begin()));
                Rtext_editor_set_text(id("note.editor"), editor.content.c_str());
                current_note_modified_ = true;
                note_autosave_pending_ = true;
                note_autosave_due_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(900);
                Rlabel_set_text(id("note.path"), (std::filesystem::relative(candidate, root).string() + " •").c_str());
            }
        }
        ++recovered;
    }
    if (recovered > 0) set_status("Recovered " + counted(static_cast<size_t>(recovered), "editor backup") + " and the current draft");
    if (conflicts > 0) set_status("Recovery preserved aside · " + counted(static_cast<size_t>(conflicts), "file") + " changed on disk");
}

void App::schedule_recovery_save() {
    recovery_save_pending_ = true;
    recovery_save_due_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(900);
}

void App::pump_recovery() {
    if (!recovery_save_pending_ || std::chrono::steady_clock::now() < recovery_save_due_) return;
    recovery_save_pending_ = false;
    recovery_store_.set_draft(Rtext_editor_get_text(id("composer")));
    if (current_code_modified_ && !current_code_path_.empty()) {
        recovery_store_.set_editor("code", current_code_path_, Rtext_editor_get_text(id("code.editor")), read_file(current_code_path_));
    }
    if (current_agent_modified_ && current_agent_ >= 0 && current_agent_ < static_cast<int>(agents_.size())) {
        recovery_store_.set_editor("agent", agents_[current_agent_].path, Rtext_editor_get_text(id("agent.editor")), agents_[current_agent_].content);
    }
    if (current_note_modified_ && current_note_ >= 0 && current_note_ < static_cast<int>(notes_.size())) {
        recovery_store_.set_editor("note", notes_[current_note_].path, Rtext_editor_get_text(id("note.editor")), notes_[current_note_].content);
    }
}

void App::reset_graph() {
    subagent_nodes_.clear();
    subagent_status_.clear();
    subagent_markers_.clear();
    subagent_kinds_.clear();
    Rwidget_clear_children(id("activity.rows"));
    Rwidget_set_visibility(id("activity.empty"), R_VISIBLE);
    update_run_usage();
    Rbutton_set_text(id("graph.toggle"), "Open mission control");
    Rlabel_set_text(id("activity.harness"), (std::string(harness_name()) + " · workspace session").c_str());
    auto graph = id("graph");
    Rnode_graph_clear(graph);
    Rnode_graph_set_read_only(graph, R_TRUE);
    Rnode_graph_set_flow_visible(graph, R_FALSE);
    Rnode_graph_set_type_color(graph, "agent", 0.91f, 0.47f, 0.29f);
    lead_node_ = Rnode_graph_add_node(graph, "Orchestrator", 70, 40);
    Rnode_graph_set_node_color(graph, lead_node_, 0.91f, 0.47f, 0.29f);
    Rnode_graph_set_node_deletable(graph, lead_node_, R_FALSE);
    auto lead_content = Rwidget_create(R_WIDGET_VBOX);
    Rpanel_set_spacing(lead_content, 4);
    Rwidget_set_min_size(lead_content, 180, 0);
    auto lead_status = Rwidget_add_child(lead_content, Rlabel_create("LEAD · READY"));
    Rlabel_set_text_size(lead_status, 11);
    auto lead_detail = Rwidget_add_child(lead_content, Rlabel_create("Awaiting delegation"));
    Rlabel_set_text_size(lead_detail, 10);
    float muted[4];
    Rwidget_get_color(Rwindow_get_content(window_), R_COLOR_TEXT_DISABLED, &muted[0], &muted[1], &muted[2], &muted[3]);
    Rlabel_set_color(lead_detail, muted[0], muted[1], muted[2], muted[3]);
    Rnode_graph_set_node_content(graph, lead_node_, lead_content);
    Rlabel_set_text(id("graph.selection"), "Select a mission to inspect its role and status");
    lead_output_ = Rnode_graph_add_output(graph, lead_node_, "delegate", "agent");
    Rnode_graph_frame_all(graph);
    update_graph_summary();
}

void App::load_demo() {
    set_run_state("DEMO · ACTIVE");
    Rlabel_set_text(id("run.title"), "Trace the authentication boundary");
    Rlabel_set_text(id("run.subtitle"), "Demonstration data · no Claude process is running");
    add_message("YOU", "Trace token expiry across the API and core repositories, then record the decision in the wiki.", true);
    add_message("ORCHESTRATOR", "I found the expiry check in the shared session validator. I’m delegating a focused review and regression check before proposing a change.");
    ClaudeEvent review;
    review.kind = EventKind::SubagentStarted;
    review.tool_id = "demo-review";
    review.name = "code-reviewer";
    review.detail = "Inspect the session boundary";
    add_subagent(review);
    ClaudeEvent tests = review;
    tests.tool_id = "demo-tests";
    tests.name = "test-runner";
    tests.detail = "Check token expiry regressions";
    add_subagent(tests);
    Rnode_graph_set_node_badge(id("graph"), subagent_nodes_["demo-tests"], "18/24");
    Rnode_graph_set_node_active(id("graph"), lead_node_, R_TRUE);
    Rnode_graph_set_flow_visible(id("graph"), R_TRUE);

    const auto& home = notes_.front();
    proposal_ = WikiProposal{
        "Demonstration proposal: add a navigation link to the workspace Home note.",
        {{"Home.md", home.content, home.content + "\n## Recent decisions\n\n- [[Session Expiry]] — refresh at the shared authentication boundary\n"}}
    };
    Rlabel_set_text(id("proposal.summary"), shortened(proposal_->summary, 140).c_str());  // full text in Review
    Rwidget_set_visibility(id("proposal.bar"), R_VISIBLE);
    queued_prompt_ = "Run the authentication regression checks.";
    update_queue();
    set_status("Demonstration workspace · no agent or wiki process is active");
}

const char* App::harness_name() const { return harness_ == Harness::KimiCode ? "Kimi Code" : "Claude Code"; }

void App::select_harness(Harness harness) { change_harness(harness == Harness::KimiCode ? 1 : 0); }

void App::select_run_mode(int index) { change_run_mode(index); }

void App::select_watch_checks(bool enabled) {
    watch_checks_ = enabled;
    Rbutton_set_text(id("verification.watch"), enabled ? "Watching" : "Watch checks");
    save_profile();
}

void App::update_composer_focus() {
    const bool glass = theme_index_ >= 14 && theme_index_ <= 18;
    const bool focused = Rwidget_is_focused(id("composer"));
    Rwidget_set_opacity(id("composer.shell"), glass && !focused ? 0.82f : 1.0f);
}

void App::select_theme(int index) {
    static const char* names[] = {
        "RavenIDE", "LedgerLight", "LedgerHighContrast", "MillerPlanet", "MannPlanet", "EdmundsPlanet",
        "Gargantua", "LedgerHighContrastLight", "MillerPlanetLight", "MannPlanetDark", "EdmundsPlanetDark", "GargantuaLight",
        "EarthPlanet", "EarthPlanetLight", "EarthPlanetGlass", "MillerPlanetGlass", "MannPlanetGlass", "EdmundsPlanetGlass", "GargantuaGlass"
    };
    static const char* labels[] = {
        "Ledger Dark", "Ledger Light", "High Contrast Dark", "Miller's Planet Dark", "Mann's Planet Light", "Edmunds' Planet Light",
        "Gargantua Dark", "High Contrast Light", "Miller's Planet Light", "Mann's Planet Dark", "Edmunds' Planet Dark", "Gargantua Light",
        "Earth Dark", "Earth Light", "Earth Glass", "Miller's Planet Glass", "Mann's Planet Glass", "Edmunds' Planet Glass", "Gargantua Glass"
    };
    if (index < 3 || index == 7 || index > 18) {
        set_status("Only Interstellar planet themes are available");
        return;
    }
    if (!Rtheme_use(names[index])) {
        set_status("Theme could not be loaded");
        return;
    }
    theme_index_ = index;
    Rwidget_set_theme(Rwindow_get_content(window_), names[index]);
    update_composer_focus();
    Rlabel_set_text(id("theme.current.label"), labels[index]);
    Rwidget_set_user_handle(id("theme.current.art"), reinterpret_cast<void*>(static_cast<intptr_t>(index + 1)));
    Rwidget_invalidate(id("theme.current.art"));
    static const char* family_labels[] = {"Earth", "Miller's Planet", "Mann's Planet", "Edmunds' Planet", "Gargantua"};
    const int family = theme_family(index);
    const int finish = theme_finish(index);
    if (Rwidget_get_visibility(id("theme.gallery")) != R_VISIBLE || theme_gallery_.family() != family) {
        theme_gallery_.set_family(family, Rwidget_get_visibility(id("theme.gallery")) == R_VISIBLE);
    }
    theme_gallery_.set_finish(finish);
    Rlabel_set_text(id("theme.gallery.family"), family_labels[family]);
    Rlabel_set_text(id("theme.gallery.position"), (std::to_string(family + 1) + " / 5").c_str());
    Rbutton_set_style(id("theme.gallery.dark"), finish == 0 ? R_BUTTON_PRIMARY : R_BUTTON_FLAT);
    Rbutton_set_style(id("theme.gallery.light"), finish == 1 ? R_BUTTON_PRIMARY : R_BUTTON_FLAT);
    Rbutton_set_style(id("theme.gallery.glass"), finish == 2 ? R_BUTTON_PRIMARY : R_BUTTON_FLAT);
    Rwidget_set_accessible_name(id("theme.carousel"), (family_labels[family] + std::string(" preview, ") + std::to_string(family + 1) + " of 5").c_str());
    Rwidget_invalidate(id("theme.carousel"));
    const bool dark = index == 3 || index == 6 || index == 9 || index == 10 || index == 12 || (index >= 14 && index <= 18);
    Rwidget_set_visibility(id("appearance.sun"), dark ? R_VISIBLE : R_COLLAPSED);
    Rwidget_set_visibility(id("appearance.moon"), dark ? R_COLLAPSED : R_VISIBLE);
    Rbutton_set_text(id("settings.appearance.toggle"), dark ? "Switch to light" : "Switch to dark");
    Rwidget_set_tooltip(id("appearance.toggle"), dark ? "Switch to light theme" : "Switch to dark theme");
    Rwidget_set_accessible_name(id("appearance.toggle"), dark ? "Switch to light theme" : "Switch to dark theme");
    const auto content = Rwindow_get_content(window_);
    const auto transcript = id("transcript");
    for (int i = 0; i < Rwidget_get_child_count(transcript); ++i) {
        auto card = Rwidget_get_child(transcript, i);
        const auto tag = Rwidget_get_user_handle(card);
        if (tag == reinterpret_cast<void*>(2)) continue;
        apply_message_style(card, tag != nullptr);
    }
    float accent[4];
    float muted[4];
    Rwidget_get_color(content, R_COLOR_ACCENT, &accent[0], &accent[1], &accent[2], &accent[3]);
    Rwidget_get_color(content, R_COLOR_TEXT_DISABLED, &muted[0], &muted[1], &muted[2], &muted[3]);
    for (const auto& [tool, marker] : subagent_markers_) {
        if (Rwidget_is_valid(marker)) Rborder_set_background(marker, accent[0], accent[1], accent[2], accent[3]);
    }
    for (const auto& [tool, kind] : subagent_kinds_) {
        if (Rwidget_is_valid(kind)) Rlabel_set_color(kind, muted[0], muted[1], muted[2], muted[3]);
    }
    for (const auto& [tool, status] : subagent_status_) {
        if (Rwidget_is_valid(status)) Rlabel_set_color(status, accent[0], accent[1], accent[2], accent[3]);
    }
    const auto graph = id("graph");
    Rnode_graph_set_type_color(graph, "agent", accent[0], accent[1], accent[2]);
    Rnode_graph_set_type_color(id("endurance.graph"), "context", accent[0], accent[1], accent[2]);
    if (Rnode_graph_get_node_count(graph) > lead_node_) {
        Rnode_graph_set_node_color(graph, lead_node_, accent[0], accent[1], accent[2]);
        Rnode_graph_set_node_active_color(graph, lead_node_, accent[0], accent[1], accent[2]);
    }
    for (const auto& [tool, node] : subagent_nodes_) {
        if (Rnode_graph_get_node_count(graph) <= node) continue;
        Rnode_graph_set_node_color(graph, node, accent[0], accent[1], accent[2]);
        Rnode_graph_set_node_active_color(graph, node, accent[0], accent[1], accent[2]);
    }
    Rwidget_invalidate(id("memory.canvas"));
    Rwidget_invalidate(id("tesseract.canvas"));
    refresh_navigation();
    save_profile();
}

void App::toggle_appearance() {
    static const int partners[] = {1, 0, 7, 8, 9, 10, 11, 2, 3, 4, 5, 6, 13, 12, 13, 8, 4, 5, 11};
    select_theme(partners[theme_index_]);
}

void App::open_theme_gallery() {
    Rwidget_set_visibility(id("run.setup"), R_COLLAPSED);
    Rwidget_set_visibility(id("recall.palette"), R_COLLAPSED);
    Rwidget_set_visibility(id("command.palette"), R_COLLAPSED);
    theme_gallery_.set_family(theme_family(theme_index_), false);
    theme_gallery_.set_finish(theme_finish(theme_index_));
    Rwidget_set_visibility(id("theme.gallery"), R_VISIBLE);
    Rwidget_focus(id("theme.carousel"));
    Rwidget_invalidate(id("theme.carousel"));
}

void App::close_theme_gallery() {
    Rwidget_set_visibility(id("theme.gallery"), R_COLLAPSED);
    Rwidget_focus(id("theme.current"));
}

void App::move_theme_gallery(int delta) {
    const int family = theme_gallery_.move(delta, !Raccessibility_get_reduce_motion());
    select_theme(theme_index(family, theme_finish(theme_index_)));
}

std::string App::current_model() const {
    return harness_ == Harness::KimiCode ? profile_store_.data.kimi_model : profile_store_.data.claude_model;
}

std::string App::current_effort() const {
    return harness_ == Harness::KimiCode ? profile_store_.data.kimi_effort : profile_store_.data.claude_effort;
}

void App::refresh_model_controls() {
    const bool kimi = harness_ == Harness::KimiCode;
    Rtext_editor_set_placeholder(id("composer"), kimi
        ? "Describe a task · / skills · @ files · paste an image…"
        : "Describe a task · @ files · paste an image…");
    const std::vector<std::string> models = kimi
        ? std::vector<std::string>{"", "kimi-code/kimi-for-coding", "kimi-code/kimi-for-coding-highspeed", "kimi-code/k3", "kimi-code/k3-256k", "kimi-code/k3.1-pro", "kimi-code/k3.1-pro-highspeed"}
        : std::vector<std::string>{"", "sonnet", "opus", "haiku"};
    const std::vector<std::string> model_labels = kimi
        ? std::vector<std::string>{"Default model", "K2.8 Preview", "K2.7 Code Highspeed", "K3", "K3-256k", "K3.1 Pro", "K3.1 Pro Highspeed · 2x usage"}
        : std::vector<std::string>{"Default model", "Sonnet", "Opus", "Haiku"};
    const std::vector<std::string> efforts = kimi
        ? std::vector<std::string>{"", "low", "high", "max"}
        : std::vector<std::string>{"", "low", "medium", "high", "xhigh", "max"};
    const std::vector<std::string> effort_labels = kimi
        ? std::vector<std::string>{"Default effort", "Low", "High", "Max"}
        : std::vector<std::string>{"Default effort", "Low", "Medium", "High", "XHigh", "Max"};
    Rcombo_box_clear_items(id("model.select"));
    for (const auto& label : model_labels) Rcombo_box_add_item(id("model.select"), label.c_str());
    Rcombo_box_clear_items(id("effort.select"));
    for (const auto& label : effort_labels) Rcombo_box_add_item(id("effort.select"), label.c_str());
    const auto model = std::find(models.begin(), models.end(), current_model());
    const auto effort = std::find(efforts.begin(), efforts.end(), current_effort());
    Rcombo_box_set_selected(id("model.select"), model == models.end() ? 0 : static_cast<int>(std::distance(models.begin(), model)));
    Rcombo_box_set_selected(id("effort.select"), effort == efforts.end() ? 0 : static_cast<int>(std::distance(efforts.begin(), effort)));
}

void App::change_model(int index) {
    if (lead_process_.running() || wiki_process_.running()) {
        refresh_model_controls();
        set_status("Finish the active run before changing model");
        return;
    }
    const std::vector<std::string> values = harness_ == Harness::KimiCode
        ? std::vector<std::string>{"", "kimi-code/kimi-for-coding", "kimi-code/kimi-for-coding-highspeed", "kimi-code/k3", "kimi-code/k3-256k", "kimi-code/k3.1-pro", "kimi-code/k3.1-pro-highspeed"}
        : std::vector<std::string>{"", "sonnet", "opus", "haiku"};
    if (index < 0 || index >= static_cast<int>(values.size())) return;
    auto& stored = harness_ == Harness::KimiCode ? profile_store_.data.kimi_model : profile_store_.data.claude_model;
    stored = values[index];
    save_profile();
    set_status(index == 0 ? std::string(harness_name()) + " default model selected" : "Model selected · " + stored);
}

void App::change_effort(int index) {
    if (lead_process_.running() || wiki_process_.running()) {
        refresh_model_controls();
        set_status("Finish the active run before changing effort");
        return;
    }
    const std::vector<std::string> values = harness_ == Harness::KimiCode
        ? std::vector<std::string>{"", "low", "high", "max"}
        : std::vector<std::string>{"", "low", "medium", "high", "xhigh", "max"};
    if (index < 0 || index >= static_cast<int>(values.size())) return;
    auto& stored = harness_ == Harness::KimiCode ? profile_store_.data.kimi_effort : profile_store_.data.claude_effort;
    stored = values[index];
    save_profile();
    set_status(index == 0 ? "Default reasoning effort selected" : "Reasoning effort selected · " + stored);
}

void App::change_harness(int index) {
    const auto selected = index == 1 ? Harness::KimiCode : Harness::ClaudeCode;
    if (selected == harness_) return;
    // A pending knowledge proposal belongs to the workspace and survives a harness change.
    if (lead_process_.running() || wiki_process_.running()) {
        Rcombo_box_set_selected(id("harness"), static_cast<int>(harness_));
        Rcombo_box_set_selected(id("settings.harness"), static_cast<int>(harness_));
        set_status("Finish the current run before switching harness");
        return;
    }
    harness_ = selected;
    Rcombo_box_set_selected(id("harness"), index);
    Rcombo_box_set_selected(id("settings.harness"), index);
    session_id_.clear();
    current_message_ = R_INVALID_ID;
    current_text_.clear();
    saw_delta_ = false;
    reset_graph();
    refresh_model_controls();
    Rlabel_set_text(id("run.subtitle"), (std::string("Orchestrator · ") + harness_name() + " · new session").c_str());
    set_run_state("READY");
    save_profile();
    set_status(std::string(harness_name()) + " selected · the next prompt starts a new session");
}

void App::change_run_mode(int index) {
    index = std::clamp(index, 0, 2);
    if (lead_process_.running() || wiki_process_.running()) {
        Rcombo_box_set_selected(id("run.mode"), static_cast<int>(run_mode_));
        set_status("Finish the active run before changing mode");
        return;
    }
    const auto previous = run_mode_;
    run_mode_ = static_cast<RunMode>(index);
    Rcombo_box_set_selected(id("run.mode"), index);
    if (previous != run_mode_) session_id_.clear();
    static const char* descriptions[] = {
        "Build mode · code changes, verification, checkpoints, and memory review",
        "Ask mode · read-only answers with no checkpoint or memory review",
        "Review mode · read-only findings with no checkpoint or memory review"
    };
    save_profile();
    set_status(descriptions[index]);
}

void App::open_settings() {
    const auto current = active_view(id("center.tabs"));
    const auto current_more = active_view(id("more.tabs"));
    if (current == 3 && current_more == 1) return;
    settings_return_tab_ = current;
    settings_return_more_tab_ = current_more;
    set_sidebar_visibility(false, false);
    Rcombo_box_set_selected(id("settings.harness"), static_cast<int>(harness_));
    Rwidget_set_enabled(id("settings.harness"), !lead_process_.running() && !wiki_process_.running());
    Rlabel_set_text(id("settings.interface.zoom"),
        ("Interface zoom · " + std::to_string(static_cast<int>(interface_zoom_ * 100 + 0.5f)) + "%").c_str());
    Rlabel_set_text(id("settings.editor.zoom"),
        ("Editor zoom · " + std::to_string(static_cast<int>(editor_zoom_ * 100 + 0.5f)) + "%").c_str());
    set_active_view(id("more.tabs"), 1);
    set_active_view(id("center.tabs"), 3);
    Rwidget_focus(id("settings.close"));
    set_status("Settings · changes apply immediately");
}

void App::close_settings() {
    if (active_view(id("center.tabs")) != 3 || active_view(id("more.tabs")) != 1) return;
    const auto destination = settings_return_tab_ >= 0 && settings_return_tab_ <= 5 ? settings_return_tab_ : 0;
    set_active_view(id("center.tabs"), destination);
    if (destination == 3) set_active_view(id("more.tabs"), settings_return_more_tab_);
    if (destination == 0) Rwidget_focus(id("composer"));
    else if (destination == 1) Rwidget_focus(id("code.editor"));
    else if (destination == 2) Rwidget_focus(id("graph"));
    else if (destination == 4) Rwidget_focus(id("proposal.diff"));
    else if (destination == 5) Rwidget_focus(id("tesseract.composer"));
    else if (destination == 3 && settings_return_more_tab_ == 0) Rwidget_focus(id("agent.editor"));
    else if (destination == 3 && settings_return_more_tab_ == 2) Rwidget_focus(id("memory.canvas"));
    else if (destination == 3 && settings_return_more_tab_ == 3) Rwidget_focus(id("mission.list"));
    set_status("Settings closed");
}

void App::toggle_settings() {
    if (active_view(id("center.tabs")) == 3 && active_view(id("more.tabs")) == 1) close_settings();
    else open_settings();
}

void App::toggle_vector_recall() {
    vector_recall_ = Rcheckbox_is_checked(id("settings.vector"));
    save_profile();
    set_status(vector_recall_ ? "Local vector recall enabled · deterministic, offline ranking" : "Local vector recall disabled · lexical matching only");
}

void App::load_profile() {
    if (Rwidget_is_valid(window_)) {
        for (const auto& [action, combination] : profile_store_.data.shortcuts) {
            RKey key;
            Ruint modifiers;
            if (parse_shortcut(combination, key, modifiers)) Rwindow_unregister_shortcut(window_, key, modifiers);
        }
    }
    profile_store_ = WorkspaceProfileStore(workspace_.root());
    profile_store_.load();
    const auto& profile = profile_store_.data;
    interface_zoom_ = profile.interface_zoom;
    editor_zoom_ = profile.editor_zoom;
    theme_index_ = profile.theme;
    harness_ = profile.harness == 1 ? Harness::KimiCode : Harness::ClaudeCode;
    run_mode_ = static_cast<RunMode>(profile.run_mode);
    left_sidebar_visible_ = profile.left_sidebar;
    right_sidebar_visible_ = profile.right_sidebar;
    vector_recall_ = profile.vector_recall;
    watch_checks_ = profile.watch_checks;
    opening_sound_ = profile.opening_sound;
    opening_seen_ = profile.opening_seen;
}

void App::apply_profile() {
    Rexpander_set_expanded(id("settings.keyboard.expander"), R_TRUE);
    Rset_ui_scale(interface_zoom_);
    int framebuffer_width = 0, framebuffer_height = 0;
    Rwindow_get_framebuffer_size(window_, &framebuffer_width, &framebuffer_height);
    const float window_scale = Rwindow_get_scale(window_);
    const float logical_width = window_scale > 0.0f ? framebuffer_width / window_scale - 3.0f : 0.0f;
    if (logical_width > 240.0f) {
        left_sidebar_share_ = 240.0f / logical_width;
        center_workspace_share_ = 1.0f - left_sidebar_share_ - (right_sidebar_visible_ ? right_sidebar_share_ : 0.0f);
        Rslot_set_fill(id("left.sidebar"), left_sidebar_share_);
        Rslot_set_fill(id("center.workspace"), center_workspace_share_);
        Rslot_set_fill(id("right.sidebar"), right_sidebar_share_);
    }
    static const char* editors[] = {"composer", "tesseract.composer", "code.editor", "agent.editor", "note.editor", "proposal.diff", "checkpoint.diff", "verification.detail"};
    for (const auto* editor : editors) Rtext_editor_set_zoom(id(editor), editor_zoom_);
    Rcombo_box_set_selected(id("harness"), static_cast<int>(harness_));
    Rcombo_box_set_selected(id("settings.harness"), static_cast<int>(harness_));
    Rcombo_box_set_selected(id("run.mode"), static_cast<int>(run_mode_));
    refresh_model_controls();
    Rcheckbox_set_checked(id("settings.vector"), vector_recall_);
    select_watch_checks(watch_checks_);
    select_theme(theme_index_);
    set_sidebar_visibility(true, left_sidebar_visible_);
    set_sidebar_visibility(false, right_sidebar_visible_);
    apply_custom_shortcuts();
    refresh_shortcut_editor();
    refresh_notifications();
    Rexpander_set_expanded(id("settings.keyboard.expander"), R_FALSE);
    update_interface_density();
}

void App::save_profile() {
    auto& profile = profile_store_.data;
    profile.interface_zoom = interface_zoom_;
    profile.editor_zoom = editor_zoom_;
    profile.theme = theme_index_;
    profile.harness = static_cast<int>(harness_);
    profile.run_mode = static_cast<int>(run_mode_);
    profile.left_sidebar = left_sidebar_visible_;
    profile.right_sidebar = right_sidebar_visible_;
    profile.vector_recall = vector_recall_;
    profile.watch_checks = watch_checks_;
    profile.opening_sound = opening_sound_;
    profile.opening_seen = opening_seen_;
    try {
        profile_store_.save();
    } catch (const std::exception& error) {
        set_status(std::string("Workspace profile could not be saved · ") + error.what());
    }
}

void App::apply_custom_shortcuts() {
    for (const auto& definition : shortcut_definitions()) {
        RKey default_key;
        Ruint default_modifiers;
        if (!parse_shortcut(definition.combination, default_key, default_modifiers)) continue;
        Rwindow_unregister_shortcut(window_, default_key, default_modifiers);
        const auto custom = profile_store_.data.shortcuts.find(definition.action);
        if (custom == profile_store_.data.shortcuts.end()) {
            Rwindow_register_shortcut(window_, default_key, default_modifiers, definition.action);
            continue;
        }
        RKey key;
        Ruint modifiers;
        if (parse_shortcut(custom->second, key, modifiers) && (modifiers & (R_MOD_CTRL | R_MOD_ALT | R_MOD_SUPER))) {
            Rwindow_register_shortcut(window_, key, modifiers, definition.action);
        } else {
            Rwindow_register_shortcut(window_, default_key, default_modifiers, definition.action);
        }
    }
}

void App::refresh_shortcut_editor() {
    const auto selected = Rcombo_box_get_selected(id("shortcut.action"));
    if (selected < 0 || selected >= static_cast<int>(shortcut_definitions().size())) return;
    const auto& definition = shortcut_definitions()[selected];
    const auto custom = profile_store_.data.shortcuts.find(definition.action);
    Rlabel_set_text(id("shortcut.current"), (custom == profile_store_.data.shortcuts.end() ? definition.combination : custom->second).c_str());
    Rbutton_set_text(id("shortcut.rebind"), "Rebind");
    capturing_shortcut_ = false;
}

void App::begin_shortcut_rebind() {
    capturing_shortcut_ = true;
    Rlabel_set_text(id("shortcut.current"), "Press a new shortcut · Esc cancels");
    Rbutton_set_text(id("shortcut.rebind"), "Listening…");
    set_status("Shortcut capture · Ctrl, Alt, or Super required");
}

void App::reset_shortcut() {
    const auto selected = Rcombo_box_get_selected(id("shortcut.action"));
    if (selected < 0 || selected >= static_cast<int>(shortcut_definitions().size())) return;
    const auto& definition = shortcut_definitions()[selected];
    const auto custom = profile_store_.data.shortcuts.find(definition.action);
    if (custom != profile_store_.data.shortcuts.end()) {
        RKey key;
        Ruint modifiers;
        if (parse_shortcut(custom->second, key, modifiers)) Rwindow_unregister_shortcut(window_, key, modifiers);
        profile_store_.data.shortcuts.erase(custom);
    }
    RKey key;
    Ruint modifiers;
    if (parse_shortcut(definition.combination, key, modifiers)) {
        Rwindow_unregister_shortcut(window_, key, modifiers);
        Rwindow_register_shortcut(window_, key, modifiers, definition.action);
    }
    save_profile();
    refresh_shortcut_editor();
    set_status("Shortcut restored · " + std::string(definition.combination));
}

bool App::capture_shortcut(const REvent* event) {
    if (!capturing_shortcut_) return false;
    if (event->key == R_KEY_ESCAPE) {
        refresh_shortcut_editor();
        set_status("Shortcut capture cancelled");
        return true;
    }
    const auto combination = event_shortcut(event);
    RKey key;
    Ruint modifiers;
    if (combination.empty() || !parse_shortcut(combination, key, modifiers)) return true;
    if (!(modifiers & (R_MOD_CTRL | R_MOD_ALT | R_MOD_SUPER))) {
        Rlabel_set_text(id("shortcut.current"), "Add Ctrl, Alt, or Super");
        return true;
    }
    const bool plain_ctrl = modifiers == R_MOD_CTRL;
    const bool editor_conflict = plain_ctrl && (key == R_KEY_A || key == R_KEY_C || key == R_KEY_V || key == R_KEY_X ||
        key == R_KEY_Z || key == R_KEY_Y || key == R_KEY_F || key == R_KEY_W || key == R_KEY_Q || key == R_KEY_EQUAL || key == R_KEY_MINUS);
    if (editor_conflict) {
        Rlabel_set_text(id("shortcut.current"), (combination + " is reserved by the editor").c_str());
        return true;
    }
    const auto selected = Rcombo_box_get_selected(id("shortcut.action"));
    if (selected < 0 || selected >= static_cast<int>(shortcut_definitions().size())) return true;
    const auto& definition = shortcut_definitions()[selected];
    for (const auto& other : shortcut_definitions()) {
        if (std::string(other.action) == definition.action) continue;
        const auto custom = profile_store_.data.shortcuts.find(other.action);
        const auto active = custom == profile_store_.data.shortcuts.end() ? other.combination : custom->second;
        if (active == combination) {
            Rlabel_set_text(id("shortcut.current"), (combination + " is already assigned").c_str());
            return true;
        }
    }
    const auto previous = profile_store_.data.shortcuts.find(definition.action);
    const auto old_combination = previous == profile_store_.data.shortcuts.end() ? definition.combination : previous->second;
    RKey old_key;
    Ruint old_modifiers;
    if (parse_shortcut(old_combination, old_key, old_modifiers)) Rwindow_unregister_shortcut(window_, old_key, old_modifiers);
    Rwindow_register_shortcut(window_, key, modifiers, definition.action);
    profile_store_.data.shortcuts[definition.action] = combination;
    save_profile();
    refresh_shortcut_editor();
    set_status("Shortcut saved · " + combination);
    return true;
}

bool App::suppress_replaced_shortcut(const REvent* event) const {
    const auto combination = event_shortcut(event);
    if (combination.empty()) return false;
    for (const auto& definition : shortcut_definitions()) {
        const auto custom = profile_store_.data.shortcuts.find(definition.action);
        if (custom != profile_store_.data.shortcuts.end() && custom->second != definition.combination && combination == definition.combination) return true;
    }
    return false;
}

int App::active_view(Rid view) const {
    return Rwidget_get_type(view) == R_WIDGET_SWITCHER ? Rswitcher_get_index(view) : Rtab_view_get_active(view);
}

void App::set_active_view(Rid view, int index) {
    if (Rwidget_get_type(view) == R_WIDGET_SWITCHER) Rswitcher_set_index(view, index);
    else Rtab_view_set_active(view, index);
    refresh_navigation();
}

void App::refresh_navigation() {
    if (!Ris_running() || !Rwindow_is_valid(window_) || !Rwidget_is_valid(id("nav.chat"))) return;
    const int center = active_view(id("center.tabs"));
    const int more = active_view(id("more.tabs"));
    const int context = active_view(id("right.tabs"));
    const bool knowledge_context = right_sidebar_visible_ && (context == 1 || context == 2);
    std::string selected = "chat";
    if (center == 5) selected = "canvas";
    else if (center == 2) selected = "workflows";
    else if (center == 4) selected = "review";
    else if (center == 1) selected = "files";
    if (center == 3) {
        if (more == 0) selected = "agents";
        else if (more == 1) selected = "settings";
        else if (more == 2) selected = "knowledge";
        else if (more == 3) selected = "worktrees";
    }
    const bool resource_has_priority = center == 1 || (center == 3 && (more == 0 || more == 1));
    if (knowledge_context && !resource_has_priority) selected = "knowledge";

    const auto content = Rwindow_get_content(window_);
    float selection[4], hovered[4], pressed[4], text[4];
    Rwidget_get_color(content, R_COLOR_SELECTION, &selection[0], &selection[1], &selection[2], &selection[3]);
    Rwidget_get_color(content, R_COLOR_ROW_HOVERED, &hovered[0], &hovered[1], &hovered[2], &hovered[3]);
    Rwidget_get_color(content, R_COLOR_CONTROL_PRESSED, &pressed[0], &pressed[1], &pressed[2], &pressed[3]);
    Rwidget_get_color(content, R_COLOR_TEXT, &text[0], &text[1], &text[2], &text[3]);
    static const char* names[] = {"chat", "canvas", "workflows", "files", "agents", "review", "knowledge", "worktrees", "settings"};
    for (const auto* name : names) {
        const auto button = id((std::string("nav.") + name).c_str());
        const bool active = selected == name;
        Rwidget_set_color(button, R_COLOR_CONTROL, active ? selection[0] : 0.0f, active ? selection[1] : 0.0f,
                          active ? selection[2] : 0.0f, active ? selection[3] : 0.0f);
        Rwidget_set_color(button, R_COLOR_CONTROL_HOVERED, hovered[0], hovered[1], hovered[2], hovered[3]);
        Rwidget_set_color(button, R_COLOR_CONTROL_PRESSED, pressed[0], pressed[1], pressed[2], pressed[3]);
        Rwidget_set_color(button, R_COLOR_CONTROL_BORDER, 0.0f, 0.0f, 0.0f, 0.0f);
        Rwidget_set_color(button, R_COLOR_TEXT, text[0], text[1], text[2], text[3]);
        Rwidget_set_metric(button, R_METRIC_BORDER_WIDTH, 0.0f);
        Rwidget_set_metric(button, R_METRIC_CORNER_RADIUS, 8.0f);
        Rwidget_set_metric(button, R_METRIC_PADDING_X, 12.0f);
    }
    const bool chats_visible = center == 0;
    Rwidget_set_visibility(id("left.tabs"), chats_visible ? R_VISIBLE : R_COLLAPSED);
    Rwidget_set_visibility(id("navigation.bottom.spacer"), chats_visible ? R_COLLAPSED : R_VISIBLE);
    Rwidget_set_visibility(id("navigation.search.row"), chats_visible ? R_VISIBLE : R_COLLAPSED);
    Rwidget_set_visibility(id("navigation.list.header"), chats_visible ? R_VISIBLE : R_COLLAPSED);
    Rwidget_set_visibility(id("threads.view"), chats_visible ? R_VISIBLE : R_COLLAPSED);
    Rwidget_set_visibility(id("threads.archive.slot"), chats_visible ? R_VISIBLE : R_COLLAPSED);
    Rwidget_set_visibility(id("agents.new"), R_COLLAPSED);
    Rwidget_set_visibility(id("notes.new"), R_COLLAPSED);
    Rlabel_set_text(id("threads.section"), show_archived_ ? "Archived chats" : "Recent chats");
    Rtext_input_set_placeholder(id("navigation.search"), "Search chats…");

    if (selected != status_destination_) {
        const bool first = status_destination_.empty();
        status_destination_ = selected;
        if (!first && !lead_process_.running() && !wiki_process_.running()) {
            static const std::pair<const char*, const char*> defaults[] = {
                {"workflows", "Workflows · build, run, and review multi-agent missions"},
                {"files", "Files · open a file from the workspace tree"},
                {"agents", "Agents · edit shared agent definitions"},
                {"review", "Review · code changes and knowledge proposals"},
                {"knowledge", "Knowledge · reviewed entities, relations, and memory"},
                {"worktrees", "Worktrees · isolated Git tasks you apply after review"},
                {"settings", "Settings · changes apply immediately"},
                {"canvas", "Tesseract · describe an interface and watch it take shape"}
            };
            std::string text = workspace_summary();
            for (const auto& [name, message] : defaults) {
                if (selected == name) text = message;
            }
            set_status(text);
        }
    }
}

void App::show_context(int index) {
    static const char* titles[] = {"Activity", "Notes", "Knowledge", "Alerts", "Workflow step"};
    static const char* subtitles[] = {
        "Live agent work and run details",
        "Workspace Markdown and backlinks",
        "Reviewed entities, relations, and memory",
        "Run results and workspace events",
        "Edit the selected step"
    };
    const int selected = std::clamp(index, 0, 4);
    set_active_view(id("right.tabs"), selected);
    Rlabel_set_text(id("context.title"), titles[selected]);
    Rlabel_set_text(id("context.subtitle"), subtitles[selected]);
    Rwidget_set_visibility(id("context.alerts"), selected == 3 ? R_COLLAPSED : R_VISIBLE);
    set_sidebar_visibility(false, true);
    refresh_navigation();
}

void App::set_sidebar_visibility(bool left, bool visible) {
    auto& current = left ? left_sidebar_visible_ : right_sidebar_visible_;
    auto& target_share = left ? left_sidebar_share_ : right_sidebar_share_;
    const auto target = id(left ? "left.sidebar" : "right.sidebar");
    const auto expected = visible ? R_VISIBLE : R_COLLAPSED;
    if (current == visible && Rwidget_get_visibility(target) == expected) return;

    if (!visible) {
        if (left_sidebar_visible_) left_sidebar_share_ = Rslot_get_fill(id("left.sidebar"));
        center_workspace_share_ = Rslot_get_fill(id("center.workspace"));
        if (right_sidebar_visible_) right_sidebar_share_ = Rslot_get_fill(id("right.sidebar"));
        center_workspace_share_ += target_share;
        Rslot_set_fill(id("center.workspace"), center_workspace_share_);
        const bool other_visible = left ? right_sidebar_visible_ : left_sidebar_visible_;
        if (other_visible) Rslot_set_fill(id(left ? "right.sidebar" : "left.sidebar"), left ? right_sidebar_share_ : left_sidebar_share_);
    } else {
        const bool other_visible = left ? right_sidebar_visible_ : left_sidebar_visible_;
        const float other_share = other_visible ? (left ? right_sidebar_share_ : left_sidebar_share_) : 0.0f;
        const float share = std::clamp(target_share, 0.0f, std::max(0.0f, 1.0f - other_share - 0.30f));
        center_workspace_share_ = 1.0f - other_share - share;
        Rslot_set_fill(id("center.workspace"), center_workspace_share_);
        if (other_visible) Rslot_set_fill(id(left ? "right.sidebar" : "left.sidebar"), other_share);
        Rslot_set_fill(target, share);
    }

    current = visible;
    Rwidget_set_visibility(target, expected);
    refresh_navigation();
}

void App::toggle_sidebar(bool left) {
    auto& visible = left ? left_sidebar_visible_ : right_sidebar_visible_;
    set_sidebar_visibility(left, !visible);
    save_profile();
    set_status(std::string(left ? "Navigation" : "Context panel") + (visible ? " shown" : " folded"));
}

void App::update_interface_density() {
    const bool compact = interface_zoom_ > 1.5f;
    if (compact != compact_interface_) {
        if (compact) {
            compact_restore_left_ = left_sidebar_visible_;
            compact_restore_right_ = right_sidebar_visible_;
            set_sidebar_visibility(true, false);
            set_sidebar_visibility(false, false);
        } else {
            set_sidebar_visibility(true, compact_restore_left_);
            set_sidebar_visibility(false, compact_restore_right_);
        }
        compact_interface_ = compact;
    }
    const auto context_visibility = compact ? R_COLLAPSED : R_VISIBLE;
    Rwidget_set_visibility(id("workspace.name"), context_visibility);
    Rwidget_set_visibility(id("workspace.separator"), context_visibility);
    Rwidget_set_visibility(id("run.title"), context_visibility);
    Rwidget_set_min_size(id("theme.carousel"), -1, compact ? 150 : 240);
    Rborder_set_padding(id("theme.gallery"), compact ? 18 : 34, compact ? 14 : 26,
                        compact ? 18 : 34, compact ? 14 : 26);
    Rwidget_set_size(id("command.palette"), compact ? 500 : 640, -1);
    Rwidget_set_size(id("palette.results"), -1, compact ? 170 : 360);
    Rwidget_set_size(id("recall.palette"), compact ? 500 : 680, -1);
    Rwidget_set_size(id("recall.results"), -1, compact ? 170 : 300);
}

Rid App::add_message(const std::string& role, const std::string& text, bool user) {
    auto card = Rwidget_create(R_WIDGET_BORDER);
    Rwidget_set_user_handle(card, reinterpret_cast<void*>(user ? 1 : 0));
    Rborder_set_padding(card, user ? 14 : 4, user ? 11 : 0, user ? 14 : 4, user ? 11 : 0);
    auto box = Rwidget_add_child(card, Rwidget_create(R_WIDGET_VBOX));
    Rpanel_set_spacing(box, 7);
    // Consecutive lead messages from the same role share one heading.
    bool repeated_role = false;
    if (!user) {
        const auto transcript = id("transcript");
        const int count = Rwidget_get_child_count(transcript);
        const auto previous = count > 0 ? Rwidget_get_child(transcript, count - 1) : R_INVALID_ID;
        if (Rwidget_is_valid(previous) && Rwidget_get_type(previous) == R_WIDGET_BORDER &&
            Rwidget_get_user_handle(previous) == nullptr) {
            const auto previous_box = Rwidget_get_child(previous, 0);
            const auto previous_heading = Rwidget_is_valid(previous_box) ? Rwidget_get_child(previous_box, 0) : R_INVALID_ID;
            const char* previous_role = Rwidget_is_valid(previous_heading) ? Rlabel_get_text(previous_heading) : nullptr;
            repeated_role = previous_role && role == previous_role;
        }
    }
    auto heading = Rwidget_add_child(box, Rlabel_create(role.c_str()));
    Rlabel_set_text_size(heading, 10);
    Rwidget_set_visibility(heading, user || repeated_role ? R_COLLAPSED : R_VISIBLE);
    auto body = Rwidget_add_child(box, Rlabel_create(text.c_str()));
    Rlabel_set_text_size(body, 14);
    apply_message_style(card, user);
    Rlabel_set_markdown(body, R_TRUE);
    Rlabel_set_link_callback(body, wiki_link, this);
    Rlabel_set_wrap(body, R_TRUE);
    Rwidget_set_transition(card, R_TRANSITION_FADE, 0.0f);
    Rwidget_add_child(id("transcript"), card);
    Rscroll_box_scroll_to_end(id("transcript.scroll"));
    return body;
}

void App::show_pending(const std::string& role, const std::string& text) {
    if (!Rwidget_is_valid(pending_message_)) {
        pending_message_ = add_message(role, text);
        return;
    }
    Rlabel_set_text(pending_message_, text.c_str());
    const auto box = Rwidget_get_parent(pending_message_);
    const auto heading = Rwidget_get_child(box, 0);
    if (Rwidget_is_valid(heading)) Rlabel_set_text(heading, role.c_str());
}

void App::dismiss_pending() {
    if (!Rwidget_is_valid(pending_message_)) return;
    const auto box = Rwidget_get_parent(pending_message_);
    const auto card = Rwidget_get_parent(box);
    if (Rwidget_is_valid(card)) Rwidget_destroy(card);
    pending_message_ = R_INVALID_ID;
}

void App::apply_message_style(Rid card, bool user) {
    float background[4];
    float outline[4];
    float role[4];
    const auto content = Rwindow_get_content(window_);
    Rwidget_get_color(content, user ? R_COLOR_CONTROL : R_COLOR_WINDOW, &background[0], &background[1], &background[2], &background[3]);
    Rwidget_get_color(content, R_COLOR_BORDER, &outline[0], &outline[1], &outline[2], &outline[3]);
    Rwidget_get_color(content, user ? R_COLOR_LINK : R_COLOR_ACCENT, &role[0], &role[1], &role[2], &role[3]);
    Rwidget_set_material(card, user ? R_MATERIAL_DEFAULT : R_MATERIAL_SOLID);
    Rborder_set_background(card, background[0], background[1], background[2], user ? background[3] : 0.0f);
    Rborder_set_outline(card, user ? 1 : 0, outline[0], outline[1], outline[2], outline[3]);
    Rborder_set_corner_radius(card, user ? 12 : 0, user ? 12 : 0, user ? 12 : 0, user ? 12 : 0);
    Rwidget_set_max_size(card, user ? 680 : -1, -1);
    Rslot_set_halign(card, user ? R_HALIGN_RIGHT : R_HALIGN_FILL);
    const auto box = Rwidget_get_child(card, 0);
    if (Rwidget_is_valid(box)) {
        const auto heading = Rwidget_get_child(box, 0);
        if (Rwidget_is_valid(heading)) Rlabel_set_color(heading, role[0], role[1], role[2], role[3]);
    }
}

void App::add_tool_activity(const ClaudeEvent& event) {
    if (!Rwidget_is_valid(activity_expander_)) {
        activity_expander_ = Rwidget_add_child(id("transcript"), Rwidget_create(R_WIDGET_EXPANDER));
        Rwidget_set_user_handle(activity_expander_, reinterpret_cast<void*>(2));
        Rwidget_set_accessible_name(activity_expander_, "Work activity");
        Rwidget_set_accessible_description(activity_expander_, "Collapsed tool activity for this run. Expand to inspect individual steps.");
        Rwidget_set_tooltip(activity_expander_, "Expand to inspect the tools used in this run");
        activity_rows_ = Rwidget_add_child(activity_expander_, Rwidget_create(R_WIDGET_VBOX));
        Rexpander_set_expanded(activity_expander_, R_FALSE);
        Rpanel_set_spacing(activity_rows_, 7);
        Rslot_set_padding(activity_rows_, 36, 2, 4, 10);
        Rscroll_box_scroll_to_end(id("transcript.scroll"));
    }
    auto detail = event.detail;
    const auto root = workspace_.root().string();
    if (detail.rfind(root + "/", 0) == 0) detail.erase(0, root.size() + 1);
    std::replace(detail.begin(), detail.end(), '\n', ' ');
    if (const auto kind = verification_command_kind(detail); !kind.empty()) {
        const auto check = std::find_if(active_checks_.begin(), active_checks_.end(), [&](const auto& item) {
            return item.id == event.tool_id;
        });
        if (check == active_checks_.end()) {
            active_checks_.push_back({event.tool_id, detail, kind, VerificationStatus::Running, ""});
        } else {
            check->command = detail;
            check->kind = kind;
        }
    }
    const auto existing = tool_activity_status_.find(event.tool_id);
    if (!event.tool_id.empty() && existing != tool_activity_status_.end()) {
        if (detail.empty() || !Rwidget_is_valid(existing->second)) return;
        const auto row = Rwidget_get_parent(existing->second);
        const auto text = Rwidget_get_child(row, 1);
        const auto action = Rlabel_get_text(Rwidget_get_child(text, 0));
        const auto target = Rwidget_get_child(text, 1);
        const auto display = shortened(detail, 180);
        const bool linked = editor_reference(workspace_.root(), display).has_value();
        Rlabel_set_markdown(target, linked ? R_TRUE : R_FALSE);
        Rlabel_set_text(target, activity_detail(workspace_.root(), display).c_str());
        if (linked) Rwidget_set_tooltip(target, "Open in Editor");
        const auto current = std::string(action) + " · " + shortened(detail, 56);
        update_activity_summary(current);
        set_current_action(current);
        return;
    }

    const auto action = tool_action(event);
    auto row = Rwidget_add_child(activity_rows_, Rwidget_create(R_WIDGET_HBOX));
    Rpanel_set_spacing(row, 10);
    Rslot_set_padding(row, 0, 2, 0, 2);
    auto icon = Rwidget_add_child(row, Rwidget_create(R_WIDGET_IMAGE));
    Rwidget_set_size(icon, 16, 16);
    Rslot_set_valign(icon, R_VALIGN_CENTER);
    Rwidget_set_accessible_hidden(icon, R_TRUE);
    Rimage_set_texture(icon, Rtexture_create_from_file(tool_icon(event).c_str()));
    Rimage_set_tint_text(icon, R_TRUE);
    auto text = Rwidget_add_child(row, Rwidget_create(R_WIDGET_VBOX));
    Rslot_set_fill(text, 1);
    Rpanel_set_spacing(text, 3);
    auto label = Rwidget_add_child(text, Rlabel_create(action.c_str()));
    Rlabel_set_text_size(label, 12);
    const auto display = detail.empty() ? "Preparing tool input…" : shortened(detail, 180);
    const bool linked = !detail.empty() && editor_reference(workspace_.root(), display).has_value();
    auto target = Rwidget_add_child(text, Rlabel_create(activity_detail(workspace_.root(), display).c_str()));
    Rlabel_set_text_size(target, 11);
    Rlabel_set_wrap(target, R_TRUE);
    Rlabel_set_markdown(target, linked ? R_TRUE : R_FALSE);
    Rlabel_set_link_callback(target, wiki_link, this);
    if (linked) Rwidget_set_tooltip(target, "Open in Editor");
    auto status = Rwidget_add_child(row, Rlabel_create("running"));
    Rlabel_set_text_size(status, 10);
    Rwidget_set_min_size(status, 42, 0);
    Rlabel_set_text_align(status, R_TEXT_ALIGN_RIGHT);
    Rslot_set_valign(status, R_VALIGN_CENTER);
    if (!event.tool_id.empty()) tool_activity_status_[event.tool_id] = status;
    ++activity_count_;
    update_activity_summary(action + (detail.empty() ? "" : " · " + shortened(detail, 56)));
}

void App::update_activity_summary(const std::string& current) {
    if (!Rwidget_is_valid(activity_expander_)) return;
    const auto steps = std::to_string(activity_count_) + (activity_count_ == 1 ? " step" : " steps");
    const auto title = current.empty() ? "Working · " + steps : "Working · " + current;
    Rexpander_set_title(activity_expander_, title.c_str());
}

void App::finish_activity(bool error) {
    if (!Rwidget_is_valid(activity_expander_)) return;
    for (auto& [tool, status] : tool_activity_status_) {
        if (Rwidget_is_valid(status) && std::string(Rlabel_get_text(status)) == "running") {
            Rlabel_set_text(status, error ? "failed" : "done");
        }
    }
    const auto steps = std::to_string(activity_count_) + (activity_count_ == 1 ? " step" : " steps");
    const auto title = (error ? "Run failed · " : "Work activity · ") + steps +
        (activity_failed_ > 0 ? " · " + std::to_string(activity_failed_) + " failed" : "");
    Rexpander_set_title(activity_expander_, title.c_str());
}

void App::update_queue() {
    const bool lead_running = lead_process_.running();
    const bool wiki_running = wiki_process_.running();
    const bool busy = lead_running || wiki_running;
    Rwidget_set_visibility(id("queue.bar"), queued_prompt_.empty() ? R_COLLAPSED : R_VISIBLE);
    Rwidget_set_visibility(id("queue.review"), R_COLLAPSED);
    Rwidget_set_visibility(id("tesseract.queue.bar"), queued_tesseract_ && !queued_prompt_.empty() ? R_VISIBLE : R_COLLAPSED);
    if (!queued_prompt_.empty()) {
        Rlabel_set_text(id("queue.prompt"), shortened(queued_prompt_, 180).c_str());
        Rlabel_set_text(id("tesseract.queue.prompt"), shortened(queued_prompt_, 100).c_str());
        std::string state = "Follow-up queued";
        if (last_run_failed_) state = "Previous run failed · follow-up paused";
        else if (wiki_running) state = "Knowledge review in progress · your follow-up is saved";
        else if (lead_running) state = run_mode_ == RunMode::Build
            ? "Waiting for the current run and knowledge review"
            : "Waiting for the current run";
        Rlabel_set_text(id("queue.state"), state.c_str());
        Rlabel_set_text(id("tesseract.queue.state"), state.c_str());
    }
    Rwidget_set_enabled(id("run.send"), R_TRUE);
    Rwidget_set_tooltip(id("run.send"), busy ? "Queue a follow-up (Enter)" : "Send to lead (Enter)");
    Rwidget_set_accessible_name(id("run.send"), busy ? "Queue a follow-up" : "Send to lead");
    Rwidget_set_enabled(id("tesseract.send"), R_TRUE);
    Rwidget_set_tooltip(id("tesseract.send"), busy ? "Queue a UI refinement (Enter)" : "Create with agent (Enter)");
    Rwidget_set_accessible_name(id("tesseract.send"), busy ? "Queue a UI refinement" : "Create with agent");
    refresh_tesseract_chrome();
}

void App::cancel_queued() {
    if (queued_prompt_.empty()) return;
    queued_prompt_.clear();
    queued_image_.reset();
    queued_tesseract_context_.clear();
    update_queue();
    set_active_view(id("center.tabs"), queued_tesseract_ ? 5 : 0);
    Rwidget_focus(id(queued_tesseract_ ? "tesseract.composer" : "composer"));
    set_status("Queued follow-up cancelled");
}

void App::maybe_start_queued() {
    if (queued_prompt_.empty() || lead_process_.running() || wiki_process_.running() || last_run_failed_) return;
    const auto prompt = queued_prompt_;
    tesseract_context_override_ = queued_tesseract_context_;
    queued_prompt_.clear();
    queued_tesseract_context_.clear();
    update_queue();
    set_active_view(id("center.tabs"), queued_tesseract_ ? 5 : 0);
    Rtext_editor_set_text(id(queued_tesseract_ ? "tesseract.composer" : "composer"), prompt.c_str());
    image_for(queued_tesseract_) = queued_image_;
    queued_image_.reset();
    refresh_image_attachment(queued_tesseract_);
    start_run();
}

void App::retry_run() {
    if (lead_process_.running() || wiki_process_.running()) {
        set_status("Finish the active run before retrying");
        return;
    }
    if (last_prompt_.empty()) {
        set_status("There is no previous prompt to retry");
        return;
    }
    tesseract_context_override_ = run_tesseract_context_;
    set_active_view(id("center.tabs"), run_tesseract_ ? 5 : 0);
    Rtext_editor_set_text(id(run_tesseract_ ? "tesseract.composer" : "composer"), last_prompt_.c_str());
    image_for(run_tesseract_) = last_image_;
    refresh_image_attachment(run_tesseract_);
    start_run();
}

void App::set_current_action(const std::string& action) {
    current_action_ = action;
    displayed_elapsed_seconds_ = -1;
    update_run_timer();
}

void App::update_run_timer() {
    const bool running = lead_process_.running() || wiki_process_.running();
    if (running) {
        run_elapsed_seconds_ = std::chrono::duration<double>(std::chrono::steady_clock::now() - run_started_).count();
    }
    const auto seconds = static_cast<int>(run_elapsed_seconds_);
    if (seconds == displayed_elapsed_seconds_) return;
    displayed_elapsed_seconds_ = seconds;
    const auto two = [](int value) { return (value < 10 ? "0" : "") + std::to_string(value); };
    const auto minutes = seconds / 60;
    const auto duration = minutes >= 60
        ? std::to_string(minutes / 60) + ":" + two((minutes % 60)) + ":" + two(seconds % 60)
        : two(minutes) + ":" + two(seconds % 60);
    Rlabel_set_text(id("run.timing"), duration.c_str());
    Rlabel_set_text(id("tesseract.activity.time"), duration.c_str());
    Rwidget_set_visibility(id("run.timing"), seconds > 0 || running ? R_VISIBLE : R_COLLAPSED);
    Rlabel_set_text(id("activity.action"), (current_action_ + (seconds > 0 || running ? " · " + duration : "")).c_str());
    refresh_tesseract_chrome();
}

bool App::has_run_timers() const {
    return active_view(id("center.tabs")) == 5 || lead_process_.running() || wiki_process_.running() || endurance_.busy() || endurance_yaml_pending_ || endurance_graph_sync_pending_ ||
        code_autosave_pending_ || agent_autosave_pending_ || note_autosave_pending_ || palette_search_pending_ ||
        verification_rerun_running_ || watch_due_pending_ || recovery_save_pending_ ||
        std::any_of(endurance_workflows_.begin(), endurance_workflows_.end(), [](const auto& workflow) { return workflow.recurrence.enabled; });
}

void App::notify_event(const std::string& title, const std::string& body, bool error,
                       const std::string& kind, const std::string& action_name, const std::string& target) {
    try {
        notification_store_.add({"", title, body, kind, action_name, target, 0, error});
        refresh_notifications();
    } catch (const std::exception& exception) {
        set_status(std::string("Notification history could not be saved · ") + exception.what());
    }
}

void App::refresh_notifications() {
    const auto previous = Rlist_view_get_selected(id("alerts.list"));
    const auto previous_id = previous >= 0 && previous < static_cast<int>(notification_rows_.size()) ? notification_rows_[previous] : std::string{};
    notification_rows_.clear();
    Rlist_view_clear_items(id("alerts.list"));
    int selected = -1;
    const auto& entries = notification_store_.entries();
    for (auto entry = entries.rbegin(); entry != entries.rend(); ++entry) {
        if (entry->id == previous_id) selected = static_cast<int>(notification_rows_.size());
        notification_rows_.push_back(entry->id);
        // "Stellar · " repeats on every event and the kind repeats the title; rows show what happened
        // and when, and the body appears in the detail below once selected.
        auto title = entry->title;
        if (title.rfind("Stellar · ", 0) == 0) title.erase(0, std::string("Stellar · ").size());
        const auto age = relative_age(entry->created_at);
        Rlist_view_add_item(id("alerts.list"), (shortened(title, 34) + (age.empty() ? "" : " · " + age)).c_str());
    }
    Rwidget_set_enabled(id("alerts.clear"), !entries.empty());
    if (selected >= 0) Rlist_view_set_selected(id("alerts.list"), selected);
    else {
        Rlist_view_set_selected(id("alerts.list"), -1);
        Rwidget_set_enabled(id("alerts.open"), false);
        Rlabel_set_text(id("alerts.detail"), entries.empty() ? "No run or workflow events yet." :
            "Select an event to inspect it and open its destination.");
    }
}

void App::select_notification(int index) {
    if (index < 0 || index >= static_cast<int>(notification_rows_.size())) return;
    const auto entry = notification_store_.find(notification_rows_[index]);
    if (!entry) return;
    const auto detail = entry->title + "\n\n" + entry->kind + (entry->error ? " · failed" : " · recorded") + "\n\n" + entry->body;
    Rlabel_set_text(id("alerts.detail"), detail.c_str());
    Rwidget_set_enabled(id("alerts.open"), !entry->action.empty());
}

void App::open_notification() {
    const auto index = Rlist_view_get_selected(id("alerts.list"));
    if (index < 0 || index >= static_cast<int>(notification_rows_.size())) return;
    const auto entry = notification_store_.find(notification_rows_[index]);
    if (!entry || entry->action.empty()) return;
    if (entry->action == "verification" && !entry->target.empty()) {
        open_checkpoint(entry->target);
        open_verification(0);
    } else if (entry->action == "proposal") open_proposal();
    else if (entry->action == "endurance" && !entry->target.empty()) {
        set_active_view(id("center.tabs"), 2);
        set_active_view(id("endurance.views"), 2);
        refresh_endurance_runs();
        const auto found = std::find(endurance_run_rows_.begin(), endurance_run_rows_.end(), entry->target);
        if (found != endurance_run_rows_.end()) open_endurance_run(static_cast<int>(found - endurance_run_rows_.begin()));
    }
    else if (entry->action == "mission" && !entry->target.empty()) review_mission(entry->target);
    else if (entry->action == "editor") set_active_view(id("center.tabs"), 1);
    else set_active_view(id("center.tabs"), 0);
    set_status("Opened notification · " + entry->title);
}

void App::clear_notifications() {
    notification_store_.clear();
    refresh_notifications();
    set_status("Notification history cleared");
}

void App::notify_autosave_error(const std::string& error) {
    const auto now = std::chrono::steady_clock::now();
    if (now - last_autosave_notification_ < std::chrono::minutes(1)) return;
    last_autosave_notification_ = now;
    notify_event("Stellar · Autosave failed", error, true, "autosave", "editor");
}

std::string App::run_usage_summary() const {
    if (!run_usage_reported_) return "Run cost · not reported by " + std::string(harness_name());
    std::string summary;
    if (run_cost_usd_ >= 0.0) {
        std::ostringstream cost;
        cost << "$" << std::fixed << std::setprecision(run_cost_usd_ < 1.0 ? 4 : 2) << run_cost_usd_;
        summary = "Run cost · " + cost.str();
    } else {
        summary = "Run cost · unavailable";
    }
    if (run_input_tokens_ > 0 || run_output_tokens_ > 0) {
        summary += " · " + std::to_string(run_input_tokens_) + " in / " + std::to_string(run_output_tokens_) + " out";
    }
    return summary;
}

void App::update_run_usage(const ClaudeEvent* event, bool accumulate) {
    if (!event) {
        run_cost_usd_ = -1.0;
        run_input_tokens_ = 0;
        run_output_tokens_ = 0;
        run_usage_reported_ = false;
        active_run_notification_id_.clear();
        Rlabel_set_text(id("run.usage"), "Run cost · no runs yet");
        return;
    }
    if (event->has_usage) {
        if (accumulate) {
            if (event->cost_usd >= 0.0) run_cost_usd_ = run_cost_usd_ >= 0.0 ? run_cost_usd_ + event->cost_usd : event->cost_usd;
            run_input_tokens_ += event->input_tokens;
            run_output_tokens_ += event->output_tokens;
        } else {
            run_cost_usd_ = event->cost_usd;
            run_input_tokens_ = event->input_tokens;
            run_output_tokens_ = event->output_tokens;
        }
        run_usage_reported_ = true;
    }
    Rlabel_set_text(id("run.usage"), run_usage_summary().c_str());
}

void App::notify_run_finished(bool error) {
    const auto title = error ? "Stellar · Run failed" : "Stellar · Run complete";
    auto body = run_title_.empty() ? (error ? "The agent run failed" : "The agent run completed") : run_title_;
    body += "\n" + run_usage_summary();
    notify_event(title, body, error, "run", "agent");
    if (!notification_store_.entries().empty()) active_run_notification_id_ = notification_store_.entries().back().id;
}

void App::start_with_prompt(const std::string& prompt) {
    dismiss_splash();
    set_active_view(id("center.tabs"), 0);
    Rtext_editor_set_text(id("composer"), prompt.c_str());
    start_run();
}

void App::start_run() {
    struct ContextOverrideGuard {
        std::optional<std::string>& value;
        ~ContextOverrideGuard() { value.reset(); }
    } context_override_guard{tesseract_context_override_};
    const bool tesseract = active_view(id("center.tabs")) == 5;
    if (tesseract) finish_tesseract_text_edit(true);
    const Rid active_composer = id(tesseract ? "tesseract.composer" : "composer");
    auto& attachment = image_for(tesseract);
    std::string prompt = Rtext_editor_get_text(active_composer);
    if (prompt.find_first_not_of(" \t\r\n") == std::string::npos) {
        if (attachment) {
            prompt = tesseract ? "Create an interface using the attached reference image." : "Inspect the attached image and describe what you see.";
        } else {
            set_status(tesseract ? "Describe the interface you want the agent to create" : "Write a task or attach an image first");
            Rwidget_focus(active_composer);
            return;
        }
    }
    Rwidget_set_visibility(id("run.setup"), R_COLLAPSED);
    if (tesseract && run_mode_ != RunMode::Build) {
        set_status("Tesseract requires Build mode to create and edit its document");
        return;
    }
    if (tesseract && prompt.front() == '/') {
        set_status("Slash commands belong to Agent · describe the UI in plain language for Tesseract");
        return;
    }
    if (prompt.front() == '/' && run_mode_ != RunMode::Build) {
        set_status("Slash commands require Build mode");
        return;
    }
    if (lead_process_.running() || wiki_process_.running()) {
        if (!queued_prompt_.empty()) {
            set_status("A follow-up is already queued · cancel it before queueing another");
            return;
        }
        queued_prompt_ = prompt;
        queued_image_ = attachment;
        queued_tesseract_ = tesseract;
        queued_tesseract_context_ = tesseract ? tesseract_.selection_context() : "";
        Rtext_editor_set_text(active_composer, "");
        attachment.reset();
        refresh_image_attachment(tesseract);
        update_context_summary();
        Rwindow_close_popups(window_);
        update_queue();
        set_status(wiki_process_.running()
            ? "Follow-up saved · knowledge review is still in progress"
            : "Follow-up queued · waiting for the current run");
        return;
    }
    // A pending knowledge proposal stays available in Activity and Review; the next run's
    // curator folds its own findings into it instead of the proposal blocking new work.
    if (!flush_autosave()) {
        set_status("Resolve the autosave error before starting the agent");
        return;
    }

    run_tesseract_ = tesseract;
    if (tesseract_context_override_) {
        run_tesseract_context_ = std::move(*tesseract_context_override_);
        tesseract_context_override_.reset();
    } else {
        run_tesseract_context_ = tesseract ? tesseract_.selection_context() : "";
    }
    last_prompt_ = prompt;
    last_image_ = attachment;
    last_run_failed_ = false;
    Rwidget_set_visibility(id("run.retry"), R_COLLAPSED);
    run_started_ = std::chrono::steady_clock::now();
    run_elapsed_seconds_ = 0.0;
    displayed_elapsed_seconds_ = -1;
    if (session_id_.empty() || tesseract) {
        run_title_ = shortened(conversation_title(prompt), 48);
        Rlabel_set_text(id("run.title"), shortened(run_title_, 36).c_str());
        Rwidget_set_tooltip(id("run.title"), run_title_.c_str());
    }
    std::string checkpoint_error;
    if (prompt.front() != '/' && run_mode_ == RunMode::Build) {
        try {
            active_checkpoint_id_ = checkpoint_store_.begin(run_title_);
        } catch (const std::exception& error) {
            checkpoint_error = error.what();
        }
    }
    auto displayed_prompt = prompt;
    if (attachment) {
        displayed_prompt += "\n\nImage attached · " + attachment->path.filename().string() + " · " +
            std::to_string(attachment->width) + " × " + std::to_string(attachment->height);
    }
    // The READY introduction belongs to an empty chat; it is not part of the conversation, and a
    // restored chat never shows it, so it goes when the first prompt is sent.
    if (thread_messages_.empty()) Rwidget_clear_children(id("transcript"));
    add_message("YOU", displayed_prompt, true);
    Rwindow_close_popups(window_);
    transcript_text_ += "\n\nUSER:\n" + prompt;
    if (attachment) transcript_text_ += "\nAttached image: " + attachment->path.string();
    thread_messages_.push_back({"YOU", displayed_prompt, true});
    save_thread();
    recovery_store_.clear_draft();
    Rtext_editor_set_text(active_composer, "");
    attachment.reset();
    refresh_image_attachment(tesseract);
    update_context_summary();
    current_message_ = R_INVALID_ID;
    pending_message_ = R_INVALID_ID;
    activity_expander_ = R_INVALID_ID;
    activity_rows_ = R_INVALID_ID;
    current_text_.clear();
    tool_activity_status_.clear();
    active_checks_.clear();
    activity_count_ = 0;
    activity_failed_ = 0;
    saw_delta_ = false;
    reset_graph();
    Rnode_graph_set_node_active(id("graph"), lead_node_, R_TRUE);
    Rnode_graph_set_node_badge(id("graph"), lead_node_, "run");
    Rlabel_set_text(Rwidget_get_child(Rnode_graph_get_node_content(id("graph"), lead_node_), 0), "LEAD · RUNNING");
    Rnode_graph_set_flow_visible(id("graph"), R_TRUE);
    Rwidget_set_visibility(id("run.stop"), R_VISIBLE);
    update_queue();
    set_run_state("RUNNING");
    Rwidget_set_enabled(id("harness"), R_FALSE);
    Rwidget_set_enabled(id("run.mode"), R_FALSE);
    Rwidget_set_enabled(id("model.select"), R_FALSE);
    Rwidget_set_enabled(id("effort.select"), R_FALSE);
    run_cost_usd_ = -1.0;
    run_input_tokens_ = 0;
    run_output_tokens_ = 0;
    run_usage_reported_ = false;
    Rlabel_set_text(id("run.usage"), "Run cost · pending");
    set_current_action("Starting the agent");
    set_status(checkpoint_error.empty() ? std::string(harness_name()) + " is starting…"
        : "Checkpoint unavailable · " + checkpoint_error);
    show_pending("STARTING", std::string(harness_name()) + " · preparing the workspace and launching the agent…");

    RunOptions options;
    options.working_directory = workspace_.root();
    const std::string knowledge_binary = std::filesystem::exists(RAVEN_IDE_INSTALLED_KNOWLEDGE_BIN)
        ? RAVEN_IDE_INSTALLED_KNOWLEDGE_BIN : RAVEN_IDE_KNOWLEDGE_BIN;
    const auto tool = shell_quote(knowledge_binary) + " --workspace " + shell_quote(workspace_.root().string());
    options.read_only = run_mode_ != RunMode::Build;
    if (run_tesseract_) {
        options.prompt = tesseract_.agent_guide(run_tesseract_context_);
        if (const auto knowledge = workspace_.knowledge_context(prompt, vector_recall_); !knowledge.empty()) {
            options.prompt += "\n\nRELEVANT GRAPH CONTEXT (with provenance and inference):\n" + knowledge;
        }
        options.prompt += "\n\nUSER TASK:\n" + prompt;
    } else if (!prompt.empty() && prompt.front() == '/') {
        options.prompt = prompt;
    } else {
        if (run_mode_ == RunMode::Build) {
            options.prompt =
                "Act as a coding agent, not a chat-only assistant. For implementation requests, inspect the relevant code, make the changes with your tools, and run the project's relevant build and tests before responding. Do not stop at a plan or instructions when you can complete the task. Investigate before answering codebase questions. Keep narration concise and do not announce every tool call. Finish with the outcome, the key files changed, and the checks actually run; never claim unrun checks or unmade edits.\n\n"
                "You have a reviewed neurosymbolic knowledge layer. Read relevant context with `" + tool +
                " context \"your query\"`. Queue durable facts with `" + tool +
                " propose '<operation JSON>'`; never edit wiki/graph.jsonl directly. Graph writes are applied only after human review.\n";
        } else if (run_mode_ == RunMode::Ask) {
            options.prompt =
                "Answer the user's question with read-only investigation. Use available read-only tools to inspect relevant code when needed. Do not edit, create, delete, or stage any files or knowledge writes, and do not implement changes. Give a direct answer with useful workspace paths and line references. Keep narration concise.\n\n"
                "Relevant accepted knowledge can be read with `" + tool + " context \"your query\"`.\n";
        } else {
            options.prompt =
                "Act as a read-only code reviewer. Inspect the relevant implementation and report only actionable findings, ordered by severity with workspace path and line references. Explain the failure scenario and a concrete fix, but do not edit, create, delete, or stage any files or knowledge writes. If there are no material findings, say so plainly and mention residual test gaps. Keep narration concise.\n\n"
                "Relevant accepted knowledge can be read with `" + tool + " context \"your query\"`.\n";
        }
        if (const auto knowledge = workspace_.knowledge_context(prompt, vector_recall_); !knowledge.empty()) {
            options.prompt += "\nRELEVANT GRAPH CONTEXT (with provenance and inference):\n" + knowledge;
        }
        options.prompt += "\nUSER TASK:\n" + prompt;
    }
    options.resource_links = composer_index_.mentions(prompt);
    if (last_image_) options.image_links.push_back(last_image_->path);
    for (const auto& path : options.image_links) {
        options.prompt += "\nAttached image: " + path.string() +
            "\nInspect this image as visual context for the user request. Use the image content already supplied by the client, or Read this file if needed.";
    }
    if (harness_ == Harness::ClaudeCode) {
        for (const auto& path : options.resource_links) {
            options.prompt += "\nReferenced workspace file: @" +
                std::filesystem::relative(path, workspace_.root()).generic_string();
        }
    }
    options.session_id = session_id_;
    options.knowledge_binary = knowledge_binary;
    options.model = current_model();
    options.effort = current_effort();
    run_harness_ = harness_;
    options.harness = run_harness_;
    options.agent = "orchestrator";
    options.agent_file = workspace_.agents_root() / "orchestrator.md";
    std::string error;
    if (!lead_process_.start(options, error)) {
        Rwidget_set_visibility(id("run.stop"), R_COLLAPSED);
        Rwidget_set_visibility(id("run.retry"), R_VISIBLE);
        update_queue();
        Rwidget_set_enabled(id("harness"), R_TRUE);
        Rwidget_set_enabled(id("run.mode"), R_TRUE); Rwidget_set_enabled(id("model.select"), R_TRUE); Rwidget_set_enabled(id("effort.select"), R_TRUE);
        set_run_state("ERROR");
        Rnode_graph_set_node_active(id("graph"), lead_node_, R_FALSE);
        last_run_failed_ = true;
        set_current_action("Start failed");
        notify_run_finished(true);
        finish_checkpoint("start-error");
        show_pending("START ERROR", "Could not start " + std::string(harness_name()) + " · " + error);
        set_status(std::string("Cannot start ") + harness_name() + ": " + error);
    } else {
        try {
            recall_store_.remember(prompt);
        } catch (const std::exception& error) {
            set_status(std::string("Run started · Recall could not be saved · ") + error.what());
        }
        show_pending("STARTING", std::string(harness_name()) + " process launched · waiting for the session…");
    }
}

void App::stop_run() {
    if (!lead_process_.running()) return;
    lead_process_.stop();
    finish_checkpoint("stopped");
    dismiss_pending();
    Rnode_graph_set_node_active(id("graph"), lead_node_, R_FALSE);
    Rlabel_set_text(Rwidget_get_child(Rnode_graph_get_node_content(id("graph"), lead_node_), 0), "LEAD · STOPPED");
    Rnode_graph_set_flow_visible(id("graph"), R_FALSE);
    Rwidget_set_visibility(id("run.stop"), R_COLLAPSED);
    queued_prompt_.clear();
    queued_image_.reset();
    last_run_failed_ = false;
    update_queue();
    Rwidget_set_enabled(id("harness"), R_TRUE);
    Rwidget_set_enabled(id("run.mode"), R_TRUE); Rwidget_set_enabled(id("model.select"), R_TRUE); Rwidget_set_enabled(id("effort.select"), R_TRUE);
    set_current_action("Stopped");
    set_run_state("STOPPED");
    for (const auto& [tool, label] : subagent_status_) {
        if (Rwidget_is_valid(label) && std::string(Rlabel_get_text(label)) == "running") Rlabel_set_text(label, "stopped");
    }
    for (const auto& [tool, label] : tool_activity_status_) {
        if (Rwidget_is_valid(label) && std::string(Rlabel_get_text(label)) == "running") Rlabel_set_text(label, "stopped");
    }
    if (Rwidget_is_valid(activity_expander_)) {
        Rexpander_set_title(activity_expander_, ("Work stopped · " + counted(static_cast<size_t>(activity_count_), "step")).c_str());
    }
    update_graph_summary();
    set_status("Run stopped · follow-ups can resume the session");
}

void App::add_subagent(const ClaudeEvent& event) {
    if (event.tool_id.empty() || subagent_nodes_.count(event.tool_id)) return;
    const auto index = subagent_nodes_.size();
    const auto graph = id("graph");
    const auto title = shortened(event.detail.empty() ? event.name : event.detail, 34);
    const auto node = Rnode_graph_add_node(graph, title.c_str(), 40, 200 + index * 160);
    Rnode_graph_set_node_color(graph, node, 0.91f, 0.47f, 0.29f);
    Rnode_graph_set_node_deletable(graph, node, R_FALSE);
    Rnode_graph_set_node_active(graph, node, R_TRUE);
    Rnode_graph_set_node_active_color(graph, node, 0.91f, 0.47f, 0.29f);
    Rnode_graph_set_node_badge(graph, node, "run");
    auto content = Rwidget_create(R_WIDGET_VBOX);
    Rpanel_set_spacing(content, 4);
    Rwidget_set_min_size(content, 180, 0);
    auto node_status = Rwidget_add_child(content, Rlabel_create((event.name + " · running").c_str()));
    Rlabel_set_text_size(node_status, 11);
    auto node_detail = Rwidget_add_child(content, Rlabel_create("Delegated mission"));
    Rlabel_set_text_size(node_detail, 10);
    float node_muted[4];
    Rwidget_get_color(Rwindow_get_content(window_), R_COLOR_TEXT_DISABLED, &node_muted[0], &node_muted[1], &node_muted[2], &node_muted[3]);
    Rlabel_set_color(node_detail, node_muted[0], node_muted[1], node_muted[2], node_muted[3]);
    Rnode_graph_set_node_content(graph, node, content);
    const auto input = Rnode_graph_add_input(graph, node, "task", "agent");
    Rnode_graph_connect(graph, lead_output_, input);
    subagent_nodes_[event.tool_id] = node;

    Rwidget_set_visibility(id("activity.empty"), R_COLLAPSED);
    auto row = Rwidget_add_child(id("activity.rows"), Rwidget_create(R_WIDGET_HBOX));
    Rpanel_set_spacing(row, 9);
    auto marker = Rwidget_add_child(row, Rwidget_create(R_WIDGET_BORDER));
    Rwidget_set_size(marker, 7, 7);
    Rslot_set_valign(marker, R_VALIGN_CENTER);
    Rborder_set_corner_radius(marker, 4, 4, 4, 4);
    float accent[4];
    Rwidget_get_color(Rwindow_get_content(window_), R_COLOR_ACCENT, &accent[0], &accent[1], &accent[2], &accent[3]);
    Rborder_set_background(marker, accent[0], accent[1], accent[2], accent[3]);
    auto labels = Rwidget_add_child(row, Rwidget_create(R_WIDGET_VBOX));
    Rslot_set_fill(labels, 1);
    Rpanel_set_spacing(labels, 3);
    auto title_label = Rwidget_add_child(labels, Rlabel_create(title.c_str()));
    Rlabel_set_wrap(title_label, R_TRUE);
    auto meta = Rwidget_add_child(labels, Rwidget_create(R_WIDGET_HBOX));
    Rpanel_set_spacing(meta, 8);
    auto kind = Rwidget_add_child(meta, Rlabel_create(event.name.c_str()));
    Rslot_set_fill(kind, 1);
    Rlabel_set_text_size(kind, 11);
    float muted[4];
    Rwidget_get_color(Rwindow_get_content(window_), R_COLOR_TEXT_DISABLED, &muted[0], &muted[1], &muted[2], &muted[3]);
    Rlabel_set_color(kind, muted[0], muted[1], muted[2], muted[3]);
    auto status = Rwidget_add_child(meta, Rlabel_create("running"));
    Rlabel_set_text_size(status, 11);
    Rlabel_set_color(status, accent[0], accent[1], accent[2], accent[3]);
    Rslot_set_valign(status, R_VALIGN_CENTER);
    subagent_markers_[event.tool_id] = marker;
    subagent_kinds_[event.tool_id] = kind;
    subagent_status_[event.tool_id] = status;

    Rnode_graph_frame_all(graph);
    update_graph_summary();
}

void App::finish_tool(const ClaudeEvent& event) {
    const auto tool = tool_activity_status_.find(event.tool_id);
    if (tool != tool_activity_status_.end() && Rwidget_is_valid(tool->second)) {
        if (std::string(Rlabel_get_text(tool->second)) == "running") {
            Rlabel_set_text(tool->second, event.is_error ? "failed" : "done");
            if (event.is_error) ++activity_failed_;
        }
        update_activity_summary();
    }
    const auto check = std::find_if(active_checks_.begin(), active_checks_.end(), [&](const auto& item) {
        return item.id == event.tool_id;
    });
    if (check != active_checks_.end()) {
        check->status = event.is_error ? VerificationStatus::Failed : VerificationStatus::Passed;
        check->output = event.text;
        if (check->output.size() > 8'000) {
            check->output = check->output.substr(0, 4'000) + "\n…\n" + check->output.substr(check->output.size() - 4'000);
        }
    }
    const auto found = subagent_nodes_.find(event.tool_id);
    if (found == subagent_nodes_.end()) return;
    const auto graph = id("graph");
    Rnode_graph_set_node_active(graph, found->second, R_FALSE);
    Rnode_graph_set_node_badge(graph, found->second, event.is_error ? "!" : "done");
    auto content = Rnode_graph_get_node_content(graph, found->second);
    if (Rwidget_is_valid(content)) {
        auto status_label = Rwidget_get_child(content, 0);
        std::string status = Rlabel_get_text(status_label);
        if (const auto position = status.find("running"); position != std::string::npos) {
            status.replace(position, 7, event.is_error ? "failed" : "complete");
            Rlabel_set_text(status_label, status.c_str());
        }
        if (Rnode_graph_is_node_selected(graph, found->second)) Rlabel_set_text(id("graph.selection"), status.c_str());
    }
    float semantic[4];
    Rwidget_get_color(Rwindow_get_content(window_), event.is_error ? R_COLOR_DANGER : R_COLOR_SUCCESS,
                      &semantic[0], &semantic[1], &semantic[2], &semantic[3]);
    Rnode_graph_set_node_color(graph, found->second, semantic[0], semantic[1], semantic[2]);
    const auto activity = subagent_status_.find(event.tool_id);
    if (activity != subagent_status_.end() && Rwidget_is_valid(activity->second)) {
        Rlabel_set_text(activity->second, event.is_error ? "failed" : "complete");
    }
    update_graph_summary();
}

void App::update_graph_summary() {
    const auto count = subagent_nodes_.size();
    size_t running = 0;
    for (const auto& [tool, label] : subagent_status_) {
        if (Rwidget_is_valid(label) && std::string(Rlabel_get_text(label)) == "running") ++running;
    }
    std::string text = "Lineage · ";
    if (running > 0) text += std::to_string(running) + " running";
    else if (count > 0) text += std::to_string(count) + " subagent" + (count == 1 ? "" : "s");
    else text += "no subagents";
    Rlabel_set_text(id("graph.summary"), text.c_str());
    Rwidget_set_visibility(id("graph.summary"), count > 0 ? R_VISIBLE : R_COLLAPSED);
    Rwidget_set_visibility(id("graph.toggle"), count > 0 ? R_VISIBLE : R_COLLAPSED);
    const bool active = lead_process_.running() || running > 0;
    Rlabel_set_text(id("graph.state"), active ? "LIVE" : count > 0 ? "COMPLETE" : "STANDBY");
    Rlabel_set_text(id("graph.caption"), !active && count == 0 ? "Lead standing by · no delegated missions"
        : running > 0 ? (std::to_string(running) + " active · execution flow is live").c_str()
        : count > 0 ? (std::to_string(count) + " delegated mission" + (count == 1 ? "" : "s") + " recorded").c_str()
        : "Lead active · awaiting delegation");
}

void App::handle_lead_event(const ClaudeEvent& event) {
    switch (event.kind) {
    case EventKind::SessionStarted:
        session_id_ = event.session_id;
        save_thread();
        Rlabel_set_text(id("run.subtitle"), ("Orchestrator · " + event.name + " · resumable session").c_str());
        show_pending("CONNECTED", event.name + " session connected · waiting for the agent…");
        set_current_action("Connected · waiting for the agent");
        set_status(std::string("Lead agent running · ") + harness_name() + " session " + shortened(session_id_, 18));
        break;
    case EventKind::ThoughtDelta:
        show_pending("THINKING", std::string(harness_name()) + " is thinking · preparing the next action…");
        set_current_action("Thinking");
        set_status(std::string(harness_name()) + " is thinking…");
        break;
    case EventKind::AssistantDelta:
        dismiss_pending();
        if (!event.parent_tool_id.empty()) {
            const auto found = subagent_nodes_.find(event.parent_tool_id);
            if (found != subagent_nodes_.end()) Rnode_graph_set_node_badge(id("graph"), found->second, "write");
            break;
        }
        if (current_message_ == R_INVALID_ID) {
            current_text_.clear();
            current_message_ = add_message("ORCHESTRATOR", "");
        }
        current_text_ += event.text;
        saw_delta_ = true;
        Rlabel_set_text(current_message_, current_text_.c_str());
        set_current_action("Writing the response");
        set_status(std::string(harness_name()) + " is responding…");
        Rscroll_box_scroll_to_end(id("transcript.scroll"));
        break;
    case EventKind::AssistantMessage:
        dismiss_pending();
        if (!event.parent_tool_id.empty()) break;
        if (saw_delta_ && current_message_ != R_INVALID_ID) {
            Rlabel_set_text(current_message_, event.text.c_str());
        } else {
            add_message("ORCHESTRATOR", event.text);
        }
        transcript_text_ += "\n\nASSISTANT:\n" + event.text;
        thread_messages_.push_back({"ORCHESTRATOR", event.text, false});
        save_thread();
        current_message_ = R_INVALID_ID;
        current_text_.clear();
        saw_delta_ = false;
        break;
    case EventKind::ToolStarted:
        dismiss_pending();
        add_tool_activity(event);
        set_current_action(tool_action(event) + (event.detail.empty() ? "" : " · " + shortened(event.detail, 56)));
        set_status(tool_action(event) + " in progress…");
        break;
    case EventKind::SubagentStarted:
        dismiss_pending();
        add_subagent(event);
        add_tool_activity(event);
        set_current_action("Delegated work · " + event.name);
        set_status("Delegated work in progress · " + event.name);
        break;
    case EventKind::ToolFinished:
        finish_tool(event);
        if (event.is_error) {
            if (Rwidget_is_valid(activity_rows_) && !event.text.empty()) {
                auto detail = Rwidget_add_child(activity_rows_, Rlabel_create(shortened(event.text, 600).c_str()));
                Rlabel_set_text_size(detail, 11);
                Rlabel_set_wrap(detail, R_TRUE);
                Rslot_set_padding(detail, 0, 0, 0, 4);
            }
            set_status("A tool failed · expand Work activity for details");
        }
        break;
    case EventKind::RunFinished: {
        update_run_usage(&event);
        dismiss_pending();
        if (saw_delta_ && current_message_ != R_INVALID_ID) {
            transcript_text_ += "\n\nASSISTANT:\n" + current_text_;
            thread_messages_.push_back({"ORCHESTRATOR", current_text_, false});
            current_message_ = R_INVALID_ID;
            current_text_.clear();
            saw_delta_ = false;
        }
        save_thread();
        finish_checkpoint(event.is_error ? "error" : "complete");
        finish_activity(event.is_error);
        Rnode_graph_set_node_active(id("graph"), lead_node_, R_FALSE);
        Rnode_graph_set_node_badge(id("graph"), lead_node_, event.is_error ? "!" : "done");
        Rlabel_set_text(Rwidget_get_child(Rnode_graph_get_node_content(id("graph"), lead_node_), 0),
                        event.is_error ? "LEAD · ERROR" : "LEAD · COMPLETE");
        Rnode_graph_set_flow_visible(id("graph"), R_FALSE);
        for (const auto& [tool, node] : subagent_nodes_) Rnode_graph_set_node_active(id("graph"), node, R_FALSE);
        Rwidget_set_visibility(id("run.stop"), R_COLLAPSED);
        Rwidget_set_visibility(id("run.retry"), event.is_error ? R_VISIBLE : R_COLLAPSED);
        update_queue();
        set_run_state(event.is_error ? "ERROR" : "COMPLETE");
        for (const auto& [tool, label] : subagent_status_) {
            if (Rwidget_is_valid(label) && std::string(Rlabel_get_text(label)) == "running") {
                Rlabel_set_text(label, event.is_error ? "failed" : "complete");
            }
        }
        update_graph_summary();
        last_run_failed_ = event.is_error;
        set_current_action(event.is_error ? "Run failed" : "Run complete");
        notify_run_finished(event.is_error);
        if (event.is_error) {
            Rwidget_set_enabled(id("harness"), R_TRUE);
            Rwidget_set_enabled(id("run.mode"), R_TRUE); Rwidget_set_enabled(id("model.select"), R_TRUE); Rwidget_set_enabled(id("effort.select"), R_TRUE);
            add_message("RUN ERROR", event.text);
            set_status(std::string(harness_name()) + " run failed · " + event.detail);
        } else if (run_mode_ == RunMode::Build) {
            set_status("Run complete · preparing a knowledge review");
            start_wiki_curator();
        } else {
            Rwidget_set_enabled(id("harness"), R_TRUE);
            Rwidget_set_enabled(id("run.mode"), R_TRUE); Rwidget_set_enabled(id("model.select"), R_TRUE); Rwidget_set_enabled(id("effort.select"), R_TRUE);
            set_status(run_mode_ == RunMode::Ask ? "Ask complete · no workspace changes" : "Review complete · no workspace changes");
        }
        break;
    }
    case EventKind::Error:
        if (!event.text.empty()) add_message("ERROR", std::string(harness_name()) + ": " + event.text);
        break;
    case EventKind::SubagentFinished:
        break;
    }
}

void App::start_wiki_curator() {
    if (transcript_text_.empty()) {
        Rwidget_set_enabled(id("harness"), R_TRUE);
        Rwidget_set_enabled(id("run.mode"), R_TRUE); Rwidget_set_enabled(id("model.select"), R_TRUE); Rwidget_set_enabled(id("effort.select"), R_TRUE);
        set_status("Run complete · no transcript for knowledge review");
        return;
    }
    set_current_action("Reviewing knowledge");
    update_queue();
    try {
        workspace_.stage_run_episode(session_id_, run_title_, harness_name(), transcript_text_, "completed");
    } catch (const std::exception& error) {
        set_status(std::string("Run complete · memory staging failed: ") + error.what());
    }
    set_run_state("KNOWLEDGE REVIEW");
    wiki_output_.clear();
    auto prompt = std::string(
        "Review this completed lead-agent run and propose durable updates to the workspace wiki. "
        "Use the supplied existing notes. Return no edits when the run contains no settled knowledge worth keeping. "
        "Each edit must be a complete replacement Markdown file with a wiki-relative path (do not prefix it with wiki/). "
        "Also return a graph array of typed upsert_entity or relate operations with provenance and confidence; use an empty array when no graph change is needed. "
        "Also return title: a plain 2 to 5 word title for this conversation, in the user's language, with no quotes or final period. "
        "Do not write files yourself. Return only JSON with title, summary, edits, and graph.\n\nEXISTING WIKI FILES:\n");
    size_t wiki_budget = 40'000;
    for (const auto& note : notes_) {
        const auto path = std::filesystem::relative(note.path, workspace_.wiki_root()).generic_string();
        // A note with an unreviewed edit is shown as that edit, so this run builds on it.
        const std::string* content = &note.content;
        if (proposal_) {
            for (const auto& edit : proposal_->edits) {
                if (edit.path == path) content = &edit.after;
            }
        }
        const auto available = std::min(wiki_budget, content->size());
        prompt += "\n--- " + path + " ---\n" + content->substr(0, available) + "\n";
        wiki_budget -= available;
        if (wiki_budget == 0) break;
    }
    if (const auto staged = workspace_.knowledge().staged(); !staged.empty()) {
        prompt += "\nSTAGED GRAPH WRITES TO REVIEW AND DEDUPLICATE:\n";
        for (const auto& operation : staged) prompt += KnowledgeGraph::to_json(operation) + "\n";
    }
    auto transcript = transcript_text_;
    if (transcript.size() > 60'000) transcript = transcript.substr(transcript.size() - 60'000);
    prompt += "\nRUN TRANSCRIPT:\n" + transcript;
    const auto curator = std::find_if(agents_.begin(), agents_.end(), [](const auto& agent) {
        return agent.name == "wiki-curator";
    });
    if (curator != agents_.end()) prompt = agent_body(curator->content) + "\n\n" + prompt;

    RunOptions options;
    options.working_directory = workspace_.root();
    options.prompt = std::move(prompt);
    options.read_only = true;
    options.wiki_schema = true;
    options.harness = run_harness_;
    options.model = current_model();
    options.effort = current_effort();
    options.agent_file = workspace_.agents_root() / "wiki-curator.md";
    std::string error;
    if (!wiki_process_.start(options, error)) {
        Rwidget_set_enabled(id("harness"), R_TRUE);
        Rwidget_set_enabled(id("run.mode"), R_TRUE); Rwidget_set_enabled(id("model.select"), R_TRUE); Rwidget_set_enabled(id("effort.select"), R_TRUE);
        set_run_state("COMPLETE");
        set_current_action("Knowledge review unavailable");
        update_queue();
        set_status("Run complete · wiki curator could not start: " + error);
    }
}

void App::handle_wiki_event(const ClaudeEvent& event) {
    if (event.kind == EventKind::Error) {
        Rlabel_set_text(id("run.subtitle"), ("Wiki curator error · " + event.text).c_str());
        set_status("Wiki curator: " + event.text);
        return;
    }
    if (event.kind == EventKind::AssistantMessage) {
        wiki_output_ += event.text;
        return;
    }
    if (event.kind != EventKind::RunFinished) return;
    update_run_usage(&event, true);
    if (event.has_usage && !active_run_notification_id_.empty()) {
        try {
            notification_store_.update_body(active_run_notification_id_, run_title_ + "\n" + run_usage_summary());
            refresh_notifications();
        } catch (const std::exception& error) {
            set_status(std::string("Run receipt could not be updated · ") + error.what());
        }
    }
    Rwidget_set_enabled(id("harness"), R_TRUE);
    Rwidget_set_enabled(id("run.mode"), R_TRUE); Rwidget_set_enabled(id("model.select"), R_TRUE); Rwidget_set_enabled(id("effort.select"), R_TRUE);
    set_current_action(event.is_error ? "Knowledge review failed" : "Knowledge review complete");
    update_queue();
    set_run_state(event.is_error ? "KNOWLEDGE ERROR" : "COMPLETE");
    if (event.is_error) {
        set_status("Run complete · knowledge review failed: " + event.text);
        return;
    }
    try {
        const auto payload = event.text.empty() ? wiki_output_ : event.text;
        const auto begin = payload.find('{');
        const auto end = payload.rfind('}');
        if (begin == std::string::npos || end == std::string::npos || end <= begin) {
            throw std::runtime_error("curator did not return a JSON object");
        }
        const auto data = nlohmann::json::parse(payload.substr(begin, end - begin + 1));
        apply_agent_title(data.value("title", ""));
        std::vector<std::pair<std::string, std::string>> edits;
        for (const auto& edit : data.at("edits")) edits.emplace_back(edit.at("path"), edit.at("content"));
        // Unreviewed edits from an earlier run stay in the proposal unless this run replaced the note.
        if (proposal_) {
            for (const auto& earlier : proposal_->edits) {
                const bool replaced = std::any_of(edits.begin(), edits.end(), [&](const auto& edit) { return edit.first == earlier.path; });
                if (!replaced) edits.emplace_back(earlier.path, earlier.after);
            }
        }
        std::map<std::string, GraphOperation> unique_operations;
        int skipped_operations = 0;
        if (data.contains("graph")) {
            for (const auto& item : data["graph"]) {
                // One malformed operation (for example an id with a path in it) drops only itself;
                // the rest of the review still reaches the user.
                try {
                    auto operation = KnowledgeGraph::from_json(item.dump());
                    KnowledgeGraph::validate(operation);
                    unique_operations[operation.id] = std::move(operation);
                } catch (const std::exception&) {
                    ++skipped_operations;
                }
            }
        }
        for (const auto& operation : workspace_.knowledge().staged()) unique_operations[operation.id] = operation;
        std::vector<GraphOperation> graph;
        for (const auto& [id, operation] : unique_operations) graph.push_back(operation);
        if (edits.empty() && graph.empty()) {
            set_status("Run complete · wiki curator found no durable changes");
            return;
        }
        proposal_ = workspace_.validate_proposal(data.value("summary", "Suggested wiki updates"), edits, graph);
        Rlabel_set_text(id("proposal.summary"), shortened(proposal_->summary, 140).c_str());  // full text in Review
        Rwidget_set_visibility(id("proposal.bar"), R_VISIBLE);
        update_queue();
        set_status(skipped_operations > 0
            ? "Knowledge review ready · " + counted(static_cast<size_t>(skipped_operations), "invalid graph operation") + " left out"
            : "Knowledge review ready · review it from Activity whenever you like");
        notify_event("Stellar · Knowledge review ready", proposal_->summary, false, "knowledge", "proposal");
    } catch (const std::exception& error) {
        set_status(std::string("Run complete · invalid knowledge proposal: ") + error.what());
    }
}

void App::pump() {
    auto lead_events = lead_process_.drain();
    for (const auto& event : lead_events) handle_lead_event(event);
    auto wiki_events = wiki_process_.drain();
    for (const auto& event : wiki_events) handle_wiki_event(event);
    pump_endurance();
    maybe_start_queued();
    // A harness process reports its last event before it exits, so the chat row can still read
    // "running" when the final state is set. Refresh once more when the processes have stopped.
    const bool busy = lead_process_.running() || wiki_process_.running();
    if (busy != threads_busy_) {
        threads_busy_ = busy;
        refresh_threads();
    }
    update_reading_column();
    sync_accessibility();
    sync_file_browser_path();
    // The Review → Knowledge tab follows the pending proposal. A cheap key detects changes.
    const auto proposal_key = proposal_ ? proposal_->summary + "#" + std::to_string(proposal_->edits.size()) + "#" +
        std::to_string(proposal_->graph.size()) : std::string();
    if (proposal_key != rendered_proposal_key_) {
        rendered_proposal_key_ = proposal_key;
        render_proposal();
        save_pending_wiki_edits();
    }
    // Accept and Discard only exist while there is a proposal to act on.
    const auto proposal_actions = proposal_ ? R_VISIBLE : R_COLLAPSED;
    if (Rwidget_get_visibility(id("proposal.actions")) != proposal_actions) Rwidget_set_visibility(id("proposal.actions"), proposal_actions);
}

// Raven exposes every child of a Switcher and everything under an overlay to screen readers. Only
// the page on screen is exposed: inactive pages are hidden, and the workspace is hidden while the
// opening or the world gallery covers it.
void App::sync_accessibility() {
    // Runs every frame; only update the tree when a page or cover actually changed, so screen
    // readers are not sent a stream of identical changes.
    std::string key;
    for (const char* name : {"left.tabs", "center.tabs", "more.tabs", "right.tabs"}) key += std::to_string(Rswitcher_get_index(id(name))) + ",";
    key += splash_.active() ? "s" : "-";
    key += Rwidget_get_visibility(id("theme.gallery")) == R_VISIBLE ? "g" : "-";
    if (key == accessibility_key_) return;
    accessibility_key_ = key;
    for (const char* name : {"left.tabs", "center.tabs", "more.tabs", "right.tabs"}) {
        const auto switcher = id(name);
        if (!Rwidget_is_valid(switcher)) continue;
        const int active = Rswitcher_get_index(switcher);
        const int count = Rwidget_get_child_count(switcher);
        for (int index = 0; index < count; ++index) Rwidget_set_accessible_hidden(Rwidget_get_child(switcher, index), index != active);
    }
    const bool covered = splash_.active() || Rwidget_get_visibility(id("theme.gallery")) == R_VISIBLE;
    Rwidget_set_accessible_hidden(id("app.shell"), covered);
}

// The file tree's path field shows the folder relative to the workspace (read-only), and the tree
// cannot leave the workspace through its Up button.
void App::sync_file_browser_path() {
    const auto browser = id("workspace.files");
    if (!Rwidget_is_valid(browser) || Rwidget_get_child_count(browser) == 0) return;
    const char* current = Rfile_browser_get_directory(browser);
    if (!current) return;
    // pump() runs every animation frame: the label is only recomputed (with filesystem calls)
    // when the folder changes; otherwise this is a string comparison.
    if (browser_directory_ != current) {
        browser_directory_ = current;
        std::error_code error;
        const auto root = std::filesystem::weakly_canonical(workspace_.root(), error);
        const auto directory = std::filesystem::weakly_canonical(current, error);
        auto relative = std::filesystem::relative(directory, root, error).generic_string();
        if (error || relative.rfind("..", 0) == 0) {
            Rfile_browser_set_directory(browser, root.string().c_str());
            browser_directory_ = root.string();
            relative = ".";
        }
        browser_label_ = workspace_.name() + (relative == "." ? "" : "/" + relative);
    }
    const auto path_field = Rwidget_get_child(browser, 0);
    if (Rwidget_get_type(path_field) != R_WIDGET_TEXT_INPUT) return;
    const char* shown = Rtext_input_get_text(path_field);
    if (shown && browser_label_ == shown) return;
    Rtext_input_set_read_only(path_field, R_TRUE);
    Rtext_input_set_text(path_field, browser_label_.c_str());
}

// Centers the transcript, queue bar and composer in the 860px reading column. A filling slot in a
// box panel ignores MaxWidth and cannot center what a maximum leaves over, so wide windows stretched
// prose and the composer across the whole center pane. The side padding is recomputed whenever the
// pane's width changes.
void App::update_reading_column() {
    float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f;
    Rwidget_get_rect(id("transcript.scroll"), &x, &y, &width, &height);
    if (width > 0.0f && width != reading_pane_width_) {
        reading_pane_width_ = width;
        const float column = 860.0f;
        const float transcript_side = std::max(26.0f, (width - column) / 2.0f);
        const float tool_side = std::max(24.0f, (width - column) / 2.0f);
        Rslot_set_padding(id("transcript.row"), transcript_side, 30.0f, transcript_side, 30.0f);
        Rslot_set_padding(id("queue.row"), tool_side, 0.0f, tool_side, 6.0f);
        Rslot_set_padding(id("composer.row"), tool_side, 6.0f, tool_side, 22.0f);
        // Re-wrapping changes every message height. Keep following the newest entry while a reply
        // streams or a restore is still settling; otherwise leave the reader's position alone.
        if (transcript_follow_frames_ > 0 || lead_process_.running()) transcript_follow_frames_ = 3;
    }
    // The opening screen covers the workspace; settle the scroll position once it is visible.
    if (transcript_follow_frames_ > 0 && width > 0.0f && !splash_.active()) {
        --transcript_follow_frames_;
        Rscroll_box_scroll_to_end(id("transcript.scroll"));
        Rwindow_request_redraw(window_);
    }
}

void App::finish_checkpoint(const std::string& status) {
    if (active_checkpoint_id_.empty()) return;
    const auto id = active_checkpoint_id_;
    active_checkpoint_id_.clear();
    for (auto& check : active_checks_) {
        if (check.status == VerificationStatus::Running) {
            check.status = VerificationStatus::Failed;
            if (!check.output.empty()) check.output += "\n";
            check.output += "The run ended before this command completed.";
        }
    }
    try {
        const auto checkpoint = checkpoint_store_.finish(id, status, active_checks_);
        active_checks_.clear();
        refresh_checkpoint_history(checkpoint.changes.empty() && checkpoint.checks.empty() ? selected_checkpoint_id_ : checkpoint.id);
        const auto failed = std::count_if(checkpoint.checks.begin(), checkpoint.checks.end(), [](const auto& check) {
            return check.status == VerificationStatus::Failed;
        });
        if (status == "complete" && failed > 0) {
            notify_event("Stellar · Verification failed", counted(static_cast<size_t>(failed), "check") + " failed · " + checkpoint.title, true, "verification", "verification", checkpoint.id);
        }
        if (!checkpoint.changes.empty()) {
            set_status("Checkpoint ready · " + counted(checkpoint.changes.size(), "changed file"));
        } else if (!checkpoint.checks.empty()) {
            set_status("Verification ready · " + counted(checkpoint.checks.size(), "check"));
        }
    } catch (const std::exception& error) {
        set_status(std::string("Checkpoint could not be finalized: ") + error.what());
    }
}

void App::refresh_checkpoint_history(const std::string& selected) {
    checkpoint_rows_.clear();
    Rcombo_box_clear_items(id("checkpoint.history"));
    std::string chosen = selected;
    if (chosen.empty() && checkpoint_store_.find(selected_checkpoint_id_)) chosen = selected_checkpoint_id_;
    if (chosen.empty() && !checkpoint_store_.checkpoints().empty()) chosen = checkpoint_store_.checkpoints().front().id;
    int selected_index = -1;
    for (const auto& checkpoint : checkpoint_store_.checkpoints()) {
        if (checkpoint.id == chosen) selected_index = static_cast<int>(checkpoint_rows_.size());
        checkpoint_rows_.push_back(checkpoint.id);
        const auto label = checkpoint.title + (checkpoint.status == "restored" ? " · restored" : "");
        Rcombo_box_add_item(id("checkpoint.history"), label.c_str());
    }
    Rcombo_box_set_selected(id("checkpoint.history"), selected_index);
    Rwidget_set_enabled(id("checkpoint.history"), selected_index >= 0);

    if (checkpoint_store_.checkpoints().empty()) {
        Rwidget_set_visibility(id("checkpoint.bar"), R_COLLAPSED);
    } else {
        const auto& latest = checkpoint_store_.checkpoints().front();
        const auto failed = std::count_if(latest.checks.begin(), latest.checks.end(), [](const auto& check) {
            return check.status == VerificationStatus::Failed;
        });
        Rlabel_set_text(id("checkpoint.notice.title"), failed > 0 ? "Verification failed"
            : latest.changes.empty() ? "Verification ready" : "Code changes ready");
        auto notice = counted(latest.changes.size(), "file");
        if (!latest.checks.empty()) notice += " · " + counted(latest.checks.size(), "check");
        notice += " · " + latest.title;
        Rlabel_set_text(id("checkpoint.notice"), notice.c_str());
        Rwidget_set_visibility(id("checkpoint.bar"), R_VISIBLE);
    }
    open_checkpoint(chosen);
}

void App::open_checkpoint(const std::string& checkpoint_id) {
    const auto* checkpoint = checkpoint_store_.find(checkpoint_id);
    selected_checkpoint_id_ = checkpoint ? checkpoint_id : "";
    // With a single checkpoint the selector only repeated the title above it; Restore task moves
    // to the file row in that case.
    const bool several_checkpoints = checkpoint_store_.checkpoints().size() > 1;
    Rwidget_set_visibility(id("checkpoint.history.row"), several_checkpoints ? R_VISIBLE : R_COLLAPSED);
    Rwidget_set_visibility(id("checkpoint.restore.inline"), !several_checkpoints && checkpoint ? R_VISIBLE : R_COLLAPSED);
    Rwidget_set_visibility(id("checkpoint.files.row"), !checkpoint || checkpoint->changes.empty() ? R_COLLAPSED : R_VISIBLE);
    Rwidget_set_visibility(id("checkpoint.hunks.row"), R_COLLAPSED);
    Rwidget_set_visibility(id("checkpoint.content.tabs"), checkpoint ? R_VISIBLE : R_COLLAPSED);
    Rcombo_box_clear_items(id("checkpoint.files"));
    Rlist_view_clear_items(id("verification.results"));
    if (!checkpoint) {
        Rlabel_set_text(id("checkpoint.title"), "No checkpoints yet");
        Rlabel_set_text(id("checkpoint.summary"), "Complete a Build run to create a checkpoint. Diffs, hunk restores, and verification results will appear here.");
        Rtext_editor_set_text(id("checkpoint.diff"), "");
        Rcombo_box_clear_items(id("checkpoint.hunks"));
        checkpoint_hunk_ids_.clear();
        Rwidget_set_enabled(id("checkpoint.files"), R_FALSE);
        Rwidget_set_enabled(id("checkpoint.open"), R_FALSE);
        Rwidget_set_enabled(id("checkpoint.restore"), R_FALSE);
        Rwidget_set_enabled(id("checkpoint.restore.inline"), R_FALSE);
        Rwidget_set_enabled(id("checkpoint.restore_hunk"), R_FALSE);
        open_verification(-1);
        set_active_view(id("checkpoint.content.tabs"), 0);
        return;
    }

    Rlabel_set_text(id("checkpoint.title"), checkpoint->title.c_str());
    auto summary = counted(checkpoint->changes.size(), "changed file") + " · " + checkpoint->status;
    if (checkpoint->partial) summary += " · partial snapshot";
    const auto passed = std::count_if(checkpoint->checks.begin(), checkpoint->checks.end(), [](const auto& check) {
        return check.status == VerificationStatus::Passed;
    });
    const auto failed = std::count_if(checkpoint->checks.begin(), checkpoint->checks.end(), [](const auto& check) {
        return check.status == VerificationStatus::Failed;
    });
    if (!checkpoint->checks.empty()) {
        summary += " · " + std::to_string(passed) + " passed";
        if (failed > 0) summary += " · " + std::to_string(failed) + " failed";
    }
    Rlabel_set_text(id("checkpoint.summary"), summary.c_str());
    for (const auto& change : checkpoint->changes) {
        const char* kind = change.kind == CheckpointChangeKind::Added ? "Added · "
            : change.kind == CheckpointChangeKind::Deleted ? "Deleted · " : "Modified · ";
        Rcombo_box_add_item(id("checkpoint.files"), (kind + change.path).c_str());
    }
    for (const auto& check : checkpoint->checks) {
        std::string status = verification_status_name(check.status);
        std::transform(status.begin(), status.end(), status.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        Rlist_view_add_item(id("verification.results"), (status + " · " + check.kind + " · " + shortened(check.command, 90)).c_str());
    }
    Rcombo_box_set_selected(id("checkpoint.files"), checkpoint->changes.empty() ? -1 : 0);
    Rwidget_set_enabled(id("checkpoint.files"), !checkpoint->changes.empty());
    Rwidget_set_enabled(id("checkpoint.open"), !checkpoint->changes.empty());
    Rwidget_set_enabled(id("checkpoint.restore"), !checkpoint->changes.empty() && checkpoint->status != "restored");
    Rwidget_set_enabled(id("checkpoint.restore.inline"), !checkpoint->changes.empty() && checkpoint->status != "restored");
    open_verification(0);
    show_checkpoint_diff(0);
}

void App::show_checkpoint_diff(int index) {
    set_active_view(id("checkpoint.content.tabs"), 0);
    const auto* checkpoint = checkpoint_store_.find(selected_checkpoint_id_);
    Rcombo_box_clear_items(id("checkpoint.hunks"));
    checkpoint_hunk_ids_.clear();
    if (!checkpoint || index < 0 || index >= static_cast<int>(checkpoint->changes.size())) {
        Rtext_editor_set_text(id("checkpoint.diff"), "");
        Rwidget_set_visibility(id("checkpoint.hunks.row"), R_COLLAPSED);
        Rwidget_set_enabled(id("checkpoint.restore_hunk"), R_FALSE);
        return;
    }
    try {
        const auto difference = checkpoint_store_.diff(selected_checkpoint_id_, checkpoint->changes[index].path);
        Rtext_editor_set_text(id("checkpoint.diff"), difference.c_str());
        for (const auto& file : checkpoint_store_.hunks(selected_checkpoint_id_)) {
            for (const auto& hunk : file.hunks) {
                checkpoint_hunk_ids_.push_back(hunk.id);
                Rcombo_box_add_item(id("checkpoint.hunks"), hunk_label(hunk).c_str());
            }
        }
    } catch (const std::exception& error) {
        Rtext_editor_set_text(id("checkpoint.diff"), error.what());
    }
    Rcombo_box_set_selected(id("checkpoint.hunks"), checkpoint_hunk_ids_.empty() ? -1 : 0);
    Rwidget_set_visibility(id("checkpoint.hunks.row"), checkpoint_hunk_ids_.empty() ? R_COLLAPSED : R_VISIBLE);
    Rwidget_set_enabled(id("checkpoint.restore_hunk"), !checkpoint_hunk_ids_.empty());
}

void App::open_checkpoint_file() {
    const auto* checkpoint = checkpoint_store_.find(selected_checkpoint_id_);
    const int index = Rcombo_box_get_selected(id("checkpoint.files"));
    if (!checkpoint || index < 0 || index >= static_cast<int>(checkpoint->changes.size())) return;
    const auto& change = checkpoint->changes[index];
    if (change.kind == CheckpointChangeKind::Deleted) {
        set_status("This file was deleted by the task · restore the checkpoint to reopen it");
        return;
    }
    open_code_file(workspace_.root() / change.path);
}

void App::open_verification(int index) {
    const auto* checkpoint = checkpoint_store_.find(selected_checkpoint_id_);
    current_verification_ = checkpoint && index >= 0 && index < static_cast<int>(checkpoint->checks.size()) ? index : -1;
    Rwidget_set_visibility(id("verification.results"), current_verification_ >= 0 ? R_VISIBLE : R_COLLAPSED);
    Rwidget_set_visibility(id("verification.actions.row"), current_verification_ >= 0 ? R_VISIBLE : R_COLLAPSED);
    Rwidget_set_visibility(id("verification.diagnostics.row"), R_COLLAPSED);
    if (Rlist_view_get_selected(id("verification.results")) != current_verification_) {
        Rlist_view_set_selected(id("verification.results"), current_verification_);
    }
    Rcombo_box_clear_items(id("verification.diagnostics"));
    verification_diagnostics_.clear();
    Rwidget_set_enabled(id("verification.rerun"), current_verification_ >= 0 && !verification_rerun_running_);
    Rbutton_set_text(id("verification.watch"), watch_checks_ ? "Watching" : "Watch checks");
    if (current_verification_ < 0) {
        Rlabel_set_text(id("verification.run_state"), "No checks");
        Rtext_editor_set_text(id("verification.detail"), "No build or test commands were recorded for this task.");
        Rwidget_set_enabled(id("verification.diagnostics"), R_FALSE);
        Rwidget_set_enabled(id("verification.open_diagnostic"), R_FALSE);
        set_active_view(id("checkpoint.content.tabs"), 1);
        return;
    }

    const auto& check = checkpoint->checks[current_verification_];
    std::string status = verification_status_name(check.status);
    std::transform(status.begin(), status.end(), status.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    Rlabel_set_text(id("verification.run_state"), status.c_str());
    const auto detail = "$ " + check.command + "\n\n" + status + " · " + check.kind + "\n\n" + check.output;
    Rtext_editor_set_text(id("verification.detail"), detail.c_str());
    verification_diagnostics_ = parse_diagnostics(check.output);
    for (const auto& diagnostic : verification_diagnostics_) {
        const auto location = diagnostic.path + ":" + std::to_string(diagnostic.line);
        const auto label = diagnostic.severity + " · " + location + " · " + shortened(diagnostic.message, 90);
        Rcombo_box_add_item(id("verification.diagnostics"), label.c_str());
    }
    Rcombo_box_set_selected(id("verification.diagnostics"), verification_diagnostics_.empty() ? -1 : 0);
    Rwidget_set_visibility(id("verification.diagnostics.row"), verification_diagnostics_.empty() ? R_COLLAPSED : R_VISIBLE);
    Rwidget_set_enabled(id("verification.diagnostics"), !verification_diagnostics_.empty());
    Rwidget_set_enabled(id("verification.open_diagnostic"), !verification_diagnostics_.empty());
    set_active_view(id("checkpoint.content.tabs"), 1);
}

void App::open_diagnostic() {
    const int index = Rcombo_box_get_selected(id("verification.diagnostics"));
    if (index < 0 || index >= static_cast<int>(verification_diagnostics_.size())) return;
    const auto& diagnostic = verification_diagnostics_[index];
    open_code_file(workspace_.root() / diagnostic.path, diagnostic.line);
}

void App::rerun_verification() {
    if (verification_rerun_running_) {
        set_status("A verification check is already running");
        return;
    }
    const auto* checkpoint = checkpoint_store_.find(selected_checkpoint_id_);
    if (!checkpoint || checkpoint->checks.empty()) {
        set_status("Select a recorded build or test check first");
        return;
    }
    int index = current_verification_;
    if (index < 0 || index >= static_cast<int>(checkpoint->checks.size())) {
        index = 0;
        for (int i = 0; i < static_cast<int>(checkpoint->checks.size()); ++i) {
            if (checkpoint->checks[i].status == VerificationStatus::Failed) {
                index = i;
                break;
            }
        }
    }
    rerun_checkpoint_id_ = selected_checkpoint_id_;
    rerun_check_id_ = checkpoint->checks[index].id;
    auto running = checkpoint->checks[index];
    running.status = VerificationStatus::Running;
    checkpoint_store_.update_check(rerun_checkpoint_id_, running);
    refresh_checkpoint_history(rerun_checkpoint_id_);
    open_verification(index);
    verification_rerun_running_ = true;
    Rlabel_set_text(id("verification.run_state"), "Rerunning…");
    Rwidget_set_enabled(id("verification.rerun"), R_FALSE);
    const auto command = running.command;
    const auto root = workspace_.root();
    rerun_future_ = std::async(std::launch::async, [id = running.id, command, root] {
        return run_verification_command(id, command, root);
    });
}

void App::toggle_watch_checks() {
    watch_checks_ = !watch_checks_;
    Rbutton_set_text(id("verification.watch"), watch_checks_ ? "Watching" : "Watch checks");
    save_profile();
    set_status(watch_checks_ ? "Watch mode enabled · selected check reruns after autosave" : "Watch mode disabled");
}

void App::schedule_watch() {
    if (!watch_checks_ || selected_checkpoint_id_.empty()) return;
    watch_due_pending_ = true;
    watch_due_ = std::chrono::steady_clock::now() + std::chrono::seconds(1);
}

void App::pump_verification() {
    if (verification_rerun_running_ && rerun_future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        const auto result = rerun_future_.get();
        verification_rerun_running_ = false;
        try {
            checkpoint_store_.update_check(rerun_checkpoint_id_, result);
            refresh_checkpoint_history(rerun_checkpoint_id_);
            const auto* checkpoint = checkpoint_store_.find(rerun_checkpoint_id_);
            int index = 0;
            for (int i = 0; checkpoint && i < static_cast<int>(checkpoint->checks.size()); ++i) {
                if (checkpoint->checks[i].id == result.id) {
                    index = i;
                    break;
                }
            }
            open_verification(index);
            if (result.status == VerificationStatus::Failed) {
                notify_event("Stellar · Verification failed", result.command, true, "verification", "verification", rerun_checkpoint_id_);
            }
            set_status(result.status == VerificationStatus::Passed ? "Verification passed" : "Verification failed");
            Rlabel_set_text(id("verification.run_state"), verification_status_name(result.status));
        } catch (const std::exception& error) {
            set_status(error.what());
            Rlabel_set_text(id("verification.run_state"), "Rerun failed");
        }
        Rwidget_set_enabled(id("verification.rerun"), R_TRUE);
    }
    if (watch_due_pending_ && std::chrono::steady_clock::now() >= watch_due_ && !verification_rerun_running_) {
        watch_due_pending_ = false;
        rerun_verification();
    }
}

void App::restore_checkpoint() {
    if (lead_process_.running() || wiki_process_.running()) {
        set_status("Finish the active run before restoring a checkpoint");
        return;
    }
    const auto* checkpoint = checkpoint_store_.find(selected_checkpoint_id_);
    if (!checkpoint || !can_discard_code() || !flush_autosave()) return;
    const auto checkpoint_id = checkpoint->id;
    const auto message = "Restore " + counted(checkpoint->changes.size(), "file") +
        " to the state before this task? Files changed afterward will be left untouched and the restore will be cancelled.";
    if (Rmessage_box(window_, "Restore task checkpoint", message.c_str(), R_MESSAGE_YES_NO) != R_MESSAGE_RESULT_YES) return;
    try {
        const auto affected_editor = !current_code_path_.empty() && std::any_of(checkpoint->changes.begin(), checkpoint->changes.end(), [&](const auto& change) {
            return workspace_.root() / change.path == current_code_path_;
        });
        const auto agent_path = current_agent_ >= 0 && current_agent_ < static_cast<int>(agents_.size())
            ? agents_[current_agent_].path : std::filesystem::path{};
        const auto note_path = current_note_ >= 0 && current_note_ < static_cast<int>(notes_.size())
            ? notes_[current_note_].path : std::filesystem::path{};
        checkpoint_store_.restore(checkpoint_id);
        if (affected_editor) {
            if (std::filesystem::is_regular_file(current_code_path_)) open_code_file(current_code_path_);
            else {
                current_code_path_.clear();
                current_code_modified_ = false;
                code_autosave_pending_ = false;
                Rtext_editor_set_text(id("code.editor"), "");
                Rlabel_set_text(id("code.title"), "No file open");
                Rlabel_set_text(id("code.path"), "Select a file in FILES");
            }
        }
        agents_ = workspace_.agents();
        notes_ = workspace_.wiki_notes();
        current_agent_ = current_note_ = -1;
        refresh_lists();
        const auto agent = std::find_if(agents_.begin(), agents_.end(), [&](const auto& item) { return item.path == agent_path; });
        if (agent != agents_.end()) open_agent(static_cast<int>(agent - agents_.begin()));
        const auto note = std::find_if(notes_.begin(), notes_.end(), [&](const auto& item) { return item.path == note_path; });
        if (note != notes_.end()) open_note(static_cast<int>(note - notes_.begin()));
        refresh_composer_index();
        refresh_checkpoint_history(checkpoint_id);
        set_status("Task restored · source files returned to their pre-run state");
    } catch (const std::exception& error) {
        set_status(error.what());
        Rmessage_box(window_, "Checkpoint not restored", error.what(), R_MESSAGE_OK);
    }
}

void App::restore_checkpoint_hunk() {
    if (lead_process_.running() || wiki_process_.running()) {
        set_status("Finish the active run before restoring a hunk");
        return;
    }
    const auto index = Rcombo_box_get_selected(id("checkpoint.hunks"));
    if (selected_checkpoint_id_.empty() || index < 0 || index >= static_cast<int>(checkpoint_hunk_ids_.size())) {
        set_status("Select a diff hunk to restore");
        return;
    }
    if (!can_discard_code() || !flush_autosave()) return;
    const auto hunk_id = checkpoint_hunk_ids_[index];
    if (Rmessage_box(window_, "Restore selected hunk", "Restore only this hunk? Its file must still match the post-run state.", R_MESSAGE_YES_NO) != R_MESSAGE_RESULT_YES) return;
    try {
        checkpoint_store_.restore_hunks(selected_checkpoint_id_, {hunk_id});
        const auto path = hunk_id.substr(0, hunk_id.rfind('#'));
        if (!current_code_path_.empty() && current_code_path_ == workspace_.root() / path && std::filesystem::is_regular_file(current_code_path_)) {
            open_code_file(current_code_path_);
        }
        refresh_checkpoint_history(selected_checkpoint_id_);
        set_status("Hunk restored · " + path);
    } catch (const std::exception& error) {
        set_status(error.what());
        Rmessage_box(window_, "Hunk not restored", error.what(), R_MESSAGE_OK);
    }
}

void App::open_workspace() {
    RFileDialogDesc desc{};
    desc.parent = window_;
    desc.title = "Open a workspace root";
    desc.directory = workspace_.root().c_str();
    desc.flags = R_FILE_DIALOG_SHOW_HIDDEN;
    const char* selected = Rfile_dialog_select_folder(&desc);
    if (!selected) return;
    active_mission_id_.clear();
    selected_mission_id_.clear();
    open_workspace_path(selected);
}

void App::open_workspace_path(const std::filesystem::path& path) {
    if (!can_discard_code() || !flush_autosave()) return;
    try {
        pump_recovery();
        save_thread();
        lead_process_.stop();
        finish_checkpoint("stopped");
        wiki_process_.stop();
        save_profile();
        // A proposal targets the old workspace's wiki; the new one reloads its own staged writes.
        proposal_.reset();
        workspace_ = Workspace(path);
        workspace_.initialize();
        selected_checkpoint_id_.clear();
        current_agent_ = current_note_ = -1;
        current_code_path_.clear();
        Rwidget_set_enabled(id("code.save"), R_FALSE);
        current_code_modified_ = current_agent_modified_ = current_note_modified_ = false;
        loading_code_ = loading_agent_ = loading_note_ = false;
        code_autosave_pending_ = agent_autosave_pending_ = note_autosave_pending_ = false;
        Rtext_editor_set_text(id("code.editor"), "");
        Rlabel_set_text(id("code.title"), "No file open");
        Rlabel_set_text(id("code.path"), "Select a file in FILES");
        recall_store_ = RecallStore(workspace_.root());
        refresh_workspace();
        seed_recall();
        tesseract_.open(workspace_.root() / ".stellar" / "tesseract.json");
        Rwidget_invalidate(id("tesseract.canvas"));
        refresh_tesseract_chrome();
        load_profile();
        apply_profile();
        reset_session();
        if (const auto* latest = thread_store_.latest()) open_thread(latest->id);
        open_agent(0);
        open_note(0);
        Rwindow_set_title(window_, ("Stellar — " + workspace_.name()).c_str());
        set_status("Opened " + workspace_.root().string());
    } catch (const std::exception& error) {
        Rmessage_box(window_, "Cannot open workspace", error.what(), R_MESSAGE_OK);
    }
}

void App::refresh_missions() {
    if (active_mission_id_.empty() && mission_store_.workspace() != workspace_.root()) {
        mission_store_ = GitMissionStore(workspace_.root());
    }
    mission_store_.load();
    loading_missions_ = true;
    Rcombo_box_clear_items(id("mission.repo"));
    mission_repository_rows_.clear();
    for (const auto& repository : repositories_) {
        mission_repository_rows_.push_back(repository.string());
        const auto label = repository == workspace_.root() ? workspace_.name() + " (root)" : repository.filename().string();
        Rcombo_box_add_item(id("mission.repo"), label.c_str());
    }
    Rcombo_box_set_selected(id("mission.repo"), repositories_.empty() ? -1 : 0);
    Rwidget_set_visibility(id("mission.return"), active_mission_id_.empty() ? R_COLLAPSED : R_VISIBLE);
    if (!active_mission_id_.empty()) {
        if (const auto* active = mission_store_.find(active_mission_id_)) {
            Rlabel_set_text(id("mission.summary"), ("Working in an isolated worktree · " + active->title).c_str());
        }
    } else {
        Rlabel_set_text(id("mission.summary"), "Create a Git worktree, then review exactly what returns to this workspace.");
    }

    Rlist_view_clear_items(id("mission.list"));
    mission_rows_.clear();
    int selected = -1;
    for (const auto& mission : mission_store_.missions()) {
        if (mission.id == selected_mission_id_) selected = static_cast<int>(mission_rows_.size());
        mission_rows_.push_back(mission.id);
        Rlist_view_add_item(id("mission.list"), (mission.status + " · " + mission.title).c_str());
    }
    Rlist_view_set_selected(id("mission.list"), selected);
    const bool running = lead_process_.running() || wiki_process_.running();
    const auto* mission = mission_store_.find(selected_mission_id_);
    Rlabel_set_text(id("mission.empty"), repositories_.empty()
        ? "No Git repository found. Missions require the workspace root or a direct child Git repository."
        : mission_rows_.empty() ? "No isolated tasks yet. Use one for risky or exploratory changes: the agent works in a separate Git worktree, and nothing reaches this workspace until you review the diff and apply it."
        : "Select a mission to inspect its diff and available actions.");
    Rwidget_set_visibility(id("mission.empty"), mission ? R_COLLAPSED : R_VISIBLE);
    Rwidget_set_visibility(id("mission.list"), mission_rows_.empty() ? R_COLLAPSED : R_VISIBLE);
    Rwidget_set_visibility(id("mission.hunks.row"), mission ? R_VISIBLE : R_COLLAPSED);
    Rwidget_set_visibility(id("mission.actions.row"), mission ? R_VISIBLE : R_COLLAPSED);
    Rwidget_set_visibility(id("mission.diff"), mission ? R_VISIBLE : R_COLLAPSED);
    Rwidget_set_enabled(id("mission.repo"), active_mission_id_.empty() && !repositories_.empty());
    Rwidget_set_enabled(id("mission.title"), active_mission_id_.empty() && !repositories_.empty());
    Rwidget_set_enabled(id("mission.create"), active_mission_id_.empty() && !repositories_.empty() && !running && run_mode_ == RunMode::Build);
    Rwidget_set_enabled(id("mission.review"), mission && !running);
    Rwidget_set_enabled(id("mission.apply"), mission && mission->status != "applied" && !running);
    Rwidget_set_enabled(id("mission.apply_hunk"), mission && mission->status != "applied" && !running && !mission_hunk_ids_.empty());
    Rwidget_set_enabled(id("mission.discard"), mission && !running);
    if (!mission) {
        Rtext_editor_set_text(id("mission.diff"), "");
        Rcombo_box_clear_items(id("mission.hunks"));
        mission_hunk_ids_.clear();
    }
    loading_missions_ = false;
}

void App::create_mission() {
    if (!active_mission_id_.empty()) {
        set_status("Finish or return from the current mission first");
        return;
    }
    if (lead_process_.running() || wiki_process_.running()) {
        set_status("Finish the current run before creating a mission");
        return;
    }
    if (run_mode_ != RunMode::Build) {
        set_status("Isolated missions require Build mode");
        return;
    }
    const auto repository = Rcombo_box_get_selected(id("mission.repo"));
    if (repository < 0 || repository >= static_cast<int>(mission_repository_rows_.size())) {
        set_status("Select a Git repository for the mission");
        return;
    }
    try {
        const auto mission = mission_store_.create(mission_repository_rows_[repository], Rtext_input_get_text(id("mission.title")));
        active_mission_id_ = mission.id;
        selected_mission_id_ = mission.id;
        Rtext_input_set_text(id("mission.title"), "");
        open_workspace_path(mission.worktree);
        if (workspace_.root() != mission.worktree) {
            active_mission_id_.clear();
            return;
        }
        start_with_prompt(mission.title);
    } catch (const std::exception& error) {
        active_mission_id_.clear();
        set_status(error.what());
        Rmessage_box(window_, "Mission not created", error.what(), R_MESSAGE_OK);
    }
}

void App::review_mission(const std::string& mission_id) {
    if (lead_process_.running() || wiki_process_.running()) {
        set_status("Finish active work before reviewing a mission diff");
        return;
    }
    const auto* mission = mission_store_.find(mission_id);
    if (!mission) return;
    try {
        selected_mission_id_ = mission_id;
        Rlabel_set_text(id("mission.summary"), (mission->title + " · " + mission->status).c_str());
        Rtext_editor_set_text(id("mission.diff"), mission_store_.diff(mission_id).c_str());
        Rcombo_box_clear_items(id("mission.hunks"));
        mission_hunk_ids_.clear();
        for (const auto& file : mission_store_.hunks(mission_id)) {
            for (const auto& hunk : file.hunks) {
                mission_hunk_ids_.push_back(hunk.id);
                Rcombo_box_add_item(id("mission.hunks"), hunk_label(hunk).c_str());
            }
        }
        Rcombo_box_set_selected(id("mission.hunks"), mission_hunk_ids_.empty() ? -1 : 0);
        set_active_view(id("more.tabs"), 3);
        set_active_view(id("center.tabs"), 3);
        refresh_missions();
    } catch (const std::exception& error) {
        set_status(error.what());
        Rmessage_box(window_, "Cannot review mission", error.what(), R_MESSAGE_OK);
    }
}

void App::apply_mission() {
    if (lead_process_.running() || wiki_process_.running()) {
        set_status("Finish active work before applying a mission");
        return;
    }
    const auto* selected = mission_store_.find(selected_mission_id_);
    if (!selected) {
        set_status("Select a mission to apply");
        return;
    }
    const auto mission = *selected;
    if (mission.status == "applied") {
        set_status("This mission has already been applied");
        return;
    }
    const auto message = "Apply the complete diff for “" + mission.title + "” to " + mission.repository.string() +
        "? Conflicting current changes will stop the apply without modifying the workspace.";
    if (Rmessage_box(window_, "Apply mission", message.c_str(), R_MESSAGE_YES_NO) != R_MESSAGE_RESULT_YES) return;
    try {
        mission_store_.apply(mission.id);
        active_mission_id_.clear();
        open_workspace_path(mission.repository);
        selected_mission_id_ = mission.id;
        refresh_missions();
        set_status("Mission applied to the original workspace · no commit or merge was created");
        notify_event("Stellar · Mission applied", mission.title, false, "mission", "mission", mission.id);
    } catch (const std::exception& error) {
        set_status(error.what());
        Rmessage_box(window_, "Mission not applied", error.what(), R_MESSAGE_OK);
    }
}

void App::apply_mission_hunk() {
    if (lead_process_.running() || wiki_process_.running()) {
        set_status("Finish active work before applying a mission hunk");
        return;
    }
    const auto index = Rcombo_box_get_selected(id("mission.hunks"));
    if (selected_mission_id_.empty() || index < 0 || index >= static_cast<int>(mission_hunk_ids_.size())) {
        set_status("Select a mission hunk to apply");
        return;
    }
    const auto hunk_id = mission_hunk_ids_[index];
    if (Rmessage_box(window_, "Apply selected hunk", "Apply only this mission hunk to the original workspace? Conflicts will stop the apply.", R_MESSAGE_YES_NO) != R_MESSAGE_RESULT_YES) return;
    try {
        mission_store_.apply_hunks(selected_mission_id_, {hunk_id});
        review_mission(selected_mission_id_);
        set_status("Mission hunk applied · remaining hunks are still isolated");
    } catch (const std::exception& error) {
        set_status(error.what());
        Rmessage_box(window_, "Mission hunk not applied", error.what(), R_MESSAGE_OK);
    }
}

void App::discard_mission() {
    if (lead_process_.running() || wiki_process_.running()) {
        set_status("Finish active work before discarding a mission");
        return;
    }
    const auto* selected = mission_store_.find(selected_mission_id_);
    if (!selected) {
        set_status("Select a mission to discard");
        return;
    }
    const auto mission = *selected;
    const auto message = "Permanently discard “" + mission.title + "”, its worktree, and every unapplied change? This cannot be undone.";
    if (Rmessage_box(window_, "Discard mission", message.c_str(), R_MESSAGE_YES_NO) != R_MESSAGE_RESULT_YES) return;
    try {
        if (active_mission_id_ == mission.id) {
            active_mission_id_.clear();
            open_workspace_path(mission.repository);
        }
        mission_store_.discard(mission.id);
        selected_mission_id_.clear();
        Rtext_editor_set_text(id("mission.diff"), "");
        refresh_missions();
        set_status("Mission discarded");
    } catch (const std::exception& error) {
        set_status(error.what());
        Rmessage_box(window_, "Mission not discarded", error.what(), R_MESSAGE_OK);
    }
}

void App::return_from_mission() {
    if (lead_process_.running() || wiki_process_.running()) {
        set_status("Finish active work before returning to the original workspace");
        return;
    }
    const auto* active = mission_store_.find(active_mission_id_);
    if (!active) return;
    const auto mission = *active;
    active_mission_id_.clear();
    selected_mission_id_ = mission.id;
    open_workspace_path(mission.repository);
    set_status("Returned to the original workspace · mission changes remain isolated");
}

void App::open_code_file(const std::filesystem::path& path, int line) {
    if (!can_discard_code()) return;
    try {
        const auto root = std::filesystem::weakly_canonical(workspace_.root());
        const auto candidate = std::filesystem::weakly_canonical(path.is_absolute() ? path : root / path);
        if (candidate != root && std::mismatch(root.begin(), root.end(), candidate.begin()).first != root.end()) {
            throw std::runtime_error("File is outside the workspace");
        }
        if (!std::filesystem::is_regular_file(candidate)) throw std::runtime_error("Not a regular file");
        if (std::filesystem::file_size(candidate) > 2 * 1024 * 1024) throw std::runtime_error("File exceeds the 2 MiB editor limit");
        const auto content = read_file(candidate);
        if (content.find('\0') != std::string::npos) throw std::runtime_error("Binary files are not supported by the editor");

        current_code_path_ = candidate;
        Rwidget_set_enabled(id("code.save"), R_TRUE);
        loading_code_ = true;
        Rtext_editor_set_text(id("code.editor"), content.c_str());
        Rtext_editor_set_language(id("code.editor"), Rlanguage_for_file(candidate.c_str()));
        loading_code_ = false;
        current_code_modified_ = false;
        code_autosave_pending_ = false;
        Rlabel_set_text(id("code.title"), candidate.filename().c_str());
        Rlabel_set_text(id("code.path"), std::filesystem::relative(candidate, root).c_str());
        set_active_view(id("center.tabs"), 1);
        Rwidget_focus(id("code.editor"));
        if (line > 0) {
            Rtext_editor_set_cursor(id("code.editor"), line - 1, 0);
            Rtext_editor_scroll_to_line(id("code.editor"), line - 1);
        }
        set_status("Opened " + candidate.string() + (line > 0 ? ":" + std::to_string(line) : ""));
    } catch (const std::exception& error) {
        loading_code_ = false;
        set_status(error.what());
        Rmessage_box(window_, "Cannot open file", error.what(), R_MESSAGE_OK);
    }
}

bool App::save_code_file(bool automatic) {
    if (current_code_path_.empty()) {
        if (!automatic) set_status("Open a source file before saving");
        return false;
    }
    if (automatic && !current_code_modified_) {
        code_autosave_pending_ = false;
        return true;
    }
    try {
        write_file(current_code_path_, Rtext_editor_get_text(id("code.editor")));
        recovery_store_.clear_editor("code", current_code_path_);
        current_code_modified_ = false;
        code_autosave_pending_ = false;
        Rlabel_set_text(id("code.path"), std::filesystem::relative(current_code_path_, workspace_.root()).c_str());
        set_status((automatic ? "Autosaved " : "Saved ") + current_code_path_.string());
        if (automatic) schedule_watch();
        return true;
    } catch (const std::exception& error) {
        if (automatic) {
            code_autosave_pending_ = true;
            code_autosave_due_ = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        }
        set_status(std::string(automatic ? "Autosave failed · " : "") + error.what());
        if (automatic) notify_autosave_error(error.what());
        return false;
    }
}

void App::code_modified() {
    if (loading_code_ || current_code_path_.empty()) return;
    current_code_modified_ = true;
    code_autosave_pending_ = true;
    code_autosave_due_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(900);
    recovery_store_.set_editor("code", current_code_path_, Rtext_editor_get_text(id("code.editor")), read_file(current_code_path_));
    schedule_recovery_save();
    Rlabel_set_text(id("code.path"), (std::filesystem::relative(current_code_path_, workspace_.root()).string() + " •").c_str());
}

void App::pump_autosave() {
    const auto now = std::chrono::steady_clock::now();
    if (code_autosave_pending_ && now >= code_autosave_due_) save_code_file(true);
    if (agent_autosave_pending_ && now >= agent_autosave_due_) save_agent(true);
    if (note_autosave_pending_ && now >= note_autosave_due_) save_note(true);
}

bool App::flush_autosave() {
    const bool code = !current_code_modified_ || save_code_file(true);
    const bool agent = !current_agent_modified_ || save_agent(true);
    const bool note = !current_note_modified_ || save_note(true);
    return code && agent && note;
}

bool App::can_discard_code() {
    if (!current_code_modified_ || save_code_file(true)) return true;
    return Rmessage_box(window_, "Unsaved file changes", "Discard the changes in the current code file?", R_MESSAGE_YES_NO)
        == R_MESSAGE_RESULT_YES;
}

void App::file_accepted(Rid browser, const char* path, void* user) {
    (void)browser;
    static_cast<App*>(user)->open_code_file(path);
}

void App::open_agent(int index) {
    if (loading_agent_ || index < 0 || index >= static_cast<int>(agents_.size())) return;
    if (current_agent_modified_ && !save_agent(true)) return;
    loading_agent_ = true;
    current_agent_ = index;
    const auto& agent = agents_[index];
    Rlabel_set_text(id("agent.title"), agent.name.c_str());
    Rlabel_set_text(id("agent.path"), std::filesystem::relative(agent.path, workspace_.root()).c_str());
    Rtext_editor_set_text(id("agent.editor"), agent.content.c_str());
    const auto row = std::find(agent_rows_.begin(), agent_rows_.end(), index);
    Rlist_view_set_selected(id("agents"), row == agent_rows_.end() ? -1 : static_cast<int>(row - agent_rows_.begin()));
    const auto filename = agent.path.filename();
    Rwidget_set_enabled(id("agent.delete"), !builtin_agent(filename));
    Rwidget_set_tooltip(id("agent.delete"), builtin_agent(filename)
        ? "Built-in agents can be edited but not deleted" : "Permanently delete this custom agent");
    current_agent_modified_ = false;
    agent_autosave_pending_ = false;
    loading_agent_ = false;
}

bool App::save_agent(bool automatic) {
    if (current_agent_ < 0 || current_agent_ >= static_cast<int>(agents_.size())) {
        if (!automatic) set_status("Select an agent to save");
        return false;
    }
    if (automatic && !current_agent_modified_) {
        agent_autosave_pending_ = false;
        return true;
    }
    try {
        const auto path = agents_[current_agent_].path;
        const std::string content = Rtext_editor_get_text(id("agent.editor"));
        workspace_.save_agent(agents_[current_agent_], content);
        recovery_store_.clear_editor("agent", path);
        current_agent_modified_ = false;
        agent_autosave_pending_ = false;
        loading_agent_ = true;
        agents_ = workspace_.agents();
        refresh_lists();
        const auto found = std::find_if(agents_.begin(), agents_.end(), [&](const auto& agent) { return agent.path == path; });
        current_agent_ = found == agents_.end() ? -1 : static_cast<int>(found - agents_.begin());
        if (current_agent_ >= 0) {
            const auto& agent = agents_[current_agent_];
            Rlabel_set_text(id("agent.title"), agent.name.c_str());
            Rlabel_set_text(id("agent.path"), std::filesystem::relative(agent.path, workspace_.root()).c_str());
            const auto row = std::find(agent_rows_.begin(), agent_rows_.end(), current_agent_);
            Rlist_view_set_selected(id("agents"), row == agent_rows_.end() ? -1 : static_cast<int>(row - agent_rows_.begin()));
        }
        loading_agent_ = false;
        set_status(automatic ? "Agent autosaved" : "Agent saved · both harnesses read .agents/agents on the next run");
        if (automatic) schedule_watch();
        return true;
    } catch (const std::exception& error) {
        loading_agent_ = false;
        if (automatic) {
            agent_autosave_pending_ = true;
            agent_autosave_due_ = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        }
        set_status(std::string(automatic ? "Autosave failed · " : "") + error.what());
        if (automatic) notify_autosave_error(error.what());
        return false;
    }
}

void App::agent_modified() {
    if (loading_agent_ || current_agent_ < 0 || current_agent_ >= static_cast<int>(agents_.size())) return;
    current_agent_modified_ = true;
    agent_autosave_pending_ = true;
    agent_autosave_due_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(900);
    recovery_store_.set_editor("agent", agents_[current_agent_].path, Rtext_editor_get_text(id("agent.editor")), agents_[current_agent_].content);
    schedule_recovery_save();
    Rlabel_set_text(id("agent.path"), (std::filesystem::relative(agents_[current_agent_].path, workspace_.root()).string() + " •").c_str());
}

void App::delete_agent() {
    if (current_agent_ < 0 || current_agent_ >= static_cast<int>(agents_.size())) {
        set_status("Select an agent to delete");
        return;
    }
    const auto agent = agents_[current_agent_];
    const auto filename = agent.path.filename();
    if (builtin_agent(filename)) {
        set_status("Built-in agents cannot be deleted");
        return;
    }
    const auto message = "Permanently delete .agents/agents/" + filename.string() + "? This cannot be undone.";
    if (Rmessage_box(window_, "Delete agent", message.c_str(), R_MESSAGE_YES_NO) != R_MESSAGE_RESULT_YES) return;
    try {
        const auto deleted_index = current_agent_;
        workspace_.delete_agent(agent.path);
        agents_ = workspace_.agents();
        current_agent_ = -1;
        current_agent_modified_ = false;
        agent_autosave_pending_ = false;
        refresh_lists();
        if (agents_.empty()) {
            Rlabel_set_text(id("agent.title"), "Select an agent");
            Rlabel_set_text(id("agent.path"), ".agents/agents/*.md");
            Rtext_editor_set_text(id("agent.editor"), "");
            Rwidget_set_enabled(id("agent.delete"), R_FALSE);
        } else {
            open_agent(std::min(deleted_index, static_cast<int>(agents_.size()) - 1));
        }
        set_status("Deleted " + filename.string());
    } catch (const std::exception& error) {
        set_status(error.what());
    }
}

void App::new_agent() {
    if (!flush_autosave()) return;
    try {
        std::string name = "agent";
        int suffix = 2;
        while (std::filesystem::exists(workspace_.agents_root() / (name + ".md"))) name = "agent-" + std::to_string(suffix++);
        auto agent = workspace_.create_agent(name);
        agents_ = workspace_.agents();
        refresh_lists();
        const auto found = std::find_if(agents_.begin(), agents_.end(), [&](const auto& item) { return item.path == agent.path; });
        open_agent(static_cast<int>(found - agents_.begin()));
        set_active_view(id("more.tabs"), 0);
        set_active_view(id("center.tabs"), 3);
        Rwidget_focus(id("agent.editor"));
        set_status("Created " + agent.path.filename().string() + " · describe the agent · changes autosave");
    } catch (const std::exception& error) {
        set_status(error.what());
    }
}

void App::open_note(int index) {
    if (loading_note_ || index < 0 || index >= static_cast<int>(notes_.size())) return;
    if (current_note_modified_ && !save_note(true)) return;
    loading_note_ = true;
    current_note_ = index;
    const auto& note = notes_[index];
    Rlabel_set_text(id("note.title"), note.title.c_str());
    Rlabel_set_text(id("note.path"), std::filesystem::relative(note.path, workspace_.root()).c_str());
    Rtext_editor_set_text(id("note.editor"), note.content.c_str());
    const auto row = std::find(note_rows_.begin(), note_rows_.end(), index);
    Rlist_view_set_selected(id("wiki"), row == note_rows_.end() ? -1 : static_cast<int>(row - note_rows_.begin()));
    current_note_modified_ = false;
    note_autosave_pending_ = false;
    loading_note_ = false;
    update_preview();
}

void App::refresh_knowledge() {
    knowledge_records_ = workspace_.knowledge().entities();
    const auto relations = workspace_.knowledge().relations();
    memory_.set_data(knowledge_records_, relations);
    refresh_memory_details();
    Rwidget_invalidate(id("memory.canvas"));
    knowledge_records_.insert(knowledge_records_.end(), relations.begin(), relations.end());
    const auto query = lowercase(Rtext_input_get_text(id("knowledge.search")));
    if (!query.empty()) {
        knowledge_records_.erase(std::remove_if(knowledge_records_.begin(), knowledge_records_.end(), [&](const auto& record) {
            return lowercase(KnowledgeGraph::to_json(record)).find(query) == std::string::npos;
        }), knowledge_records_.end());
    }
    current_knowledge_ = -1;
    Rwidget_set_enabled(id("knowledge.forget"), R_FALSE);
    Rlist_view_clear_items(id("knowledge.results"));
    for (const auto& record : knowledge_records_) {
        // The inspector column is narrow and list rows clip without an ellipsis: name first, then
        // the type, shortened to fit. Full details appear when the record is selected.
        const auto text = record.kind == GraphOpKind::UpsertEntity
            ? shortened(record.name, 26) + " · " + record.entity_type
            : shortened(record.source_id + " → " + record.target_id, 26) + " · " + record.entity_type;
        Rlist_view_add_item(id("knowledge.results"), text.c_str());
    }
    const auto stats = workspace_.knowledge().stats();
    // Episodes are stored as entities; both views count them once, as episodes.
    const auto entities = stats.entities - std::min(stats.entities, stats.episodes);
    const auto entity_count = std::to_string(entities) + (entities == 1 ? " entity" : " entities");
    const auto counts = counted(stats.episodes, "episode") + " · " + entity_count + " · " + counted(stats.relations, "relation");
    Rlabel_set_text(id("knowledge.stats"), (counts + (stats.staged > 0 ? " · " + std::to_string(stats.staged) + " staged" : "")).c_str());
    Rlabel_set_text(id("memory.stats"), counts.c_str());
    Rlabel_set_text(id("knowledge.detail"), knowledge_records_.empty()
        ? "No matching graph records. Queue a fact with `raven-ide-knowledge propose`."
        : "Select an entity or relation to inspect its provenance and symbolic context.");
}

void App::open_memory() {
    dismiss_splash();
    refresh_knowledge();
    set_sidebar_visibility(false, false);
    set_active_view(id("more.tabs"), 2);
    set_active_view(id("center.tabs"), 3);
    Rwidget_focus(id("memory.canvas"));
    set_status("Memory Universe · reviewed episodes, entities, and relations");
}

void App::open_tesseract() {
    dismiss_splash();
    set_sidebar_visibility(false, false);
    set_active_view(id("center.tabs"), 5);
    if (tesseract_.reload_if_changed()) Rwidget_invalidate(id("tesseract.canvas"));
    tesseract_.fit();
    Rwidget_invalidate(id("tesseract.canvas"));
    refresh_tesseract_chrome();
    Rwidget_focus(id("tesseract.composer"));
    set_status("Tesseract · describe an interface and watch it take shape");
}

void App::start_tesseract_with_prompt(const std::string& prompt) {
    open_tesseract();
    Rtext_editor_set_text(id("tesseract.composer"), prompt.c_str());
    start_run();
}

void App::begin_tesseract_text_edit() {
    const auto* selected = tesseract_.find(tesseract_.selected_id());
    if (!selected || selected->kind != TesseractNodeKind::Text) return;
    const auto editor = id("tesseract.inline.editor");
    const auto bounds = tesseract_.screen_bounds(selected->id);
    Rslot_set_position(editor, bounds.x, bounds.y);
    Rslot_set_size(editor, std::clamp(bounds.width, 120.0f, 520.0f), std::clamp(bounds.height, 34.0f, 120.0f));
    Rtext_input_set_text(editor, selected->content.c_str());
    Rwidget_set_visibility(editor, R_VISIBLE);
    Rwidget_focus(editor);
    Rtext_input_select_all(editor);
}

void App::finish_tesseract_text_edit(bool commit) {
    const auto editor = id("tesseract.inline.editor");
    if (Rwidget_get_visibility(editor) != R_VISIBLE) return;
    const std::string text = Rtext_input_get_text(editor);
    Rwidget_set_visibility(editor, R_COLLAPSED);
    if (commit) {
        set_status(tesseract_.update_selected_text(text) ? "Text updated · .stellar/tesseract.json saved" : "Text could not be saved");
    } else {
        set_status("Text edit cancelled");
    }
    Rwidget_invalidate(id("tesseract.canvas"));
    Rwidget_focus(id("tesseract.canvas"));
    refresh_tesseract_chrome();
}

void App::refresh_tesseract_chrome() {
    Rlabel_set_text(id("tesseract.zoom"), (std::to_string(static_cast<int>(tesseract_.zoom_percent() + 0.5f)) + "%").c_str());
    const auto* selected = tesseract_.find(tesseract_.selected_id());
    const bool queued = queued_tesseract_ && !queued_prompt_.empty();
    const bool busy = lead_process_.running() || wiki_process_.running();
    const bool activity = run_tesseract_ && !last_prompt_.empty();
    Rwidget_set_accessible_name(id("tesseract.send"), busy ? "Queue a UI refinement" : selected ? "Refine selected element with agent" : "Create with agent");
    Rwidget_set_tooltip(id("tesseract.send"), busy ? "Queue a UI refinement (Enter)" : selected ? "Refine selected element with agent (Enter)" : "Create with agent (Enter)");
    const auto inline_editor = id("tesseract.inline.editor");
    if (Rwidget_get_visibility(inline_editor) == R_VISIBLE) {
        if (!selected || selected->kind != TesseractNodeKind::Text) {
            Rwidget_set_visibility(inline_editor, R_COLLAPSED);
        } else {
            const auto bounds = tesseract_.screen_bounds(selected->id);
            Rslot_set_position(inline_editor, bounds.x, bounds.y);
            Rslot_set_size(inline_editor, std::clamp(bounds.width, 120.0f, 520.0f), std::clamp(bounds.height, 34.0f, 120.0f));
        }
    }
    Rwidget_set_visibility(id("tesseract.activity.bar"), activity ? R_VISIBLE : R_COLLAPSED);
    if (activity) {
        std::string state = "Run complete";
        if (last_run_failed_) state = "Creation failed";
        else if (current_action_ == "Stopped") state = "Creation stopped";
        else if (wiki_process_.running()) state = "Reviewing the result";
        else if (lead_process_.running()) state = "Creating your interface";
        else if (!tesseract_.empty()) state = "Interface ready";
        std::string action = current_action_;
        if (!busy && !last_run_failed_ && current_action_ != "Stopped" && !tesseract_.empty()) {
            const auto frames = tesseract_.roots().size();
            action = std::to_string(frames) + (frames == 1 ? " frame" : " frames") + " live · .stellar/tesseract.json";
        }
        auto prompt = last_prompt_;
        std::replace(prompt.begin(), prompt.end(), '\n', ' ');
        std::replace(prompt.begin(), prompt.end(), '\r', ' ');
        std::replace(action.begin(), action.end(), '\n', ' ');
        std::replace(action.begin(), action.end(), '\r', ' ');
        Rlabel_set_text(id("tesseract.activity.state"), state.c_str());
        Rlabel_set_text(id("tesseract.activity.action"), shortened(action, 88).c_str());
        Rlabel_set_text(id("tesseract.activity.prompt"), ("“" + shortened(prompt, 96) + "”").c_str());
        Rwidget_set_tooltip(id("tesseract.activity.action"), action.c_str());
        Rwidget_set_tooltip(id("tesseract.activity.prompt"), prompt.c_str());
    }
    const auto composer = id("tesseract.composer");
    const float editor_height = composer_editor_height(composer);
    Rwidget_set_size(composer, -1.0f, editor_height);
    const float composer_height = 120.0f + editor_height - 48.0f + (activity ? 60.0f : 0.0f) +
        (selected ? 26.0f : 0.0f) + (queued ? 38.0f : 0.0f) + (tesseract_image_ ? 76.0f : 0.0f);
    Rslot_set_size(id("tesseract.composer.shell"), 0.0f, composer_height);
    Rslot_set_position(id("tesseract.composer.shell"), 0.0f, -composer_height - 22.0f);
    Rwidget_set_visibility(id("tesseract.selection.bar"), selected ? R_VISIBLE : R_COLLAPSED);
    if (selected) {
        Rlabel_set_text(id("tesseract.selection"), ("Selected " + selected->name).c_str());
        Rwidget_set_tooltip(id("tesseract.selection"), (selected->id + " · Drag to move · Bottom-right handle to resize").c_str());
        Rtext_editor_set_placeholder(id("tesseract.composer"), "Refine this element…");
    } else {
        Rtext_editor_set_placeholder(id("tesseract.composer"), "Describe an interface…");
    }
}

void App::refresh_memory_details() {
    const auto markdown = memory_.selected_markdown();
    Rlabel_set_text(id("memory.detail"), markdown.c_str());
    Rwidget_set_visibility(id("memory.details"), markdown.empty() ? R_COLLAPSED : R_VISIBLE);
    Rwidget_set_enabled(id("memory.forget"), !memory_.selected_id().empty());
}

void App::open_knowledge(int index) {
    if (index < 0 || index >= static_cast<int>(knowledge_records_.size())) return;
    current_knowledge_ = index;
    const auto& record = knowledge_records_[index];
    const auto context = workspace_.knowledge_context(record.id, vector_recall_);
    const auto markdown = "### " + record.id + "\n\n`" + record.entity_type + "` · confidence `" +
        std::to_string(record.confidence).substr(0, 4) + "`\n\n```json\n" +
        KnowledgeGraph::to_json(record, 2) + "\n```\n\n### Symbolic context\n\n```text\n" + context + "```";
    Rlabel_set_text(id("knowledge.detail"), markdown.c_str());
    Rlist_view_set_selected(id("knowledge.results"), index);
    Rwidget_set_enabled(id("knowledge.forget"), R_TRUE);
}

void App::forget_memory() {
    const bool universe = active_view(id("center.tabs")) == 3 && active_view(id("more.tabs")) == 2;
    const auto memory_id = universe ? memory_.selected_id()
        : current_knowledge_ >= 0 && current_knowledge_ < static_cast<int>(knowledge_records_.size())
            ? knowledge_records_[current_knowledge_].id : std::string{};
    if (memory_id.empty()) {
        set_status("Select an accepted memory or graph record first");
        return;
    }
    const auto message = "Forget “" + memory_id + "” and every relation touching it? This deletes accepted data from wiki/graph.jsonl and cannot be undone.";
    if (Rmessage_box(window_, "Forget accepted memory", message.c_str(), R_MESSAGE_YES_NO) != R_MESSAGE_RESULT_YES) return;
    try {
        if (!workspace_.knowledge().forget(memory_id)) {
            set_status("Memory record was not found");
            return;
        }
        refresh_knowledge();
        set_status("Accepted memory forgotten · graph.jsonl updated");
    } catch (const std::exception& error) {
        set_status(error.what());
        Rmessage_box(window_, "Memory not forgotten", error.what(), R_MESSAGE_OK);
    }
}

bool App::save_note(bool automatic) {
    if (current_note_ < 0 || current_note_ >= static_cast<int>(notes_.size())) {
        if (!automatic) set_status("Select a wiki note to save");
        return false;
    }
    if (automatic && !current_note_modified_) {
        note_autosave_pending_ = false;
        return true;
    }
    try {
        const auto path = notes_[current_note_].path;
        const std::string content = Rtext_editor_get_text(id("note.editor"));
        workspace_.save_note(notes_[current_note_], content);
        recovery_store_.clear_editor("note", path);
        current_note_modified_ = false;
        note_autosave_pending_ = false;
        loading_note_ = true;
        notes_ = workspace_.wiki_notes();
        refresh_lists();
        const auto found = std::find_if(notes_.begin(), notes_.end(), [&](const auto& note) { return note.path == path; });
        current_note_ = found == notes_.end() ? -1 : static_cast<int>(found - notes_.begin());
        if (current_note_ >= 0) {
            const auto& note = notes_[current_note_];
            Rlabel_set_text(id("note.title"), note.title.c_str());
            Rlabel_set_text(id("note.path"), std::filesystem::relative(note.path, workspace_.root()).c_str());
            const auto row = std::find(note_rows_.begin(), note_rows_.end(), current_note_);
            Rlist_view_set_selected(id("wiki"), row == note_rows_.end() ? -1 : static_cast<int>(row - note_rows_.begin()));
        }
        loading_note_ = false;
        update_preview();
        set_status(automatic ? "Wiki note autosaved" : "Wiki note saved");
        if (automatic) schedule_watch();
        return true;
    } catch (const std::exception& error) {
        loading_note_ = false;
        if (automatic) {
            note_autosave_pending_ = true;
            note_autosave_due_ = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        }
        set_status(std::string(automatic ? "Autosave failed · " : "") + error.what());
        if (automatic) notify_autosave_error(error.what());
        return false;
    }
}

void App::note_modified() {
    if (loading_note_ || current_note_ < 0 || current_note_ >= static_cast<int>(notes_.size())) return;
    current_note_modified_ = true;
    note_autosave_pending_ = true;
    note_autosave_due_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(900);
    recovery_store_.set_editor("note", notes_[current_note_].path, Rtext_editor_get_text(id("note.editor")), notes_[current_note_].content);
    schedule_recovery_save();
    Rlabel_set_text(id("note.path"), (std::filesystem::relative(notes_[current_note_].path, workspace_.root()).string() + " •").c_str());
}

void App::new_note() {
    if (!flush_autosave()) return;
    try {
        std::string title = "Untitled";
        int suffix = 2;
        while (std::filesystem::exists(workspace_.wiki_root() / (title + ".md"))) title = "Untitled-" + std::to_string(suffix++);
        auto note = workspace_.create_note(title);
        notes_ = workspace_.wiki_notes();
        refresh_lists();
        const auto found = std::find_if(notes_.begin(), notes_.end(), [&](const auto& item) { return item.path == note.path; });
        open_note(static_cast<int>(found - notes_.begin()));
        set_sidebar_visibility(false, true);
        show_context(1);
        set_active_view(id("wiki.tabs"), 0);
        Rwidget_focus(id("note.editor"));
        set_status("Created " + note.path.filename().string());
    } catch (const std::exception& error) {
        set_status(error.what());
    }
}

void App::update_preview() {
    if (current_note_ < 0 || current_note_ >= static_cast<int>(notes_.size())) return;
    const auto& note = notes_[current_note_];
    const auto markdown = Workspace::expand_wiki_links(Rtext_editor_get_text(id("note.editor")));
    Rlabel_set_text(id("note.preview"), markdown.c_str());
    std::string links = note.links.empty() ? "none" : join(note.links, ", ");
    std::string backlinks = note.backlinks.empty() ? "none" : join(note.backlinks, ", ");
    Rlabel_set_text(id("note.links"), ("Outgoing: " + links + "\nBacklinks: " + backlinks).c_str());
}

void App::open_proposal() {
    if (!proposal_) {
        set_status("There is no knowledge proposal to review");
        return;
    }
    set_sidebar_visibility(false, false);
    render_proposal();
    set_active_view(id("review.tabs"), 1);
    set_active_view(id("center.tabs"), 4);
}

// Graph writes are staged on disk, but a proposal's Markdown edits only lived in memory and were
// lost when Stellar closed. They are saved alongside the staged writes and restored on startup.
void App::save_pending_wiki_edits() {
    const auto path = workspace_.root() / ".stellar" / "pending-wiki-edits.json";
    try {
        if (!proposal_ || proposal_->edits.empty()) {
            std::error_code error;
            std::filesystem::remove(path, error);
            return;
        }
        nlohmann::json edits = nlohmann::json::array();
        for (const auto& edit : proposal_->edits) edits.push_back({{"path", edit.path}, {"content", edit.after}});
        write_file(path, nlohmann::json({{"summary", proposal_->summary}, {"edits", edits}}).dump(2) + "\n");
    } catch (const std::exception& error) {
        set_status(std::string("Pending wiki edits could not be saved: ") + error.what());
    }
}

// Fills Review → Knowledge from the pending proposal. Called whenever the proposal changes, so the
// tab is current however it is reached, not only through the Review button in Activity.
void App::render_proposal() {
    if (!proposal_) {
        Rlabel_set_text(id("proposal.title"), "No pending proposal");
        Rlabel_set_text(id("proposal.files"), "");
        Rtext_editor_set_text(id("proposal.diff"), "");
        return;
    }
    Rlabel_set_text(id("proposal.title"), proposal_->summary.c_str());
    std::string files;
    std::string differences;
    for (const auto& edit : proposal_->edits) {
        if (!files.empty()) files += " · ";
        files += edit.path;
        differences += "FILE: " + edit.path + "\n" + Workspace::diff(edit.before, edit.after) + "\n";
    }
    for (const auto& operation : proposal_->graph) {
        if (!files.empty()) files += " · ";
        files += "graph:" + operation.id;
        differences += "GRAPH OPERATION:\n" + KnowledgeGraph::to_json(operation, 2) + "\n";
    }
    Rlabel_set_text(id("proposal.files"), files.c_str());
    Rtext_editor_set_text(id("proposal.diff"), differences.c_str());
}

void App::accept_proposal() {
    if (!proposal_ || !flush_autosave()) return;
    try {
        workspace_.apply_proposal(*proposal_);
        const auto first_path = proposal_->edits.empty() ? std::filesystem::path{} : workspace_.wiki_root() / proposal_->edits.front().path;
        proposal_.reset();
        Rwidget_set_visibility(id("proposal.bar"), R_COLLAPSED);
        notes_ = workspace_.wiki_notes();
        refresh_lists();
        refresh_knowledge();
        const auto found = std::find_if(notes_.begin(), notes_.end(), [&](const auto& note) { return note.path == first_path; });
        if (found != notes_.end()) {
            open_note(static_cast<int>(found - notes_.begin()));
            set_sidebar_visibility(false, true);
            show_context(1);
        }
        set_active_view(id("center.tabs"), 0);
        set_status("Knowledge proposal accepted and written");
    } catch (const std::exception& error) {
        set_status(error.what());
        Rmessage_box(window_, "Wiki update not applied", error.what(), R_MESSAGE_OK);
    }
}

void App::discard_proposal() {
    if (!proposal_) return;
    proposal_.reset();
    workspace_.knowledge().clear_staged();
    refresh_knowledge();
    Rwidget_set_visibility(id("proposal.bar"), R_COLLAPSED);
    Rlabel_set_text(id("proposal.title"), "No pending proposal");
    Rlabel_set_text(id("proposal.files"), "");
    Rtext_editor_set_text(id("proposal.diff"), "");
    set_active_view(id("center.tabs"), 0);
    set_status("Knowledge proposal discarded · files were not changed");
}

void App::zoom_editors(float amount) {
    editor_zoom_ = amount == 0.0f ? 1.0f : std::clamp(editor_zoom_ + amount, 0.5f, 2.5f);
    static const char* editors[] = {"composer", "tesseract.composer", "code.editor", "agent.editor", "note.editor", "proposal.diff", "checkpoint.diff", "verification.detail"};
    for (const auto* editor : editors) Rtext_editor_set_zoom(id(editor), editor_zoom_);
    const auto percent = std::to_string(static_cast<int>(editor_zoom_ * 100 + 0.5f));
    Rlabel_set_text(id("settings.editor.zoom"), ("Editor zoom · " + percent + "%").c_str());
    save_profile();
    set_status("Editor zoom · " + percent + "%");
}

void App::zoom_interface(float amount) {
    interface_zoom_ = amount == 0.0f ? 1.15f : std::clamp(interface_zoom_ + amount, 0.5f, 2.0f);
    Rset_ui_scale(interface_zoom_);
    update_interface_density();
    const auto percent = std::to_string(static_cast<int>(interface_zoom_ * 100 + 0.5f));
    Rlabel_set_text(id("settings.interface.zoom"), ("Interface zoom · " + percent + "%").c_str());
    save_profile();
    set_status("Interface zoom · " + percent + "%");
}

int App::window_event(Rid window, const REvent* event, void* user) {
    (void)window;
    if (event->type != R_EVENT_KEY_DOWN) return R_FALSE;
    auto& app = *static_cast<App*>(user);
    if (app.splash_.active()) {
        const bool activate = event->key == R_KEY_ENTER || event->key == R_KEY_KP_ENTER || event->key == R_KEY_SPACE;
        if (activate && Rwidget_is_focused(app.id("splash.sound"))) return R_FALSE;
        if (activate || event->key == R_KEY_ESCAPE) app.dismiss_splash();
        return R_TRUE;
    }
    if (event->key == R_KEY_V && (event->modifiers & R_MOD_CTRL) && !(event->modifiers & (R_MOD_ALT | R_MOD_SUPER))) {
        const bool tesseract = Rwidget_is_focused(app.id("tesseract.composer"));
        if ((tesseract || Rwidget_is_focused(app.id("composer"))) && app.paste_image(tesseract)) return R_TRUE;
        const char* clipboard = Rclipboard_get_text();
        if (clipboard && !is_valid_utf8(clipboard)) {
            app.set_status("Clipboard does not contain plain text or a supported image");
            return R_TRUE;
        }
    }
    if (app.capture_shortcut(event)) return R_TRUE;
    if (event->key == R_KEY_ESCAPE && Rwidget_get_visibility(app.id("run.setup")) == R_VISIBLE) {
        Rwidget_set_visibility(app.id("run.setup"), R_COLLAPSED);
        app.set_active_view(app.id("center.tabs"), app.run_setup_return_tab_);
        Rwidget_focus(app.id(app.run_setup_return_tab_ == 5 ? "tesseract.composer" : "composer"));
        return R_TRUE;
    }
    if (event->key == R_KEY_ESCAPE && Rwidget_get_visibility(app.id("recall.palette")) == R_VISIBLE) {
        app.close_recall();
        return R_TRUE;
    }
    if (event->key == R_KEY_ESCAPE && Rwidget_get_visibility(app.id("command.palette")) == R_VISIBLE) {
        app.close_palette();
        return R_TRUE;
    }
    if (Rwidget_get_visibility(app.id("theme.gallery")) == R_VISIBLE) {
        if (event->key == R_KEY_ESCAPE) {
            app.close_theme_gallery();
            return R_TRUE;
        }
        if (!(event->modifiers & (R_MOD_CTRL | R_MOD_ALT | R_MOD_SUPER))) {
            if (event->repeat && (event->key == R_KEY_LEFT || event->key == R_KEY_RIGHT ||
                                  event->key == R_KEY_UP || event->key == R_KEY_DOWN)) return R_TRUE;
            if (event->key == R_KEY_LEFT || event->key == R_KEY_RIGHT) {
                app.move_theme_gallery(event->key == R_KEY_LEFT ? -1 : 1);
                return R_TRUE;
            }
            if (event->key == R_KEY_UP || event->key == R_KEY_DOWN) {
                const int finish = (theme_finish(app.theme_index_) + (event->key == R_KEY_UP ? 2 : 1)) % 3;
                app.select_theme(theme_index(app.theme_gallery_.family(), finish));
                return R_TRUE;
            }
        }
    }
    if (event->key == R_KEY_ESCAPE && app.active_view(app.id("center.tabs")) == 3 && app.active_view(app.id("more.tabs")) == 1) {
        app.close_settings();
        return R_TRUE;
    }
    if (app.suppress_replaced_shortcut(event)) return R_TRUE;
    if (!(event->modifiers & R_MOD_CTRL)) return R_FALSE;
    if (event->key == R_KEY_COMMA && !(event->modifiers & (R_MOD_SHIFT | R_MOD_ALT | R_MOD_SUPER))) {
        app.toggle_settings();
        return R_TRUE;
    }
    if ((event->modifiers & R_MOD_SHIFT) && event->key == R_KEY_R) {
        app.open_proposal();
        return R_TRUE;
    }
    if ((event->modifiers & R_MOD_SHIFT) && (event->key == R_KEY_ENTER || event->key == R_KEY_KP_ENTER)) {
        app.accept_proposal();
        return R_TRUE;
    }
    const bool increase = event->key == R_KEY_EQUAL || event->key == R_KEY_KP_ADD ||
        ((event->modifiers & R_MOD_SHIFT) && event->key == R_KEY_0);
    const bool decrease = event->key == R_KEY_MINUS || event->key == R_KEY_KP_SUBTRACT;
    if (!increase && !decrease) return R_FALSE;
    const float amount = increase ? 0.1f : -0.1f;
    if (event->modifiers & R_MOD_SHIFT) app.zoom_interface(amount);
    else app.zoom_editors(amount);
    return R_TRUE;
}

void App::action(Rid sender, const char* action_name, void* user) {
    auto& app = *static_cast<App*>(user);
    const std::string name = action_name;
    if (name == "run.send") app.start_run();
    else if (name == "run.setup.toggle") {
        const bool open = Rwidget_get_visibility(app.id("run.setup")) != R_VISIBLE;
        Rwidget_set_visibility(app.id("recall.palette"), R_COLLAPSED);
        Rwidget_set_visibility(app.id("command.palette"), R_COLLAPSED);
        Rwidget_set_visibility(app.id("theme.gallery"), R_COLLAPSED);
        Rwidget_set_visibility(app.id("run.setup"), open ? R_VISIBLE : R_COLLAPSED);
        if (open) {
            app.run_setup_return_tab_ = app.active_view(app.id("center.tabs"));
            Rwidget_focus(app.id("model.select"));
        } else {
            app.set_active_view(app.id("center.tabs"), app.run_setup_return_tab_);
            Rwidget_focus(app.id(app.run_setup_return_tab_ == 5 ? "tesseract.composer" : "composer"));
        }
    }
    else if (name == "run.setup.close") {
        Rwidget_set_visibility(app.id("run.setup"), R_COLLAPSED);
        app.set_active_view(app.id("center.tabs"), app.run_setup_return_tab_);
        Rwidget_focus(app.id(app.run_setup_return_tab_ == 5 ? "tesseract.composer" : "composer"));
    }
    else if (name == "composer.focus") {
        app.set_active_view(app.id("center.tabs"), 0);
        Rwidget_focus(app.id("composer"));
    }
    else if (name == "composer.image.remove") app.remove_image(false);
    else if (name == "tesseract.image.remove") app.remove_image(true);
    else if (name == "run.stop") app.stop_run();
    else if (name == "run.retry") app.retry_run();
    else if (name == "queue.cancel") app.cancel_queued();
    else if (name == "workspace.open") app.open_workspace();
    else if (name == "thread.new") app.new_thread();
    else if (name == "thread.open") {
        const auto row = Rlist_view_get_selected(sender);
        if (row >= 0 && row < static_cast<int>(app.thread_rows_.size())) app.open_thread(app.thread_rows_[row]);
    }
    else if (name == "thread.view") app.toggle_archived_threads();
    else if (name == "thread.archive") app.archive_selected_thread();
    else if (name == "agent.new") app.new_agent();
    else if (name == "agent.save") app.save_agent();
    else if (name == "agent.delete") app.delete_agent();
    else if (name == "agent.open") {
        const auto row = Rlist_view_get_selected(sender);
        if (row >= 0 && row < static_cast<int>(app.agent_rows_.size())) app.open_agent(app.agent_rows_[row]);
    }
    else if (name == "wiki.new") app.new_note();
    else if (name == "wiki.save") app.save_note();
    else if (name == "wiki.open") {
        const auto row = Rlist_view_get_selected(sender);
        if (row >= 0 && row < static_cast<int>(app.note_rows_.size())) {
                app.open_note(app.note_rows_[row]);
            app.show_context(1);
        }
    }
    else if (name == "proposal.open") app.open_proposal();
    else if (name == "proposal.accept") app.accept_proposal();
    else if (name == "proposal.discard") app.discard_proposal();
    else if (name == "checkpoint.review") {
        app.set_sidebar_visibility(false, false);
        if (app.selected_checkpoint_id_.empty() && !app.checkpoint_store_.checkpoints().empty()) {
            app.open_checkpoint(app.checkpoint_store_.checkpoints().front().id);
        }
        app.set_active_view(app.id("review.tabs"), 0);
        app.set_active_view(app.id("center.tabs"), 4);
        if (const auto* checkpoint = app.checkpoint_store_.find(app.selected_checkpoint_id_)) {
            if (checkpoint->changes.empty() && !checkpoint->checks.empty()) {
                app.open_verification(0);
                Rwidget_focus(app.id("verification.results"));
            } else {
                Rwidget_focus(app.id("checkpoint.files"));
            }
        }
    }
    else if (name == "checkpoint.history_changed") {
        const auto index = Rcombo_box_get_selected(sender);
        if (index >= 0 && index < static_cast<int>(app.checkpoint_rows_.size())) app.open_checkpoint(app.checkpoint_rows_[index]);
    }
    else if (name == "checkpoint.file_changed") app.show_checkpoint_diff(Rcombo_box_get_selected(sender));
    else if (name == "checkpoint.open") app.open_checkpoint_file();
    else if (name == "checkpoint.restore") app.restore_checkpoint();
    else if (name == "checkpoint.restore_hunk") app.restore_checkpoint_hunk();
    else if (name == "verification.open") app.open_verification(Rlist_view_get_selected(sender));
    else if (name == "verification.open_diagnostic") app.open_diagnostic();
    else if (name == "verification.rerun") app.rerun_verification();
    else if (name == "verification.watch") app.toggle_watch_checks();
    else if (name == "wiki.preview") {
        app.set_sidebar_visibility(false, true);
        app.show_context(1);
        app.set_active_view(app.id("wiki.tabs"), 1);
        app.update_preview();
    }
    else if (name == "harness.changed") app.change_harness(Rcombo_box_get_selected(sender));
    else if (name == "run.mode.changed") app.change_run_mode(Rcombo_box_get_selected(sender));
    else if (name == "model.changed") app.change_model(Rcombo_box_get_selected(sender));
    else if (name == "effort.changed") app.change_effort(Rcombo_box_get_selected(sender));
    else if (name == "mode.build") app.change_run_mode(0);
    else if (name == "mode.ask") app.change_run_mode(1);
    else if (name == "mode.review") app.change_run_mode(2);
    else if (name == "context.clear") app.clear_context();
    else if (name == "context.remove") app.remove_context_file();
    else if (name == "context.compact") app.compact_context();
    else if (name == "palette.commands") app.open_palette(App::PaletteMode::Commands);
    else if (name == "palette.files") app.open_palette(App::PaletteMode::Files);
    else if (name == "palette.search") app.open_palette(App::PaletteMode::Content);
    else if (name == "palette.changed") app.palette_changed();
    else if (name == "palette.open") app.open_palette_result();
    else if (name == "palette.close") app.close_palette();
    else if (name == "recall.open") app.open_recall();
    else if (name == "recall.close") app.close_recall();
    else if (name == "recall.changed") app.refresh_recall();
    else if (name == "recall.use") app.use_recall();
    else if (name == "recall.pin") app.toggle_recall_pin();
    else if (name == "recall.erase") app.erase_recall();
    else if (name == "nav.chat") {
        app.set_active_view(app.id("center.tabs"), 0);
        Rwidget_focus(app.id("composer"));
    }
    else if (name == "nav.canvas") app.open_tesseract();
    else if (name == "nav.workflows") app.open_endurance();
    else if (name == "nav.files") {
        app.set_active_view(app.id("center.tabs"), 1);
        Rwidget_focus(app.id("workspace.files"));
    }
    else if (name == "nav.agents") {
        app.set_sidebar_visibility(false, false);
        app.set_active_view(app.id("more.tabs"), 0);
        app.set_active_view(app.id("center.tabs"), 3);
        Rwidget_focus(app.id("agent.editor"));
    }
    else if (name == "nav.review") {
        app.set_sidebar_visibility(false, false);
        app.set_active_view(app.id("center.tabs"), 4);
    }
    else if (name == "nav.knowledge") {
        app.refresh_knowledge();
        app.show_context(2);
    }
    else if (name == "nav.worktrees") {
        app.set_sidebar_visibility(false, false);
        app.refresh_missions();
        app.set_active_view(app.id("more.tabs"), 3);
        app.set_active_view(app.id("center.tabs"), 3);
    }
    else if (name == "nav.settings") app.open_settings();
    else if (name == "activity.open") {
        if (app.right_sidebar_visible_ && app.active_view(app.id("right.tabs")) == 0) app.set_sidebar_visibility(false, false);
        else app.show_context(0);
    }
    else if (name == "alerts.view") app.show_context(3);
    else if (name == "context.close") app.set_sidebar_visibility(false, false);
    else if (name == "navigation.search") app.refresh_lists();
    else if (name == "knowledge.search") app.refresh_knowledge();
    else if (name == "knowledge.open_tab") {
        app.refresh_knowledge();
        app.show_context(2);
    }
    else if (name == "knowledge.open") app.open_knowledge(Rlist_view_get_selected(sender));
    else if (name == "memory.open") app.open_memory();
    else if (name == "memory.fit") {
        app.memory_.fit();
        Rwidget_invalidate(app.id("memory.canvas"));
        Rwidget_focus(app.id("memory.canvas"));
    }
    else if (name == "memory.forget") app.forget_memory();
    else if (name == "tesseract.open") app.open_tesseract();
    else if (name == "tesseract.fit") {
        app.tesseract_.fit();
        Rwidget_invalidate(app.id("tesseract.canvas"));
        app.refresh_tesseract_chrome();
    }
    else if (name == "tesseract.zoom.in" || name == "tesseract.zoom.out") {
        app.tesseract_.zoom_by(name == "tesseract.zoom.in" ? 1.25f : 0.8f);
        Rwidget_invalidate(app.id("tesseract.canvas"));
        app.refresh_tesseract_chrome();
    }
    else if (name == "tesseract.selection.clear") {
        app.finish_tesseract_text_edit(false);
        app.tesseract_.clear_selection();
        Rwidget_invalidate(app.id("tesseract.canvas"));
        app.refresh_tesseract_chrome();
    }
    else if (name == "mission.open") {
        app.refresh_missions();
        app.set_active_view(app.id("more.tabs"), 3);
        app.set_active_view(app.id("center.tabs"), 3);
    }
    else if (name == "mission.create") app.create_mission();
    else if (name == "mission.review") {
        if (!app.selected_mission_id_.empty()) app.review_mission(app.selected_mission_id_);
    }
    else if (name == "mission.apply") app.apply_mission();
    else if (name == "mission.apply_hunk") app.apply_mission_hunk();
    else if (name == "mission.discard") app.discard_mission();
    else if (name == "mission.return") app.return_from_mission();
    else if (name == "splash.skip") app.dismiss_splash();
    else if (name == "splash.sound") app.toggle_splash_sound();
    else if (name == "endurance.open") app.open_endurance();
    else if (name == "endurance.workflow") app.select_endurance_workflow(Rcombo_box_get_selected(sender));
    else if (name == "endurance.new") {
        app.endurance_store_.create_workflow(app.harness_ == Harness::KimiCode ? "kimi" : "claude");
        app.refresh_endurance_workflows(static_cast<int>(app.endurance_workflows_.size()));
    }
    else if (name == "endurance.duplicate") {
        if (const auto* workflow = app.current_endurance_workflow()) {
            const auto copy = app.endurance_store_.duplicate_workflow(*workflow);
            app.refresh_endurance_workflows();
            for (int i = 0; i < static_cast<int>(app.endurance_workflows_.size()); ++i) {
                if (app.endurance_workflows_[i].path == copy.path) app.select_endurance_workflow(i);
            }
        }
    }
    else if (name == "endurance.validate") app.validate_endurance_yaml(true);
    else if (name == "endurance.run") app.start_endurance_run();
    else if (name == "endurance.view") {
        if (app.active_view(sender) == 2) app.refresh_endurance_runs();
        app.rebuild_endurance_graph();
    }
    else if (name == "endurance.recurrence.toggle") app.toggle_endurance_recurrence();
    else if (name == "endurance.schedule.save") app.save_endurance_recurrence();
    else if (name == "endurance.schedule.enabled") {
        Rlabel_set_text(app.id("endurance.schedule.summary"), Rcheckbox_is_checked(app.id("endurance.schedule.enabled"))
            ? "Scheduled recurrence will be saved with this workflow." : "Turn on Scheduled and save to run this workflow automatically.");
    }
    else if (name == "endurance.schedule.frequency") {
        const int frequency = Rcombo_box_get_selected(app.id("endurance.schedule.frequency"));
        Rwidget_set_visibility(app.id("endurance.schedule.time"), frequency == 0 || frequency == 4 ? R_COLLAPSED : R_VISIBLE);
        Rwidget_set_visibility(app.id("endurance.schedule.cron"), frequency == 4 ? R_VISIBLE : R_COLLAPSED);
    }
    else if (name == "endurance.graph.fit") app.fit_endurance_graph();
    else if (name == "endurance.graph.layout") app.layout_endurance_graph();
    else if (name == "endurance.step.add") app.add_endurance_step();
    else if (name == "endurance.step.duplicate") app.duplicate_endurance_step();
    else if (name == "endurance.step.delete") app.delete_endurance_step();
    else if (name == "endurance.step.changed") {
        app.update_endurance_step();
        app.rebuild_endurance_graph();
    }
    else if (name == "endurance.run.open") app.open_endurance_run(Rlist_view_get_selected(sender));
    else if (name == "endurance.run.select") app.open_endurance_run(Rcombo_box_get_selected(sender));
    else if (name == "endurance.run.stop") app.stop_endurance_run();
    else if (name == "endurance.run.resume") app.resume_endurance_run();
    else if (name == "endurance.approval.accept") app.approve_endurance_run(true);
    else if (name == "endurance.approval.reject") app.approve_endurance_run(false);
    else if (name == "theme.browser") {
        if (Rwidget_get_visibility(app.id("theme.gallery")) == R_VISIBLE) app.close_theme_gallery();
        else app.open_theme_gallery();
    }
    else if (name == "theme.gallery.close") app.close_theme_gallery();
    else if (name == "theme.gallery.previous") app.move_theme_gallery(-1);
    else if (name == "theme.gallery.next") app.move_theme_gallery(1);
    else if (name == "theme.gallery.dark") app.select_theme(theme_index(app.theme_gallery_.family(), 0));
    else if (name == "theme.gallery.light") app.select_theme(theme_index(app.theme_gallery_.family(), 1));
    else if (name == "theme.gallery.glass") app.select_theme(theme_index(app.theme_gallery_.family(), 2));
    else if (name == "theme.earth") app.select_theme(12);
    else if (name == "theme.earth.light") app.select_theme(13);
    else if (name == "theme.earth.glass") app.select_theme(14);
    else if (name == "theme.miller") app.select_theme(3);
    else if (name == "theme.miller.light") app.select_theme(8);
    else if (name == "theme.miller.glass") app.select_theme(15);
    else if (name == "theme.mann") app.select_theme(4);
    else if (name == "theme.mann.dark") app.select_theme(9);
    else if (name == "theme.mann.glass") app.select_theme(16);
    else if (name == "theme.edmunds") app.select_theme(5);
    else if (name == "theme.edmunds.dark") app.select_theme(10);
    else if (name == "theme.edmunds.glass") app.select_theme(17);
    else if (name == "theme.gargantua") app.select_theme(6);
    else if (name == "theme.gargantua.light") app.select_theme(11);
    else if (name == "theme.gargantua.glass") app.select_theme(18);
    else if (name == "appearance.toggle") app.toggle_appearance();
    else if (name == "settings.toggle") app.toggle_settings();
    else if (name == "settings.close") app.close_settings();
    else if (name == "settings.harness.changed") app.change_harness(Rcombo_box_get_selected(sender));
    else if (name == "settings.vector") app.toggle_vector_recall();
    else if (name == "settings.interface.decrease") app.zoom_interface(-0.1f);
    else if (name == "settings.interface.reset") app.zoom_interface(0.0f);
    else if (name == "settings.interface.increase") app.zoom_interface(0.1f);
    else if (name == "settings.editor.decrease") app.zoom_editors(-0.1f);
    else if (name == "settings.editor.reset") app.zoom_editors(0.0f);
    else if (name == "settings.editor.increase") app.zoom_editors(0.1f);
    else if (name == "shortcut.changed") app.refresh_shortcut_editor();
    else if (name == "shortcut.rebind") app.begin_shortcut_rebind();
    else if (name == "shortcut.reset") app.reset_shortcut();
    else if (name == "alerts.open") app.open_notification();
    else if (name == "alerts.clear") app.clear_notifications();
    else if (name == "sidebar.left") app.toggle_sidebar(true);
    else if (name == "sidebar.right") app.toggle_sidebar(false);
    else if (name == "graph.fit") Rnode_graph_frame_all(app.id("graph"));
    else if (name == "graph.toggle") {
        app.set_active_view(app.id("center.tabs"), 2);
        app.set_active_view(app.id("endurance.views"), 3);
        Rnode_graph_frame_all(app.id("graph"));
        Rwidget_focus(app.id("graph"));
    }
    else if (name == "code.save") app.save_code_file();
    else if (name == "file.save") {
        if (app.active_view(app.id("center.tabs")) == 1) app.save_code_file();
        else if (app.active_view(app.id("center.tabs")) == 2 && app.active_view(app.id("endurance.views")) == 1) app.validate_endurance_yaml(true);
        else if (app.active_view(app.id("center.tabs")) == 3 && app.active_view(app.id("more.tabs")) == 0) app.save_agent();
        else if (app.right_sidebar_visible_ && app.active_view(app.id("right.tabs")) == 1) app.save_note();
    }
}

void App::list_selection(Rid list, int index, void* user) {
    auto& app = *static_cast<App*>(user);
    if (list == app.id("threads")) {
        if (index >= 0 && index < static_cast<int>(app.thread_rows_.size())) app.open_thread(app.thread_rows_[index]);
    } else if (list == app.id("agents")) {
        if (index >= 0 && index < static_cast<int>(app.agent_rows_.size())) app.open_agent(app.agent_rows_[index]);
    } else if (list == app.id("knowledge.results")) app.open_knowledge(index);
    else if (list == app.id("verification.results")) app.open_verification(index);
    else if (list == app.id("alerts.list")) app.select_notification(index);
    else if (list == app.id("recall.results")) {
        const bool valid = index >= 0 && index < static_cast<int>(app.recall_rows_.size());
        Rwidget_set_enabled(app.id("recall.pin"), valid);
        Rwidget_set_enabled(app.id("recall.erase"), valid);
        const auto* prompt = valid ? app.recall_store_.find(app.recall_rows_[index]) : nullptr;
        Rbutton_set_text(app.id("recall.pin"), prompt && prompt->pinned ? "Unpin" : "Pin");
    }
    else if (list == app.id("mission.list")) {
        if (!app.loading_missions_ && index >= 0 && index < static_cast<int>(app.mission_rows_.size())) {
            app.review_mission(app.mission_rows_[index]);
        }
    }
    else if (list == app.id("wiki")) {
        if (index < 0 || index >= static_cast<int>(app.note_rows_.size())) return;
        app.open_note(app.note_rows_[index]);
        if (!app.right_sidebar_visible_ || app.active_view(app.id("right.tabs")) != 2) app.show_context(1);
    }
}

void App::tab_changed(Rid tabs, int index, void* user) {
    auto& app = *static_cast<App*>(user);
    if (tabs == app.id("wiki.tabs") && index == 1) app.update_preview();
}

void App::wiki_link(Rid label, const char* url, void* user) {
    auto& app = *static_cast<App*>(user);
    (void)label;
    const std::string target = url;
    if (target.rfind("file:", 0) == 0) {
        const auto decoded = url_decode(target.substr(5));
        const auto reference = editor_reference(app.workspace_.root(), decoded);
        if (!reference) {
            app.set_status("File is not available inside this workspace: " + decoded);
            return;
        }
        app.open_code_file(reference->path, reference->line);
        return;
    }
    if (target.rfind("wiki:", 0) != 0) {
        Ropen_url(url);
        return;
    }
    const auto decoded = url_decode(target.substr(5));
    const auto title = lowercase(decoded);
    const auto found = std::find_if(app.notes_.begin(), app.notes_.end(), [&](const auto& note) {
        return lowercase(note.title) == title;
    });
    if (found == app.notes_.end()) {
        app.set_status("Wiki note not found: " + target.substr(5));
        return;
    }
    app.open_note(static_cast<int>(found - app.notes_.begin()));
}

bool App::has_animation_frames() const {
    if (splash_.active()) return splash_.animating();
    if (Raccessibility_get_reduce_motion()) return false;
    return Rwidget_get_visibility(id("theme.gallery")) == R_VISIBLE ||
        (endurance_.busy() && active_view(id("center.tabs")) == 2 && active_view(id("endurance.views")) == 0) ||
        (active_view(id("center.tabs")) == 3 && active_view(id("more.tabs")) == 2) ||
        (active_view(id("center.tabs")) == 5 && tesseract_.empty());
}

void App::frame(double delta, void* user) {
    auto& app = *static_cast<App*>(user);
    app.pump();
    app.update_run_timer();
    app.pump_autosave();
    app.pump_recovery();
    app.pump_palette();
    app.pump_verification();
    if (app.active_view(app.id("center.tabs")) == 5) {
        if (app.tesseract_.reload_if_changed()) Rwidget_invalidate(app.id("tesseract.canvas"));
        app.refresh_tesseract_chrome();
    }
    if (app.composer_completion_pending_) {
        app.composer_completion_pending_ = false;
        Rtext_editor_show_completions(app.id("composer"));
    }
    const bool reduce_motion = Raccessibility_get_reduce_motion();
    const bool memory_active = !reduce_motion && app.active_view(app.id("center.tabs")) == 3 && app.active_view(app.id("more.tabs")) == 2;
    const bool gallery_active = !reduce_motion && Rwidget_get_visibility(app.id("theme.gallery")) == R_VISIBLE;
    bool redraw = false;
    if (app.splash_.active()) {
        const bool splash_redraw = app.splash_.advance(delta, reduce_motion);
        if (app.splash_.active() && splash_redraw) {
            Rwidget_set_opacity(app.id("splash.container"), app.splash_.opacity());
            Rwidget_invalidate(app.id("splash.screen"));
        } else if (!app.splash_.active()) {
            app.dismiss_splash();
        }
        redraw = splash_redraw || redraw;
    }
    if (memory_active) {
        redraw = app.memory_.advance(app.memory_animation_active_ ? delta : 0.0, false) || redraw;
    }
    if (gallery_active) {
        redraw = app.theme_gallery_.advance(app.gallery_animation_active_ ? delta : 0.0, false) || redraw;
    }
    if (app.active_view(app.id("center.tabs")) == 5 && app.tesseract_.empty()) {
        redraw = app.tesseract_.advance(delta, reduce_motion) || redraw;
    }
    app.memory_animation_active_ = memory_active;
    app.gallery_animation_active_ = gallery_active;
    if (redraw) Rwindow_request_redraw(app.window_);
}

} // namespace raven
