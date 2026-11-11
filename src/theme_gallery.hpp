#pragma once

#include <Raven.h>

#include <chrono>
#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>

namespace raven {

using ThemePreviewPalette = std::map<std::string, std::uint32_t>;

class ThemeGallery {
public:
    void load_palettes(const std::filesystem::path& theme_directory);
    bool advance(double delta, bool reduce_motion);
    void set_family(int family, bool animate);
    void set_finish(int finish);
    int move(int delta, bool animate);
    int family() const;
    int event(Rid widget, const REvent* event);
    void paint(Rid widget, RPainter* painter, float width, float height) const;
    void paint_carousel(Rid widget, RPainter* painter, float width, float height);

private:
    double time_ = 0.0;
    double animation_frame_ = 0.0;
    double position_ = 0.0;
    double target_ = 0.0;
    int family_ = 0;
    int finish_ = 0;
    int hovered_direction_ = 0;
    bool tracking_ = false;
    std::chrono::steady_clock::time_point last_wheel_;
    std::array<ThemePreviewPalette, 22> palettes_;
    std::array<Rid, 5> planet_textures_{};
    Rid planet_shader_ = R_INVALID_ID;
    bool reduce_motion_ = false;
    float width_ = 0.0f;
    float height_ = 0.0f;

    int hit_direction(float x, float y) const;
};

} // namespace raven
