#include "app.hpp"
#include "platform.hpp"

#include <Raven.h>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <thread>

namespace {

void on_error(RError code, RSeverity severity, const char* detail) {
    std::fprintf(stderr, "[Raven] %s (%s): %s\n", Rerror_to_string(code), Rseverity_to_string(severity), detail);
}

void capture(Rid window, const char* path) {
    int width = 0, height = 0;
    uint8_t* pixels = Rwindow_capture(window, &width, &height);
    FILE* file = pixels ? std::fopen(path, "wb") : nullptr;
    if (!file) {
        Rpixels_free(pixels);
        return;
    }
    std::fprintf(file, "P6\n%d %d\n255\n", width, height);
    for (int i = 0; i < width * height; ++i) std::fwrite(pixels + i * 4, 1, 3, file);
    std::fclose(file);
    Rpixels_free(pixels);
}

std::atomic<bool> quit_requested{false};

// SIGINT and SIGTERM are blocked process-wide and received by one thread, which asks the main loop
// to quit. A default-action kill would skip the App destructor, leaving agent process groups
// running in the workspace and pending saves unwritten. Child processes restore the default mask.
void handle_termination_signals() {
    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGINT);
    sigaddset(&signals, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &signals, nullptr);
    std::thread([signals] {
        int received = 0;
        while (sigwait(&signals, &received) == 0) {
            if (quit_requested.exchange(true)) std::_Exit(128 + received); // second signal: stop now
            // Rquit is thread-safe and also ends nested loops such as an open file dialog, which
            // never return to the main loop's check below.
            Rquit(0);
            Rwake_up();
        }
    }).detach();
}

} // namespace

int main(int argc, char** argv) {
    std::filesystem::path workspace = std::filesystem::current_path();
    std::string prompt;
    std::string workflow_task;
    std::filesystem::path open_file;
    std::optional<raven::Harness> harness;
    std::optional<int> theme;
    std::optional<int> mode;
    bool watch = false;
    bool endurance = false;
    bool memory = false;
    bool tesseract = false;
    bool demo = false;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--workspace" && i + 1 < argc) workspace = argv[++i];
        else if (argument == "--prompt" && i + 1 < argc) prompt = argv[++i];
        else if (argument == "--workflow-task" && i + 1 < argc) workflow_task = argv[++i];
        else if (argument == "--open-file" && i + 1 < argc) open_file = argv[++i];
        else if (argument == "--theme" && i + 1 < argc) {
            const std::string value = argv[++i];
            if (value == "earth") theme = 12;
            else if (value == "earth-light") theme = 13;
            else if (value == "earth-glass") theme = 14;
            else if (value == "miller") theme = 3;
            else if (value == "miller-light") theme = 8;
            else if (value == "miller-glass") theme = 15;
            else if (value == "mann") theme = 4;
            else if (value == "mann-dark") theme = 9;
            else if (value == "mann-glass") theme = 16;
            else if (value == "edmunds") theme = 5;
            else if (value == "edmunds-dark") theme = 10;
            else if (value == "edmunds-glass") theme = 17;
            else if (value == "gargantua") theme = 6;
            else if (value == "gargantua-light") theme = 11;
            else if (value == "gargantua-glass") theme = 18;
            else if (value == "wormhole") theme = 19;
            else if (value == "wormhole-light") theme = 20;
            else if (value == "wormhole-glass") theme = 21;
            else {
                std::cerr << "Unknown theme: " << value << " (expected a supported planet variant, for example earth, mann-dark, or miller-glass)\n";
                return 2;
            }
        }
        else if (argument == "--harness" && i + 1 < argc) {
            const std::string value = argv[++i];
            if (value == "kimi") harness = raven::Harness::KimiCode;
            else if (value == "claude") harness = raven::Harness::ClaudeCode;
            else {
                std::cerr << "Unknown harness: " << value << " (expected claude or kimi)\n";
                return 2;
            }
        }
        else if (argument == "--mode" && i + 1 < argc) {
            const std::string value = argv[++i];
            if (value == "build") mode = 0;
            else if (value == "ask") mode = 1;
            else if (value == "review") mode = 2;
            else {
                std::cerr << "Unknown mode: " << value << " (expected build, ask, or review)\n";
                return 2;
            }
        }
        else if (argument == "--watch") watch = true;
        else if (argument == "--endurance") endurance = true;
        else if (argument == "--memory") memory = true;
        else if (argument == "--tesseract") tesseract = true;
        else if (argument == "--demo") demo = true;
        else if (argument == "--help") {
            std::cout << "raven-ide [--workspace PATH] [--harness claude|kimi] [--mode build|ask|review] [--watch] [--endurance] [--memory] [--tesseract] [--theme earth|earth-light|earth-glass|miller|miller-light|miller-glass|mann|mann-dark|mann-glass|edmunds|edmunds-dark|edmunds-glass|gargantua|gargantua-light|gargantua-glass|wormhole|wormhole-light|wormhole-glass] [--prompt TEXT] [--workflow-task TEXT] [--open-file PATH] [--demo]\n";
            return 0;
        } else if (!argument.empty() && argument[0] != '-') {
            workspace = argument;
        }
    }
    if (!workflow_task.empty() && !prompt.empty()) {
        std::cerr << "--workflow-task and --prompt are mutually exclusive\n";
        return 2;
    }

    raven::platform::extend_search_path();
    handle_termination_signals();
    Rerror_set_callback(on_error);
    raven::platform::prepare_windowing();
    RInitDesc desc{};
    desc.backend = R_BACKEND_VULKAN;
    desc.app_name = "Stellar";
    if (!Rinit_ex(&desc)) {
        std::cerr << "Cannot start Raven. Check the X11 display and Vulkan driver.\n";
        return 1;
    }
    RAccessibilitySettings accessibility;
    Raccessibility_get_system_settings(&accessibility);
    Raccessibility_set_reduce_motion(accessibility.reduce_motion);
    const std::filesystem::path ui_dir = std::filesystem::exists(RAVEN_IDE_UI_DIR "/main.xml")
        ? RAVEN_IDE_UI_DIR : RAVEN_IDE_INSTALLED_UI_DIR;
    static const char* theme_files[] = {
        "earth-planet.xml",
        "earth-planet-light.xml",
        "earth-planet-glass.xml",
        "miller-planet.xml",
        "miller-planet-light.xml",
        "miller-planet-glass.xml",
        "mann-planet.xml",
        "mann-planet-dark.xml",
        "mann-planet-glass.xml",
        "edmunds-planet.xml",
        "edmunds-planet-dark.xml",
        "edmunds-planet-glass.xml",
        "gargantua.xml",
        "gargantua-light.xml",
        "gargantua-glass.xml",
        "wormhole.xml",
        "wormhole-light.xml",
        "wormhole-glass.xml"
    };
    for (const auto* file : theme_files) {
        const auto path = ui_dir / "themes" / file;
        if (!Rtheme_load(path.c_str())) {
            Rshutdown();
            return 1;
        }
    }
    if (!Rtheme_use("EarthPlanet")) {
        Rshutdown();
        return 1;
    }

    int result = 0;
    {
        raven::App app(workspace, demo);
        if (!app.create()) {
            result = 1;
        } else {
            raven::platform::fit_main_window_to_display();
            if (harness) app.select_harness(*harness);
            if (mode) app.select_run_mode(*mode);
            if (watch) app.select_watch_checks(true);
            if (theme) app.select_theme(*theme);
            if (!open_file.empty()) app.open_code_file(open_file);
            if (!workflow_task.empty()) app.start_endurance_with_task(workflow_task);
            else if (endurance && prompt.empty()) app.open_endurance();
            else if (memory && prompt.empty()) app.open_memory();
            else if (tesseract && prompt.empty()) app.open_tesseract();
            if (!prompt.empty()) {
                if (tesseract) app.start_tesseract_with_prompt(prompt);
                else app.start_with_prompt(prompt);
            }
            else if (workflow_task.empty() && !memory && !tesseract) app.play_opening_score();
            const char* frames = std::getenv("RAVEN_IDE_FRAMES");
            if (frames) {
                const int count = std::atoi(frames);
                for (int i = 0; i < count && Ris_running(); ++i) {
                    Rwindow_request_redraw(app.window());
                    Rwait_frame(0.02);
                }
                if (const char* path = std::getenv("RAVEN_IDE_CAPTURE")) capture(app.window(), path);
            } else {
                while (Rwait_frame(app.has_animation_frames() ? 1.0 / 30.0 : app.has_run_timers() ? 0.25 : -1.0)) {
                    if (quit_requested.load()) Rquit(0);
                }
            }
        }
    }
    Rshutdown();
    return result;
}
