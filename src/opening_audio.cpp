#include "opening_audio.hpp"

#include <algorithm>
#include <cstdint>

namespace raven {

OpeningAudio::~OpeningAudio() {
    if (device_ != 0) SDL_CloseAudioDevice(device_);
    if (audio_data_) SDL_FreeWAV(audio_data_);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

bool OpeningAudio::load(const std::filesystem::path& path) {
    if (!std::filesystem::is_regular_file(path)) return false;
    // SDL's audio subsystem pulls in events, which installs SIGINT/SIGTERM handlers that turn the
    // signals into SDL_QUIT events. Stellar never polls SDL events, so Ctrl+C and `kill` would be
    // swallowed and the process could only be stopped with SIGKILL.
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return false;
    if (SDL_LoadWAV(path.string().c_str(), &spec_, &audio_data_, &audio_length_) == nullptr) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }
    // mix() reads and writes native signed 16-bit samples, so anything else must be rejected rather
    // than reinterpreted.
    if (spec_.format != AUDIO_S16SYS || spec_.channels <= 0) {
        SDL_FreeWAV(audio_data_);
        audio_data_ = nullptr;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }
    // SDL reports the length in bytes; position_ indexes samples, and a trailing partial frame is
    // dropped so a whole frame can always be read.
    const Uint32 samples = audio_length_ / static_cast<Uint32>(sizeof(std::int16_t));
    sample_count_ = samples - samples % static_cast<Uint32>(spec_.channels);
    spec_.callback = audio_callback;
    spec_.userdata = this;
    device_ = SDL_OpenAudioDevice(nullptr, 0, &spec_, nullptr, 0);
    if (device_ == 0) {
        SDL_FreeWAV(audio_data_);
        audio_data_ = nullptr;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }
    SDL_PauseAudioDevice(device_, 1);
    return true;
}

void OpeningAudio::play() {
    if (device_ == 0) return;
    SDL_LockAudioDevice(device_);
    if (position_ >= sample_count_) position_ = 0;
    playing_.store(true);
    target_gain_.store(0.45f);
    SDL_UnlockAudioDevice(device_);
    SDL_PauseAudioDevice(device_, 0);
}

void OpeningAudio::stop() {
    target_gain_.store(0.0f);
}

void OpeningAudio::audio_callback(void* user, Uint8* stream, int length) {
    static_cast<OpeningAudio*>(user)->mix(stream, length);
}

void OpeningAudio::mix(Uint8* stream, int length) {
    SDL_memset(stream, 0, static_cast<size_t>(length));
    if (!playing_.load() || spec_.channels <= 0) return;

    auto* output = reinterpret_cast<std::int16_t*>(stream);
    const auto* input = reinterpret_cast<const std::int16_t*>(audio_data_);
    const int channels = spec_.channels;
    const int frames = length / static_cast<int>(sizeof(std::int16_t) * channels);
    const Uint32 stride = static_cast<Uint32>(channels);
    const float target = target_gain_.load();
    const float fade_step = 1.0f / (static_cast<float>(spec_.freq) * (target > gain_ ? 2.8f : 0.65f));

    for (int frame = 0; frame < frames; ++frame) {
        if (position_ >= sample_count_ && target > 0.0f) position_ = 0;
        const bool has_input = position_ < sample_count_;
        for (int channel = 0; channel < channels; ++channel) {
            const float sample = has_input ? static_cast<float>(input[position_ + channel]) : 0.0f;
            output[frame * channels + channel] = static_cast<std::int16_t>(std::clamp(sample * gain_, -32768.0f, 32767.0f));
        }
        if (has_input) position_ += stride;
        if (target > gain_) gain_ = std::min(target, gain_ + fade_step);
        else gain_ = std::max(target, gain_ - fade_step);
        if (target == 0.0f && gain_ == 0.0f) {
            playing_.store(false);
            break;
        }
    }
}

} // namespace raven
