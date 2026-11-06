#include "memory.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <map>
#include <sstream>

namespace raven {
namespace {

constexpr float pi = 3.14159265358979323846f;

struct Color {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
};

Color color(Rid widget, RColorRole role, float alpha = 1.0f) {
    Color value;
    Rwidget_get_color(widget, role, &value.r, &value.g, &value.b, &value.a);
    value.a *= alpha;
    return value;
}

void circle(RPainter* painter, float x, float y, float radius, const Color& value) {
    Rdraw_circle(painter, x, y, radius, value.r, value.g, value.b, value.a);
}

void line(RPainter* painter, float x0, float y0, float x1, float y1, float thickness, const Color& value) {
    Rdraw_line(painter, x0, y0, x1, y1, thickness, value.r, value.g, value.b, value.a);
}

void circle_outline(RPainter* painter, float x, float y, float radius, float thickness, const Color& value, bool dashed = false) {
    constexpr int segments = 96;
    for (int i = 0; i < segments; ++i) {
        if (dashed && i % 8 >= 5) continue;
        const float a0 = static_cast<float>(i) * 2.0f * pi / segments;
        const float a1 = static_cast<float>(i + 1) * 2.0f * pi / segments;
        line(painter, x + std::cos(a0) * radius, y + std::sin(a0) * radius,
             x + std::cos(a1) * radius, y + std::sin(a1) * radius, thickness, value);
    }
}

void ellipse(RPainter* painter, float x, float y, float rx, float ry, float rotation,
             float thickness, const Color& value, bool upper_only = false) {
    constexpr int segments = 80;
    const int first = upper_only ? segments / 2 : 0;
    const int last = upper_only ? segments : segments;
    for (int i = first; i < last; ++i) {
        if (upper_only && i >= segments) break;
        const float a0 = static_cast<float>(i) * 2.0f * pi / segments;
        const float a1 = static_cast<float>(i + 1) * 2.0f * pi / segments;
        const auto point = [&](float angle) {
            const float px = std::cos(angle) * rx;
            const float py = std::sin(angle) * ry;
            return std::pair<float, float>{x + px * std::cos(rotation) - py * std::sin(rotation),
                                           y + px * std::sin(rotation) + py * std::cos(rotation)};
        };
        const auto p0 = point(a0);
        const auto p1 = point(a1);
        line(painter, p0.first, p0.second, p1.first, p1.second, thickness, value);
    }
}

uint32_t hash(const std::string& value) {
    uint32_t result = 2166136261u;
    for (const unsigned char c : value) {
        result ^= c;
        result *= 16777619u;
    }
    return result;
}

std::string shortened(const std::string& value, size_t length = 22) {
    return value.size() <= length ? value : value.substr(0, length - 1) + "…";
}

void centered_text(RPainter* painter, float x, float y, const std::string& text, float size, const Color& value) {
    float width = 0.0f;
    float height = 0.0f;
    Rfont_measure_text(R_INVALID_ID, size, text.c_str(), &width, &height);
    Rdraw_text(painter, R_INVALID_ID, size, x - width * 0.5f, y, text.c_str(), value.r, value.g, value.b, value.a);
}

} // namespace

void MemoryUniverse::set_data(const std::vector<GraphOperation>& entities, const std::vector<GraphOperation>& relations) {
    bodies_.clear();
    relations_ = relations;
    for (const auto& entity : entities) {
        if (entity.kind != GraphOpKind::UpsertEntity) continue;
        Body body;
        body.record = entity;
        body.episode = entity.entity_type == "episode";
        body.degree = static_cast<size_t>(std::count_if(relations.begin(), relations.end(), [&](const auto& relation) {
            return relation.source_id == entity.id || relation.target_id == entity.id;
        }));
        body.radius = (body.episode ? 11.0f : 8.0f) + std::min<size_t>(body.degree, 4);
        bodies_.push_back(std::move(body));
    }
    std::sort(bodies_.begin(), bodies_.end(), [](const auto& left, const auto& right) {
        if (left.episode != right.episode) return left.episode > right.episode;
        return left.record.id < right.record.id;
    });
    if (!selected_id_.empty() && std::none_of(bodies_.begin(), bodies_.end(), [&](const auto& body) {
        return body.record.id == selected_id_;
    })) selected_id_.clear();
    hovered_ = -1;
    tracking_ = false;
    dragging_ = false;
    fitted_ = false;
    layout();
    fit();
}

void MemoryUniverse::layout() {
    const auto episodes = static_cast<size_t>(std::count_if(bodies_.begin(), bodies_.end(), [](const auto& body) {
        return body.episode;
    }));
    size_t episode_index = 0;
    size_t entity_index = 0;
    const size_t entities = bodies_.size() - episodes;
    for (auto& body : bodies_) {
        if (body.episode) {
            const float band = episodes > 8 ? static_cast<float>(episode_index % 2) : 0.0f;
            body.orbit = 142.0f + band * 44.0f;
            body.angle = -pi * 0.5f + static_cast<float>(episode_index) * 2.0f * pi / std::max<size_t>(episodes, 1) + band * 0.35f;
            ++episode_index;
        } else {
            const float band = entities > 12 ? static_cast<float>(entity_index % 3) : static_cast<float>(entity_index % 2);
            body.orbit = 270.0f + band * 48.0f + static_cast<float>(hash(body.record.id) % 17);
            body.angle = -pi * 0.5f + static_cast<float>(entity_index) * 2.39996323f;
            ++entity_index;
        }
    }
}

bool MemoryUniverse::advance(double delta, bool reduce_motion) {
    if (reduce_motion) return false;
    time_ += std::max(delta, 0.0);
    if (time_ - animation_frame_ < 1.0 / 30.0) return false;
    animation_frame_ = time_;
    return true;
}

void MemoryUniverse::fit() {
    zoom_ = std::clamp(std::min(width_ / 900.0f, height_ / 700.0f), 0.45f, 1.4f);
    pan_x_ = 0.0f;
    pan_y_ = 0.0f;
    fitted_ = true;
}

MemoryUniverse::Stats MemoryUniverse::stats() const {
    return {bodies_.size(), relations_.size(), static_cast<size_t>(std::count_if(bodies_.begin(), bodies_.end(), [](const auto& body) {
        return body.episode;
    }))};
}

const std::string& MemoryUniverse::selected_id() const { return selected_id_; }

std::string MemoryUniverse::selected_markdown() const {
    const auto selected = std::find_if(bodies_.begin(), bodies_.end(), [&](const auto& body) {
        return body.record.id == selected_id_;
    });
    if (selected == bodies_.end()) return {};
    const auto& record = selected->record;
    std::map<std::string, std::string> names;
    for (const auto& body : bodies_) names[body.record.id] = body.record.name;
    std::ostringstream output;
    output << "## " << record.name << "\n\n`" << record.entity_type << "` · `" << record.id << "`\n\n";
    output << "Source: `" << record.source << "` · confidence `" << std::fixed << std::setprecision(2) << record.confidence << "`\n\n";
    for (const auto& [key, value] : record.properties) {
        if (key != "transcript") output << "**" << key << "**  \n" << value << "\n\n";
    }
    std::vector<std::string> linked;
    for (const auto& relation : relations_) {
        if (relation.source_id == record.id) {
            linked.push_back("**" + relation.entity_type + "** → " + names[relation.target_id] + "  \n`" + relation.target_id + "`");
        } else if (relation.target_id == record.id) {
            linked.push_back(names[relation.source_id] + " **" + relation.entity_type + "** → this memory  \n`" + relation.source_id + "`");
        }
    }
    if (!linked.empty()) {
        output << "### Relations\n\n";
        for (const auto& relation : linked) output << relation << "\n\n";
    }
    const auto transcript = record.properties.find("transcript");
    if (transcript != record.properties.end()) {
        output << "### Remembered transcript\n\n```text\n";
        if (transcript->second.size() <= 5'000) output << transcript->second;
        else output << transcript->second.substr(0, 5'000) << "\n… transcript truncated in this view …";
        output << "\n```";
    }
    return output.str();
}

std::pair<float, float> MemoryUniverse::position(const Body& body) const {
    const float speed = (body.episode ? 0.10f : 0.055f) * (280.0f / body.orbit);
    const float angle = body.angle + static_cast<float>(time_) * speed;
    const float pull = 1.0f + 0.018f * std::sin(static_cast<float>(time_) * 0.7f + body.angle * 2.0f);
    return {width_ * 0.5f + pan_x_ + std::cos(angle) * body.orbit * pull * zoom_,
            height_ * 0.5f + pan_y_ + std::sin(angle) * body.orbit * pull * zoom_};
}

int MemoryUniverse::hit(float x, float y) const {
    for (int i = static_cast<int>(bodies_.size()) - 1; i >= 0; --i) {
        const auto [bx, by] = position(bodies_[i]);
        const float radius = std::max(8.0f, bodies_[i].radius * zoom_) + 7.0f;
        const float dx = x - bx;
        const float dy = y - by;
        if (dx * dx + dy * dy <= radius * radius) return i;
    }
    return -1;
}

void MemoryUniverse::zoom_at(float x, float y, float factor) {
    const float next = std::clamp(zoom_ * factor, 0.4f, 3.0f);
    const float world_x = (x - width_ * 0.5f - pan_x_) / zoom_;
    const float world_y = (y - height_ * 0.5f - pan_y_) / zoom_;
    pan_x_ = x - width_ * 0.5f - world_x * next;
    pan_y_ = y - height_ * 0.5f - world_y * next;
    zoom_ = next;
}

void MemoryUniverse::paint(Rid widget, RPainter* painter, float width, float height) {
    width_ = width;
    height_ = height;
    if (!fitted_) fit();
    const auto content = Rwindow_get_content(Rwidget_get_window(widget));
    const auto window = color(content, R_COLOR_WINDOW);
    const auto text = color(widget, R_COLOR_TEXT);
    const auto muted = color(widget, R_COLOR_TEXT_PLACEHOLDER);
    const auto accent = color(widget, R_COLOR_ACCENT);
    const auto link = color(widget, R_COLOR_LINK);
    const auto success = color(widget, R_COLOR_SUCCESS);
    const float center_x = width * 0.5f + pan_x_;
    const float center_y = height * 0.5f + pan_y_;
    Rdraw_rect(painter, 0.0f, 0.0f, width, height, 0.0f, window.r, window.g, window.b, 1.0f);

    for (int i = 0; i < 110; ++i) {
        const uint32_t value = hash("stellar:" + std::to_string(i));
        const float x = static_cast<float>(value % 10'000) / 10'000.0f * width;
        const float y = static_cast<float>((value / 10'000) % 10'000) / 10'000.0f * height;
        const float alpha = 0.12f + static_cast<float>((value >> 8) % 12) / 100.0f;
        circle(painter, x, y, 0.45f + static_cast<float>(value % 3) * 0.25f, color(widget, R_COLOR_TEXT, alpha));
    }

    const float clock = static_cast<float>(time_);
    const float pulse = 1.0f + 0.025f * std::sin(clock * 0.9f);
    const float disk_rotation = -0.18f + 0.035f * std::sin(clock * 0.23f);
    for (int ring = 4; ring >= 0; --ring) {
        ellipse(painter, center_x, center_y, (62.0f + ring * 12.0f) * zoom_ * pulse, (13.0f + ring * 3.0f) * zoom_ * pulse,
                disk_rotation, ring == 0 ? 2.2f : 1.0f, color(widget, ring % 2 ? R_COLOR_LINK : R_COLOR_ACCENT, 0.32f + (4 - ring) * 0.09f));
    }
    for (int i = 0; i < 24; ++i) {
        const float phase = clock * (0.32f + static_cast<float>(i % 5) * 0.025f) + static_cast<float>(i) * 2.39996323f;
        const float orbit = (58.0f + static_cast<float>(i % 7) * 7.0f + 4.0f * std::sin(clock * 0.65f + static_cast<float>(i))) * zoom_;
        const float px = std::cos(phase) * orbit;
        const float py = std::sin(phase) * orbit * 0.22f;
        circle(painter, center_x + px * std::cos(disk_rotation) - py * std::sin(disk_rotation),
               center_y + px * std::sin(disk_rotation) + py * std::cos(disk_rotation),
               std::max(0.55f, (0.7f + static_cast<float>(i % 3) * 0.3f) * zoom_),
               color(widget, i % 2 ? R_COLOR_LINK : R_COLOR_ACCENT, 0.45f + static_cast<float>(i % 4) * 0.1f));
    }
    circle(painter, center_x + 2.0f, center_y + 3.0f, 43.0f * zoom_ * pulse, color(widget, R_COLOR_CONTROL_BORDER, 0.4f));
    circle(painter, center_x, center_y, 37.0f * zoom_ * pulse, {0.005f, 0.008f, 0.012f, 1.0f});
    ellipse(painter, center_x, center_y, 62.0f * zoom_ * pulse, 13.0f * zoom_ * pulse, disk_rotation, 2.0f,
            color(widget, R_COLOR_ACCENT, 0.9f), true);
    centered_text(painter, center_x, center_y + 51.0f * zoom_, "WORKSPACE", 11.0f, text);
    centered_text(painter, center_x, center_y + 68.0f * zoom_, "event horizon", 10.0f, muted);

    std::vector<float> orbits;
    for (const auto& body : bodies_) {
        if (std::find_if(orbits.begin(), orbits.end(), [&](float value) { return std::abs(value - body.orbit) < 0.5f; }) == orbits.end()) {
            orbits.push_back(body.orbit);
        }
    }
    for (const float orbit : orbits) circle_outline(painter, center_x, center_y, orbit * zoom_, 0.75f,
                                                     color(widget, R_COLOR_CONTROL_BORDER, 0.48f), true);

    std::map<std::string, size_t> indices;
    for (size_t i = 0; i < bodies_.size(); ++i) indices[bodies_[i].record.id] = i;
    for (const auto& relation : relations_) {
        const auto source = indices.find(relation.source_id);
        const auto target = indices.find(relation.target_id);
        if (source == indices.end() || target == indices.end()) continue;
        const auto from = position(bodies_[source->second]);
        const auto to = position(bodies_[target->second]);
        const bool active = selected_id_ == relation.source_id || selected_id_ == relation.target_id;
        line(painter, from.first, from.second, to.first, to.second, active ? 1.7f : 0.85f,
             color(widget, active ? R_COLOR_ACCENT : R_COLOR_LINK, active ? 0.9f : 0.38f));
    }

    for (size_t i = 0; i < bodies_.size(); ++i) {
        const auto& body = bodies_[i];
        const auto [x, y] = position(body);
        const float radius = std::max(6.0f, body.radius * zoom_);
        const bool selected = body.record.id == selected_id_;
        const bool hovered = static_cast<int>(i) == hovered_;
        if (selected || hovered) {
            circle(painter, x, y, radius + (selected ? 10.0f : 7.0f), color(widget, R_COLOR_ACCENT, selected ? 0.17f : 0.10f));
            circle_outline(painter, x, y, radius + (selected ? 7.0f : 4.0f), selected ? 1.5f : 1.0f,
                           color(widget, R_COLOR_ACCENT, selected ? 0.9f : 0.55f));
        }
        circle(painter, x + 1.5f, y + 2.0f, radius + 1.0f, color(widget, R_COLOR_CONTROL_BORDER, 0.65f));
        const auto fill = body.episode ? accent : (hash(body.record.entity_type) % 2 ? link : success);
        circle(painter, x, y, radius, fill);
        circle(painter, x - radius * 0.28f, y - radius * 0.30f, radius * 0.48f, color(widget, R_COLOR_ACCENT_TEXT, 0.17f));
        if (body.episode) circle_outline(painter, x, y, radius + 3.0f, 0.8f, color(widget, R_COLOR_LINK, 0.75f));
        for (size_t moon = 0; moon < std::min<size_t>(body.degree, 3); ++moon) {
            const float angle = static_cast<float>(moon) * 2.0f * pi / 3.0f + static_cast<float>(hash(body.record.id) % 100) / 100.0f;
            circle(painter, x + std::cos(angle) * (radius + 6.0f), y + std::sin(angle) * (radius + 6.0f), 1.6f,
                   color(widget, R_COLOR_TEXT, 0.55f));
        }
        if (bodies_.size() <= 36 || body.episode || selected || hovered) {
            const auto label = shortened(body.record.name);
            float label_width = 0.0f;
            float label_height = 0.0f;
            Rfont_measure_text(R_INVALID_ID, 11.0f, label.c_str(), &label_width, &label_height);
            const float label_x = std::clamp(x - label_width * 0.5f, 8.0f, std::max(8.0f, width - label_width - 8.0f));
            const float label_y = y + radius + 8.0f;
            Rdraw_rect(painter, label_x - 4.0f, label_y - 2.0f, label_width + 8.0f, label_height + 4.0f,
                       4.0f, window.r, window.g, window.b, 0.88f);
            Rdraw_text(painter, R_INVALID_ID, 11.0f, label_x, label_y, label.c_str(),
                       text.r, text.g, text.b, 1.0f);
        }
    }

    if (bodies_.empty()) {
        centered_text(painter, center_x, center_y + 112.0f * zoom_, "No reviewed memories yet", 18.0f, text);
        centered_text(painter, center_x, center_y + 140.0f * zoom_, "Complete a run, then accept its episode in the review.", 12.0f, muted);
        centered_text(painter, center_x, center_y + 160.0f * zoom_, "Accepted memories will form your workspace universe.", 12.0f, muted);
    }
    Rdraw_text(painter, R_INVALID_ID, 11.0f, 16.0f, height - 26.0f,
               "Drag to navigate  ·  Scroll to zoom  ·  Select a planet", muted.r, muted.g, muted.b, 1.0f);
}

int MemoryUniverse::event(Rid widget, const REvent* event) {
    switch (event->type) {
    case R_EVENT_MOUSE_DOWN:
        Rwidget_focus(widget);
        if (event->button != R_MOUSE_LEFT) return R_TRUE;
        tracking_ = true;
        dragging_ = false;
        down_x_ = event->local_x;
        down_y_ = event->local_y;
        Rwidget_set_cursor(widget, hit(event->local_x, event->local_y) >= 0 ? R_CURSOR_HAND : R_CURSOR_MOVE);
        return R_TRUE;
    case R_EVENT_MOUSE_MOVE:
        if (tracking_) {
            const float dx = event->local_x - down_x_;
            const float dy = event->local_y - down_y_;
            if (dx * dx + dy * dy > 12.0f) dragging_ = true;
            if (dragging_) {
                pan_x_ += event->delta_x;
                pan_y_ += event->delta_y;
                Rwidget_set_cursor(widget, R_CURSOR_MOVE);
                Rwidget_invalidate(widget);
            }
            return R_TRUE;
        } else {
            const int next = hit(event->local_x, event->local_y);
            if (next != hovered_) {
                hovered_ = next;
                Rwidget_invalidate(widget);
            }
            Rwidget_set_cursor(widget, hovered_ >= 0 ? R_CURSOR_HAND : R_CURSOR_ARROW);
        }
        return R_TRUE;
    case R_EVENT_MOUSE_UP:
        if (event->button != R_MOUSE_LEFT) return R_TRUE;
        if (tracking_ && !dragging_) {
            const int selected = hit(event->local_x, event->local_y);
            selected_id_ = selected >= 0 ? bodies_[selected].record.id : "";
        }
        tracking_ = false;
        dragging_ = false;
        hovered_ = hit(event->local_x, event->local_y);
        Rwidget_set_cursor(widget, hovered_ >= 0 ? R_CURSOR_HAND : R_CURSOR_ARROW);
        Rwidget_invalidate(widget);
        return R_TRUE;
    case R_EVENT_MOUSE_WHEEL:
        zoom_at(event->local_x, event->local_y, std::exp(event->wheel_y * 0.12f));
        Rwidget_invalidate(widget);
        return R_TRUE;
    case R_EVENT_MOUSE_LEAVE:
        if (!tracking_) {
            hovered_ = -1;
            Rwidget_invalidate(widget);
        }
        return tracking_ ? R_TRUE : R_FALSE;
    case R_EVENT_KEY_DOWN:
        if (event->key == R_KEY_0) fit();
        else if (event->key == R_KEY_EQUAL || event->key == R_KEY_KP_ADD) zoom_at(width_ * 0.5f, height_ * 0.5f, 1.15f);
        else if (event->key == R_KEY_MINUS || event->key == R_KEY_KP_SUBTRACT) zoom_at(width_ * 0.5f, height_ * 0.5f, 1.0f / 1.15f);
        else if (event->key == R_KEY_LEFT) pan_x_ += 30.0f;
        else if (event->key == R_KEY_RIGHT) pan_x_ -= 30.0f;
        else if (event->key == R_KEY_UP) pan_y_ += 30.0f;
        else if (event->key == R_KEY_DOWN) pan_y_ -= 30.0f;
        else return R_FALSE;
        Rwidget_invalidate(widget);
        return R_TRUE;
    default:
        return R_FALSE;
    }
}

} // namespace raven
