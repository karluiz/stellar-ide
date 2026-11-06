#include "splash.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace raven {
namespace {

struct PoemStanza {
    const char* lines[4];
    int count;
};

constexpr PoemStanza poem[] = {
    {{"Do not go gentle into that good night,",
      "Old age should burn and rave at close of day;",
      "Rage, rage against the dying of the light."}, 3},
    {{"Though wise men at their end know dark is right,",
      "Because their words had forked no lightning they",
      "Do not go gentle into that good night."}, 3},
    {{"Good men, the last wave by, crying how bright",
      "Their frail deeds might have danced in a green bay,",
      "Rage, rage against the dying of the light."}, 3},
    {{"Wild men who caught and sang the sun in flight,",
      "And learn, too late, they grieved it on its way,",
      "Do not go gentle into that good night."}, 3},
    {{"Grave men, near death, who see with blinding sight",
      "Blind eyes could blaze like meteors and be gay,",
      "Rage, rage against the dying of the light."}, 3},
    {{"And you, my father, there on the sad height,",
      "Curse, bless, me now with your fierce tears, I pray.",
      "Do not go gentle into that good night.",
      "Rage, rage against the dying of the light."}, 4}
};

float clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float ease_out(float value) {
    const float t = clamp01(value);
    return 1.0f - std::pow(1.0f - t, 3.0f);
}

float unit(std::uint32_t value) {
    value ^= value >> 16;
    value *= 0x7feb352d;
    value ^= value >> 15;
    value *= 0x846ca68b;
    value ^= value >> 16;
    return static_cast<float>(value & 0x00ffffff) / static_cast<float>(0x01000000);
}

} // namespace

void SplashScreen::load(const std::filesystem::path& ui_directory) {
    auto shader_path = ui_directory / "shaders" / "planet.spv";
#ifdef RAVEN_IDE_BUILD_PLANET_SHADER
    if (std::filesystem::exists(RAVEN_IDE_BUILD_PLANET_SHADER)) shader_path = RAVEN_IDE_BUILD_PLANET_SHADER;
#endif
    shader_ = Rshader_create_from_file(shader_path.c_str(), R_SHADER_ANIMATED);
    font_regular_ = Rfont_create_from_file((ui_directory / "fonts" / "NotoSerifDisplay-Light.ttf").c_str());
    font_italic_ = Rfont_create_from_file((ui_directory / "fonts" / "NotoSerifDisplay-LightItalic.ttf").c_str());
    if (!Rfont_is_valid(font_regular_)) font_regular_ = Rfont_get_default();
    if (!Rfont_is_valid(font_italic_)) font_italic_ = font_regular_;
}

void SplashScreen::begin() {
    time_ = 0.0;
    opacity_ = 1.0f;
    active_ = true;
}

bool SplashScreen::advance(double delta, bool reduce_motion) {
    if (!active_) return false;
    if (reduce_motion_ != reduce_motion) {
        reduce_motion_ = reduce_motion;
        if (reduce_motion_) time_ = 8.0;
        return true;
    }
    if (reduce_motion_ || time_ >= 6.6) return false;
    time_ += std::max(0.0, delta);
    opacity_ = 1.0f;
    return true;
}

void SplashScreen::dismiss() {
    active_ = false;
    opacity_ = 0.0f;
}

bool SplashScreen::event(const REvent* event) {
    if (!active_ || !event) return false;
    const bool click = event->type == R_EVENT_MOUSE_DOWN;
    const bool key = event->type == R_EVENT_KEY_DOWN &&
        (event->key == R_KEY_ENTER || event->key == R_KEY_KP_ENTER || event->key == R_KEY_SPACE || event->key == R_KEY_ESCAPE);
    if (!click && !key) return false;
    dismiss();
    return true;
}

void SplashScreen::paint(Rid, RPainter* painter, float width, float height) const {
    const float time = reduce_motion_ ? 8.0f : static_cast<float>(time_);
    const bool portrait = width / std::max(height, 1.0f) < 1.25f;
    const float margin = std::clamp(width * 0.052f, 48.0f, 132.0f);
    const float intro = ease_out(time / 1.55f);

    Rdraw_rect(painter, 0.0f, 0.0f, width, height, 0.0f, 0.0035f, 0.0045f, 0.0055f, 1.0f);

    for (int i = 0; i < 128; ++i) {
        const float x = unit(static_cast<std::uint32_t>(i) * 3u + 17u) * width;
        const float y = unit(static_cast<std::uint32_t>(i) * 5u + 41u) * height;
        const float depth = unit(static_cast<std::uint32_t>(i) * 7u + 83u);
        const float twinkle = 0.82f + 0.18f * std::sin(time * (0.35f + depth * 0.40f) + static_cast<float>(i));
        const float alpha = (0.10f + depth * 0.34f) * twinkle * intro;
        const float radius = 0.35f + depth * 0.70f;
        Rdraw_circle(painter, x, y, radius, 0.78f, 0.84f, 0.88f, alpha);
    }

    const float hole_scale = 0.955f + ease_out(time / 2.1f) * 0.045f;
    const float hole_size = (portrait ? std::min(width * 1.10f, height * 0.64f)
                                     : std::min(width * 0.68f, height * 1.12f)) * hole_scale;
    const float hole_x = width * (portrait ? 0.60f : 0.74f);
    const float hole_y = height * (portrait ? 0.75f : 0.53f);
    if (Rshader_is_valid(shader_)) {
        RShaderLook look;
        Rshader_look_init(&look);
        look.params[0] = 4.0f;
        look.texture = R_INVALID_ID;
        look.pointer = R_FALSE;
        look.animate = reduce_motion_ ? R_FALSE : R_TRUE;
        Rdraw_shader(painter, shader_, hole_x - hole_size * 0.5f, hole_y - hole_size * 0.5f,
                     hole_size, hole_size, &look);
    }
    Rdraw_rect(painter, 0.0f, 0.0f, width, height, 0.0f, 0.0035f, 0.0045f, 0.0055f, 1.0f - intro);

    const float brand_alpha = ease_out((time - 0.10f) / 0.80f);
    Rdraw_text(painter, R_INVALID_ID, 12.0f, margin, margin * 0.80f, "S T E L L A R",
               0.93f, 0.92f, 0.88f, brand_alpha);
    Rdraw_line(painter, margin, margin * 0.80f + 26.0f, margin + 34.0f, margin * 0.80f + 26.0f,
               1.0f, 0.90f, 0.64f, 0.32f, brand_alpha * 0.72f);

    const float font_size = std::clamp(width * (portrait ? 0.020f : 0.014f), 20.0f, 32.0f);
    const float line_height = font_size * 1.18f;
    const float stanza_gap = font_size * 0.54f;
    constexpr int line_count = 19;
    const float poem_height = line_count * line_height + 5.0f * stanza_gap;
    const float poem_x = margin;
    const float poem_y = portrait ? std::max(108.0f, height * 0.085f)
                                  : std::max(104.0f, (height - poem_height - 58.0f) * 0.5f);
    const float reveal_width = portrait ? width - margin * 2.0f : width * 0.55f;
    const char* prefix = "Do not go ";
    float prefix_width = 0.0f, gentle_width = 0.0f, text_height = 0.0f;
    Rfont_measure_text(font_regular_, font_size, prefix, &prefix_width, &text_height);
    Rfont_measure_text(font_italic_, font_size, "gentle", &gentle_width, &text_height);

    float stanza_y = poem_y;
    for (int stanza = 0; stanza < 6; ++stanza) {
        const float amount = ease_out((time - 0.48f - static_cast<float>(stanza) * 0.78f) / 0.88f);
        if (amount > 0.0f) {
            const float stanza_height = static_cast<float>(poem[stanza].count) * line_height;
            Rdraw_push_clip_rect(painter, poem_x - 3.0f, stanza_y - font_size * 0.18f,
                                 (reveal_width + 8.0f) * amount, stanza_height + font_size * 0.22f);
            for (int line = 0; line < poem[stanza].count; ++line) {
                const float line_y = stanza_y + static_cast<float>(line) * line_height;
                if (stanza == 0 && line == 0) {
                    Rdraw_text(painter, font_regular_, font_size, poem_x, line_y, prefix,
                               0.94f, 0.93f, 0.89f, amount);
                    Rdraw_text(painter, font_italic_, font_size, poem_x + prefix_width, line_y, "gentle",
                               0.98f, 0.89f, 0.71f, amount);
                    Rdraw_text(painter, font_regular_, font_size, poem_x + prefix_width + gentle_width,
                               line_y, " into that good night,", 0.94f, 0.93f, 0.89f, amount);
                } else {
                    Rdraw_text(painter, font_regular_, font_size, poem_x, line_y, poem[stanza].lines[line],
                               0.94f, 0.93f, 0.89f, amount);
                }
            }
            Rdraw_pop_clip_rect(painter);
        }
        stanza_y += static_cast<float>(poem[stanza].count) * line_height + stanza_gap;
    }

    const float author_alpha = ease_out((time - 5.45f) / 0.75f);
    const float author_y = stanza_y + 2.0f;
    Rdraw_line(painter, poem_x, author_y, poem_x + 42.0f, author_y,
               1.0f, 0.90f, 0.64f, 0.32f, author_alpha * 0.65f);
    Rdraw_text(painter, Rfont_get_monospace(), 10.0f, poem_x, author_y + 17.0f, "DYLAN THOMAS · 1951",
               0.67f, 0.65f, 0.59f, author_alpha);
}

} // namespace raven
