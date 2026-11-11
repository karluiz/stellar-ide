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

} // namespace raven::platform
