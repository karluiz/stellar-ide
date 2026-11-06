#pragma once

#include <Raven.h>

#include <filesystem>

namespace raven {

class SplashScreen {
public:
    void load(const std::filesystem::path& ui_directory);
    void begin();
    bool advance(double delta, bool reduce_motion);
    void dismiss();
    bool active() const { return active_; }
    bool animating() const { return active_ && !reduce_motion_ && time_ < 6.6; }
    float opacity() const { return opacity_; }
    bool event(const REvent* event);
    void paint(Rid widget, RPainter* painter, float width, float height) const;

private:
    double time_ = 0.0;
    float opacity_ = 1.0f;
    bool active_ = false;
    bool reduce_motion_ = false;
    Rid shader_ = R_INVALID_ID;
    Rid font_regular_ = R_INVALID_ID;
    Rid font_italic_ = R_INVALID_ID;
};

} // namespace raven
