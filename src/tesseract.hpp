#pragma once

#include <Raven.h>

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace raven {

enum class TesseractNodeKind { Frame, Text, Rect };
enum class TesseractSizeKind { Fit, Fixed, Fill };

struct TesseractDimension {
    TesseractSizeKind kind = TesseractSizeKind::Fit;
    float value = 0.0f;
};

struct TesseractRect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct TesseractNode {
    TesseractNodeKind kind = TesseractNodeKind::Frame;
    std::string id;
    std::string name;
    std::string content;
    std::string layout = "horizontal";
    std::string justify_content = "start";
    std::string align_items = "start";
    std::string fill;
    std::string stroke;
    std::string font_family;
    std::string font_weight;
    std::string text_align = "left";
    std::string text_growth = "auto";
    TesseractDimension width;
    TesseractDimension height;
    float x = 0.0f;
    float y = 0.0f;
    bool has_x = false;
    bool has_y = false;
    float padding[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float gap = 0.0f;
    float corner_radius = 0.0f;
    float stroke_width = 0.0f;
    float font_size = 14.0f;
    float line_height = 1.3f;
    float opacity = 1.0f;
    bool clip = false;
    TesseractRect bounds;
    float measured_width = 0.0f;
    float measured_height = 0.0f;
    std::size_t line_count = 1;
    std::vector<std::size_t> path;
    std::vector<TesseractNode> children;
};

class TesseractCanvas {
public:
    // Zooms by `factor` around a point in canvas coordinates (trackpad pinch).
    void pinch(float x, float y, float factor) { zoom_at(x, y, factor); }
    void open(const std::filesystem::path& file);
    bool reload_if_changed();

    void paint(Rid widget, RPainter* painter, float width, float height);
    int event(Rid widget, const REvent* event);
    bool advance(double delta, bool reduce_motion);
    bool empty() const;
    void fit();
    void zoom_by(float factor);

    const std::filesystem::path& file() const;
    const std::vector<TesseractNode>& roots() const;
    const TesseractNode* find(const std::string& id) const;
    const std::string& selected_id() const;
    const std::string& error() const;
    std::string selection_context() const;
    std::string agent_guide(const std::optional<std::string>& frozen_selection = std::nullopt) const;
    float zoom_percent() const;
    TesseractRect screen_bounds(const std::string& id) const;
    void select(const std::string& id);
    void clear_selection();
    bool consume_text_edit_request();
    bool update_selected_text(const std::string& text);
    // While a text node is edited inline, the canvas leaves it to the editor drawn above it.
    void set_editing(const std::string& id);
    // The colour a text node is drawn with, so the inline editor can match it.
    struct TesseractColor { float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f; };
    TesseractColor text_color(const TesseractNode& node) const;
    // Undo and redo the user's canvas edits (text, move, resize). Agent rewrites of the document
    // clear the history so an undo never silently reverts the agent's work.
    bool undo();
    bool redo();
    bool can_undo() const { return !undo_stack_.empty(); }

private:
    enum class DragMode { None, Pan, Move, Resize };

    std::filesystem::path file_;
    std::filesystem::file_time_type modified_{};
    bool has_modified_ = false;
    nlohmann::json document_ = nlohmann::json::object();
    std::vector<TesseractNode> roots_;
    std::map<std::string, std::string> variables_;
    std::string selected_id_;
    std::string hovered_id_;
    std::string error_;
    float zoom_ = 0.85f;
    float pan_x_ = 40.0f;
    float pan_y_ = 40.0f;
    float width_ = 0.0f;
    float height_ = 0.0f;
    float fit_width_ = 0.0f;
    float fit_height_ = 0.0f;
    bool fitted_ = false;
    bool navigating_ = false;
    double time_ = 0.0;
    double animation_frame_ = 0.0;
    bool tracking_ = false;
    bool dragging_ = false;
    bool space_down_ = false;
    bool text_edit_requested_ = false;
    DragMode drag_mode_ = DragMode::None;
    float down_x_ = 0.0f;
    float down_y_ = 0.0f;
    float start_x_ = 0.0f;
    float start_y_ = 0.0f;
    float start_width_ = 0.0f;
    float start_height_ = 0.0f;
    std::chrono::steady_clock::time_point next_poll_{};
    std::string saved_document_;
    std::string editing_id_;
    std::vector<std::string> undo_stack_;
    std::vector<std::string> redo_stack_;
    bool restore(std::vector<std::string>& from, std::vector<std::string>& to);

    void load();
    void layout();
    void paint_empty(Rid widget, RPainter* painter);
    void paint_node(RPainter* painter, const TesseractNode& node);
    const TesseractNode* hit_root(float x, float y) const;
    const TesseractNode* hit_deepest(float x, float y) const;
    const TesseractNode* parent(const std::string& id) const;
    const TesseractNode* root(const std::string& id) const;
    TesseractNode* find_mutable(const std::string& id);
    nlohmann::json* raw_node(const TesseractNode& node);
    bool save_selected();
    void zoom_at(float x, float y, float factor);
};

} // namespace raven
