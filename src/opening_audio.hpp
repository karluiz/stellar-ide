#pragma once

#include <SDL.h>

#include <atomic>
#include <cstddef>
#include <filesystem>

namespace raven {

class OpeningAudio {
public:
    OpeningAudio() = default;
    ~OpeningAudio();

    OpeningAudio(const OpeningAudio&) = delete;
    OpeningAudio& operator=(const OpeningAudio&) = delete;

    bool load(const std::filesystem::path& path);
    void play();
    void stop();
    bool available() const { return device_ != 0; }
    bool playing() const { return playing_.load(); }

private:
    static void audio_callback(void* user, Uint8* stream, int length);
    void mix(Uint8* stream, int length);

    SDL_AudioDeviceID device_ = 0;
    SDL_AudioSpec spec_{};
    Uint8* audio_data_ = nullptr;
    Uint32 audio_length_ = 0;
    Uint32 sample_count_ = 0;
    Uint32 position_ = 0;
    float gain_ = 0.0f;
    std::atomic<float> target_gain_{0.0f};
    std::atomic<bool> playing_{false};
};

} // namespace raven
