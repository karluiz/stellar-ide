#pragma once

#include "knowledge.hpp"

#include <Raven.h>

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace raven {

class MemoryUniverse {
public:
    struct Stats {
        size_t entities = 0;
        size_t relations = 0;
        size_t episodes = 0;
    };

    void set_data(const std::vector<GraphOperation>& entities, const std::vector<GraphOperation>& relations);
    bool advance(double delta, bool reduce_motion);
    void fit();
    Stats stats() const;
    const std::string& selected_id() const;
    std::string selected_markdown() const;
    void paint(Rid widget, RPainter* painter, float width, float height);
    int event(Rid widget, const REvent* event);

private:
    struct Body {
        GraphOperation record;
        float orbit = 0.0f;
        float angle = 0.0f;
        float radius = 9.0f;
        size_t degree = 0;
        bool episode = false;
    };

    std::vector<Body> bodies_;
    std::vector<GraphOperation> relations_;
    std::string selected_id_;
    int hovered_ = -1;
    float zoom_ = 1.0f;
    float pan_x_ = 0.0f;
    float pan_y_ = 0.0f;
    float width_ = 900.0f;
    float height_ = 700.0f;
    float down_x_ = 0.0f;
    float down_y_ = 0.0f;
    double time_ = 0.0;
    double animation_frame_ = 0.0;
    bool tracking_ = false;
    bool dragging_ = false;
    bool fitted_ = false;

    void layout();
    std::pair<float, float> position(const Body& body) const;
    int hit(float x, float y) const;
    void zoom_at(float x, float y, float factor);
};

} // namespace raven
