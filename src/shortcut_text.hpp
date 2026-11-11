#pragma once

#include <string>

namespace raven {

// Rewrites every shortcut written the portable way ("Ctrl+Shift+P", "Ctrl+Enter") inside `text` into
// the host platform's notation. On macOS, where Cmd acts as Ctrl, it becomes Apple's symbol order
// "⇧⌘P" / "⌘↩"; elsewhere the text is returned unchanged.
std::string native_shortcut_text(const std::string& text, bool mac);

// The same for the host this binary was built for.
std::string native_shortcut_text(const std::string& text);

} // namespace raven
