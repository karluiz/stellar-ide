#pragma once

namespace raven::platform {

// Call once before Rinit_ex(). Applies window-system adjustments the host platform needs before any
// window exists. No-op outside macOS.
void prepare_windowing();

// Call after the main window exists. Keeps it inside the visible area of its display, since the
// requested size is scaled by the interface zoom and can exceed a laptop screen. No-op outside macOS.
void fit_main_window_to_display();

// Call once at startup on every platform. A desktop launch (Finder, a .desktop entry) inherits a
// minimal PATH without the directories where agent CLIs, compilers and git are usually installed.
// Appends the common per-user and package-manager locations that exist and are not already listed.
void extend_search_path();

// Trackpad pinch accumulated since the last call: a zoom factor (1 when there was none) and where
// the gesture happened, in window pixels (the units of Raven mouse events). No-op outside macOS.
struct PinchGesture {
    float factor = 1.0f;
    float x = 0.0f;
    float y = 0.0f;
};
PinchGesture take_pinch();

// True when the latest scroll came from a trackpad or another device with precise deltas, where a
// two-finger swipe means "move the view", not "turn a wheel". False outside macOS.
bool precise_scrolling();

} // namespace raven::platform
