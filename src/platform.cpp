#include "platform.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

namespace raven::platform {

void extend_search_path() {
    const char* current = std::getenv("PATH");
    std::string path = current ? current : "";
    std::vector<std::string> listed;
    std::stringstream entries(path);
    for (std::string entry; std::getline(entries, entry, ':');) listed.push_back(entry);
    std::vector<std::string> candidates = {"/opt/homebrew/bin", "/usr/local/bin", "/usr/bin", "/bin"};
    if (const char* home = std::getenv("HOME"); home && *home) {
        const std::string root = home;
        for (const char* suffix : {"/.local/bin", "/.claude/local", "/.kimi-code/bin", "/.npm-global/bin", "/.bun/bin", "/.cargo/bin"}) {
            candidates.push_back(root + suffix);
        }
    }
    bool changed = false;
    for (const auto& candidate : candidates) {
        std::error_code error;
        if (!std::filesystem::is_directory(candidate, error)) continue;
        if (std::find(listed.begin(), listed.end(), candidate) != listed.end()) continue;
        path += (path.empty() ? "" : ":") + candidate;
        listed.push_back(candidate);
        changed = true;
    }
    if (changed) setenv("PATH", path.c_str(), 1);
}

} // namespace raven::platform

#ifdef __APPLE__

#include <Carbon/Carbon.h>
#include <CoreGraphics/CoreGraphics.h>
#include <objc/message.h>
#include <objc/runtime.h>


namespace raven::platform {
namespace {

// AppKit is driven through the Objective-C runtime so the application stays a pure C++ target.
constexpr unsigned long kStyleMaskResizable = 1UL << 3;

template <typename Result, typename... Arguments>
Result send(id receiver, const char* selector, Arguments... arguments) {
    return reinterpret_cast<Result (*)(id, SEL, Arguments...)>(objc_msgSend)(receiver, sel_registerName(selector), arguments...);
}

CGRect send_rect(id receiver, const char* selector) {
#if defined(__x86_64__)
    return reinterpret_cast<CGRect (*)(id, SEL)>(objc_msgSend_stret)(receiver, sel_registerName(selector));
#else
    return send<CGRect>(receiver, selector);
#endif
}

IMP original_can_become_key = nullptr;
IMP original_can_become_main = nullptr;

// Stellar's main window uses a custom header, which Raven creates as a borderless NSWindow. AppKit
// refuses key and main status to borderless windows by default, so the window could be clicked but
// never received a single keystroke. Resizable borderless windows are top-level application
// windows; popups and tooltips are not resizable and keep the default behaviour.
bool is_application_window(id window) {
    return (send<unsigned long>(window, "styleMask") & kStyleMaskResizable) != 0;
}

BOOL can_become_key(id self, SEL selector) {
    if (is_application_window(self)) return YES;
    return original_can_become_key ? reinterpret_cast<BOOL (*)(id, SEL)>(original_can_become_key)(self, selector) : NO;
}

BOOL can_become_main(id self, SEL selector) {
    if (is_application_window(self)) return YES;
    return original_can_become_main ? reinterpret_cast<BOOL (*)(id, SEL)>(original_can_become_main)(self, selector) : NO;
}

// Raven's flagsChanged: handler asks modifier events for their characters to map the key. AppKit
// throws NSInternalInconsistencyException for that on anything but key-down/key-up events, which
// terminated the application the first time Shift, Ctrl, Alt or Cmd was pressed. Modifier events
// carry no characters, so an empty string lets the handler fall back to the key code.
constexpr unsigned long kEventTypeKeyDown = 10;
constexpr unsigned long kEventTypeKeyUp = 11;

IMP original_characters = nullptr;
IMP original_characters_ignoring_modifiers = nullptr;

bool has_characters(id event) {
    const auto type = send<unsigned long>(event, "type");
    return type == kEventTypeKeyDown || type == kEventTypeKeyUp;
}

id empty_string() {
    return send<id>(reinterpret_cast<id>(objc_getClass("NSString")), "string");
}

id characters(id self, SEL selector) {
    if (!has_characters(self) || !original_characters) return empty_string();
    return reinterpret_cast<id (*)(id, SEL)>(original_characters)(self, selector);
}

id characters_ignoring_modifiers(id self, SEL selector) {
    if (!has_characters(self) || !original_characters_ignoring_modifiers) return empty_string();
    return reinterpret_cast<id (*)(id, SEL)>(original_characters_ignoring_modifiers)(self, selector);
}

IMP replace(Class type, const char* name, IMP implementation) {
    const SEL selector = sel_registerName(name);
    const Method method = class_getInstanceMethod(type, selector);
    if (!method) return nullptr;
    const IMP original = method_getImplementation(method);
    class_replaceMethod(type, selector, implementation, method_getTypeEncoding(method));
    return original;
}


// ---------------------------------------------------------------------------------------------
// Keyboard translation
//
// Raven's Cocoa view forwards -[NSEvent characters] verbatim as text input and never consults the
// keyboard layout. Left alone that means:
//   * Enter, Backspace, Escape, Tab and the arrow keys insert their control or function-key code
//     points into the focused text field (the X11 backend filters these; the Cocoa one does not);
//   * dead keys never compose, so accented letters cannot be typed on a Spanish layout;
//   * Option is treated as a shortcut modifier, so Option-composed characters such as @ # [ ] { }
//     on ISO layouts are discarded;
//   * a held key types its character only once;
//   * Cmd shortcuts do nothing, because every Stellar shortcut is declared with Ctrl.
// The wrappers below rebuild each key event with layout-translated text before Raven sees it.
// ---------------------------------------------------------------------------------------------
constexpr unsigned long kFlagCapsLock = 1UL << 16;
constexpr unsigned long kFlagShift = 1UL << 17;
constexpr unsigned long kFlagControl = 1UL << 18;
constexpr unsigned long kFlagOption = 1UL << 19;
constexpr unsigned long kFlagCommand = 1UL << 20;
constexpr unsigned short kKeyCodeQ = 12;
constexpr unsigned short kKeyCodeH = 4;

IMP original_key_down = nullptr;
IMP original_key_up = nullptr;
UInt32 dead_key_state = 0;

bool is_printable(UniChar unit) {
    // 0xF700-0xF8FF is where AppKit reports arrows, function keys and other non-text keys.
    return unit >= 32 && unit != 127 && !(unit >= 0xF700 && unit <= 0xF8FF);
}

// Translates a key press through the active keyboard layout, carrying dead-key state between
// presses. Returns false when the layout exposes no Unicode table (some input methods), in which
// case the event is passed through untouched.
bool translate(unsigned short key_code, unsigned long flags, std::vector<UniChar>& text) {
    const TISInputSourceRef source = TISCopyCurrentKeyboardLayoutInputSource();
    if (!source) return false;
    const auto data = static_cast<CFDataRef>(TISGetInputSourceProperty(source, kTISPropertyUnicodeKeyLayoutData));
    bool translated = false;
    if (data) {
        UInt32 modifiers = 0;
        if (flags & kFlagShift) modifiers |= shiftKey >> 8;
        if (flags & kFlagCapsLock) modifiers |= alphaLock >> 8;
        if (flags & kFlagOption) modifiers |= optionKey >> 8;
        UniChar buffer[8];
        UniCharCount length = 0;
        const auto* layout = reinterpret_cast<const UCKeyboardLayout*>(CFDataGetBytePtr(data));
        if (UCKeyTranslate(layout, key_code, kUCKeyActionDown, modifiers, LMGetKbdType(), 0, &dead_key_state,
                8, &length, buffer) == noErr) {
            translated = true;
            for (UniCharCount index = 0; index < length; ++index) {
                if (is_printable(buffer[index])) text.push_back(buffer[index]);
            }
        }
    }
    CFRelease(source);
    return translated;
}

id rebuilt_key_event(id event, unsigned long type, unsigned long flags, id text, bool repeat, int key_code = -1) {
    using Factory = id (*)(id, SEL, unsigned long, CGPoint, unsigned long, double, long, id, id, id, BOOL, unsigned short);
    return reinterpret_cast<Factory>(objc_msgSend)(reinterpret_cast<id>(objc_getClass("NSEvent")),
        sel_registerName("keyEventWithType:location:modifierFlags:timestamp:windowNumber:context:characters:charactersIgnoringModifiers:isARepeat:keyCode:"),
        type, send<CGPoint>(event, "locationInWindow"), flags, send<double>(event, "timestamp"),
        send<long>(event, "windowNumber"), static_cast<id>(nullptr), text,
        reinterpret_cast<id (*)(id, SEL)>(original_characters_ignoring_modifiers)(event, sel_registerName("charactersIgnoringModifiers")),
        repeat ? YES : NO, key_code >= 0 ? static_cast<unsigned short>(key_code) : send<unsigned short>(event, "keyCode"));
}

// macOS text navigation, translated into the PC conventions Raven's editors implement:
//   Cmd+Left/Right -> Home/End          Cmd+Up/Down -> Ctrl+Home/End
//   Option+Left/Right -> Ctrl+Left/Right (by word)    Option+Delete -> Ctrl+Backspace
//   Cmd+Delete -> delete to the start of the line (handled in key_down)
// Shift is kept so the same combinations extend the selection.
constexpr unsigned short kKeyCodeLeft = 123, kKeyCodeRight = 124, kKeyCodeDown = 125, kKeyCodeUp = 126;
constexpr unsigned short kKeyCodeHome = 115, kKeyCodeEnd = 119, kKeyCodeDelete = 51;

bool translate_navigation(unsigned short key_code, unsigned long flags, int& mapped_key, unsigned long& mapped_flags) {
    const bool command = flags & kFlagCommand;
    const bool option = flags & kFlagOption;
    if (flags & kFlagControl) return false;
    const unsigned long shift = flags & kFlagShift;
    if (command && !option) {
        if (key_code == kKeyCodeLeft || key_code == kKeyCodeRight) {
            mapped_key = key_code == kKeyCodeLeft ? kKeyCodeHome : kKeyCodeEnd;
            mapped_flags = shift;
            return true;
        }
        if (key_code == kKeyCodeUp || key_code == kKeyCodeDown) {
            mapped_key = key_code == kKeyCodeUp ? kKeyCodeHome : kKeyCodeEnd;
            mapped_flags = shift | kFlagControl;
            return true;
        }
    }
    if (option && !command && (key_code == kKeyCodeLeft || key_code == kKeyCodeRight || key_code == kKeyCodeDelete)) {
        mapped_key = key_code;
        mapped_flags = shift | kFlagControl;
        return true;
    }
    return false;
}

// Cmd acts as Ctrl for shortcuts. Control keeps working as well.
unsigned long shortcut_flags(unsigned long flags) {
    return (flags & kFlagCommand) ? ((flags & ~kFlagCommand) | kFlagControl) : flags;
}

// Keys that edit or navigate rather than type. Their events must never insert text, whatever
// characters an event source attached to them (synthetic events from automation and accessibility
// tools sometimes carry a label such as "esc").
bool is_command_key(unsigned short key_code) {
    switch (key_code) {
    case 36: case 76:               // Return, keypad Enter
    case 48: case 51: case 53:      // Tab, Delete, Escape
    case 117: case 115: case 119:   // Forward Delete, Home, End
    case 116: case 121:             // Page Up, Page Down
    case 123: case 124: case 125: case 126: // arrows
    case 122: case 120: case 99: case 118: case 96: case 97: case 98: case 100: case 101: case 109: case 103: case 111: // F1-F12
        return true;
    default:
        return false;
    }
}

void key_down(id self, SEL selector, id event) {
    const auto forward = [&](id forwarded) {
        reinterpret_cast<void (*)(id, SEL, id)>(original_key_down)(self, selector, forwarded);
    };
    if (!original_characters || !original_characters_ignoring_modifiers) return forward(event);
    const auto flags = send<unsigned long>(event, "modifierFlags");
    const auto key_code = send<unsigned short>(event, "keyCode");
    // Cmd+Delete deletes to the start of the line: select to Home, then delete the selection.
    if (key_code == kKeyCodeDelete && (flags & kFlagCommand) && !(flags & (kFlagControl | kFlagOption))) {
        dead_key_state = 0;
        forward(rebuilt_key_event(event, kEventTypeKeyDown, kFlagShift, empty_string(), false, kKeyCodeHome));
        return forward(rebuilt_key_event(event, kEventTypeKeyDown, 0, empty_string(), send<BOOL>(event, "isARepeat")));
    }
    int mapped_key = 0;
    unsigned long mapped_flags = 0;
    if (translate_navigation(key_code, flags, mapped_key, mapped_flags)) {
        dead_key_state = 0;
        return forward(rebuilt_key_event(event, kEventTypeKeyDown, mapped_flags, empty_string(),
            send<BOOL>(event, "isARepeat"), mapped_key));
    }
    if ((flags & kFlagCommand) && !(flags & (kFlagControl | kFlagOption))) {
        // A borderless window has no menu bar entries, so the standard application commands are
        // handled here instead of being forwarded as shortcuts.
        if (key_code == kKeyCodeQ) {
            const id window = send<id>(self, "window");
            const id delegate = window ? send<id>(window, "delegate") : nullptr;
            if (delegate) send<BOOL, id>(delegate, "windowShouldClose:", window);
            return;
        }
        if (key_code == kKeyCodeH) {
            send<void, id>(send<id>(reinterpret_cast<id>(objc_getClass("NSApplication")), "sharedApplication"),
                "hide:", static_cast<id>(nullptr));
            return;
        }
    }
    const id no_text = empty_string();
    if (is_command_key(key_code)) {
        dead_key_state = 0;
        return forward(rebuilt_key_event(event, kEventTypeKeyDown, shortcut_flags(flags), no_text, send<BOOL>(event, "isARepeat")));
    }
    if (flags & (kFlagControl | kFlagCommand)) {
        dead_key_state = 0;
        return forward(rebuilt_key_event(event, kEventTypeKeyDown, shortcut_flags(flags), no_text, send<BOOL>(event, "isARepeat")));
    }
    // The layout translation is only authoritative around dead keys. Otherwise the event's own
    // characters win, because input posted by other software (text expanders, dictation,
    // accessibility tools) carries its text in the event while reusing an arbitrary key code.
    const bool composing = dead_key_state != 0;
    std::vector<UniChar> text;
    if (!translate(key_code, flags, text)) return forward(event);
    if (!composing) {
        text.clear();
        if (dead_key_state == 0) {
            const id reported = reinterpret_cast<id (*)(id, SEL)>(original_characters)(event, sel_registerName("characters"));
            const auto length = reported ? send<unsigned long>(reported, "length") : 0UL;
            for (unsigned long index = 0; index < length; ++index) {
                const auto unit = send<UniChar, unsigned long>(reported, "characterAtIndex:", index);
                if (is_printable(unit)) text.push_back(unit);
            }
        }
    }
    if (text.empty()) {
        // Navigation and editing keys, or a dead key waiting for its base letter.
        return forward(rebuilt_key_event(event, kEventTypeKeyDown, flags, no_text, send<BOOL>(event, "isARepeat")));
    }
    const id characters = reinterpret_cast<id (*)(id, SEL, const UniChar*, unsigned long)>(objc_msgSend)(
        reinterpret_cast<id>(objc_getClass("NSString")), sel_registerName("stringWithCharacters:length:"),
        text.data(), static_cast<unsigned long>(text.size()));
    // Option produced text here, so it is part of the character rather than a shortcut modifier.
    // The repeat flag is dropped because Raven only emits text for non-repeat presses.
    forward(rebuilt_key_event(event, kEventTypeKeyDown, flags & ~kFlagOption, characters, false));
}

void key_up(id self, SEL selector, id event) {
    const auto flags = send<unsigned long>(event, "modifierFlags");
    int mapped_key = 0;
    unsigned long mapped_flags = 0;
    if (original_characters_ignoring_modifiers &&
        translate_navigation(send<unsigned short>(event, "keyCode"), flags, mapped_key, mapped_flags)) {
        reinterpret_cast<void (*)(id, SEL, id)>(original_key_up)(self, selector,
            rebuilt_key_event(event, kEventTypeKeyUp, mapped_flags, empty_string(), false, mapped_key));
        return;
    }
    const id forwarded = (flags & kFlagCommand) && original_characters_ignoring_modifiers
        ? rebuilt_key_event(event, kEventTypeKeyUp, shortcut_flags(flags), empty_string(), false) : event;
    reinterpret_cast<void (*)(id, SEL, id)>(original_key_up)(self, selector, forwarded);
}

} // namespace

void prepare_windowing() {
    const Class window = objc_getClass("NSWindow");
    if (!window || original_can_become_key) return;
    original_can_become_key = replace(window, "canBecomeKeyWindow", reinterpret_cast<IMP>(can_become_key));
    original_can_become_main = replace(window, "canBecomeMainWindow", reinterpret_cast<IMP>(can_become_main));
    if (const Class event = objc_getClass("NSEvent")) {
        original_characters = replace(event, "characters", reinterpret_cast<IMP>(characters));
        original_characters_ignoring_modifiers = replace(event, "charactersIgnoringModifiers",
            reinterpret_cast<IMP>(characters_ignoring_modifiers));
    }
    if (const Class view = objc_getClass("RavenCocoaView")) {
        original_key_down = replace(view, "keyDown:", reinterpret_cast<IMP>(key_down));
        original_key_up = replace(view, "keyUp:", reinterpret_cast<IMP>(key_up));
    }
}

void fit_main_window_to_display() {
    const id application = send<id>(reinterpret_cast<id>(objc_getClass("NSApplication")), "sharedApplication");
    if (!application) return;
    const id windows = send<id>(application, "windows");
    const unsigned long count = windows ? send<unsigned long>(windows, "count") : 0;
    for (unsigned long index = 0; index < count; ++index) {
        const id window = send<id>(windows, "objectAtIndex:", index);
        if (!is_application_window(window)) continue;
        id screen = send<id>(window, "screen");
        if (!screen) screen = send<id>(reinterpret_cast<id>(objc_getClass("NSScreen")), "mainScreen");
        if (!screen) continue;
        // visibleFrame excludes the menu bar and the Dock.
        const CGRect visible = send_rect(screen, "visibleFrame");
        CGRect frame = send_rect(window, "frame");
        if (visible.size.width <= 0 || visible.size.height <= 0) continue;
        frame.size.width = std::min(frame.size.width, visible.size.width);
        frame.size.height = std::min(frame.size.height, visible.size.height);
        frame.origin.x = visible.origin.x + (visible.size.width - frame.size.width) / 2;
        frame.origin.y = visible.origin.y + (visible.size.height - frame.size.height) / 2;
        send<void, CGRect, BOOL>(window, "setFrame:display:", frame, YES);
        send<void, id>(window, "makeKeyAndOrderFront:", static_cast<id>(nullptr));
        // AppKit only promotes a view to first responder on a mouse click, so without this the
        // opening screen and the composer ignore the keyboard until the user clicks somewhere.
        if (const id content = send<id>(window, "contentView")) send<BOOL, id>(window, "makeFirstResponder:", content);
    }
}

} // namespace raven::platform

#else

namespace raven::platform {

void prepare_windowing() {}
void fit_main_window_to_display() {}

} // namespace raven::platform

#endif
