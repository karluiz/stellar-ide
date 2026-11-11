#include "shortcut_text.hpp"

#include <regex>

namespace raven {

std::string native_shortcut_text(const std::string& text, bool mac) {
    if (!mac) return text;
    static const std::regex shortcut(
        R"(\b((?:(?:Ctrl|Alt|Shift|Super)\+)+)(Enter|Escape|Esc|Tab|Space|Backspace|Delete|Up|Down|Left|Right|F\d{1,2}|[A-Z0-9]\b|[,./;\[\]=-]))");
    std::string result;
    auto begin = std::sregex_iterator(text.begin(), text.end(), shortcut);
    std::size_t last = 0;
    for (auto it = begin; it != std::sregex_iterator(); ++it) {
        const auto& match = *it;
        result += text.substr(last, static_cast<std::size_t>(match.position()) - last);
        const std::string modifiers = match[1].str();
        const auto has = [&](const char* name) { return modifiers.find(std::string(name) + "+") != std::string::npos; };
        // Apple's order: Control, Option, Shift, Command.
        std::string symbol;
        if (has("Super")) symbol += "⌃";
        if (has("Alt")) symbol += "⌥";
        if (has("Shift")) symbol += "⇧";
        if (has("Ctrl")) symbol += "⌘";
        std::string key = match[2].str();
        if (key == "Enter") key = "↩";
        else if (key == "Escape" || key == "Esc") key = "⎋";
        else if (key == "Tab") key = "⇥";
        else if (key == "Backspace" || key == "Delete") key = "⌫";
        else if (key == "Up") key = "↑";
        else if (key == "Down") key = "↓";
        else if (key == "Left") key = "←";
        else if (key == "Right") key = "→";
        result += symbol + key;
        last = static_cast<std::size_t>(match.position() + match.length());
    }
    result += text.substr(last);
    return result;
}

std::string native_shortcut_text(const std::string& text) {
#ifdef __APPLE__
    return native_shortcut_text(text, true);
#else
    return native_shortcut_text(text, false);
#endif
}

} // namespace raven
