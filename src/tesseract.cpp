#include "tesseract.hpp"
#include "platform.hpp"

#include "workspace.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <limits>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <set>
#include <sstream>
#include <stdexcept>

namespace raven {
namespace {

using json = nlohmann::json;

struct Color {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
};

Color theme_color(Rid widget, RColorRole role, float alpha = 1.0f) {
    Color value;
    Rwidget_get_color(widget, role, &value.r, &value.g, &value.b, &value.a);
    value.a *= alpha;
    return value;
}

std::uint32_t hash_text(const std::string& value) {
    std::uint32_t result = 2166136261u;
    for (const unsigned char c : value) {
        result ^= c;
        result *= 16777619u;
    }
    return result;
}

Color mix_color(const Color& from, const Color& to, float amount, float alpha) {
    return {from.r + (to.r - from.r) * amount, from.g + (to.g - from.g) * amount,
            from.b + (to.b - from.b) * amount, alpha};
}

void circle(RPainter* painter, float x, float y, float radius, const Color& color) {
    Rdraw_circle(painter, x, y, radius, color.r, color.g, color.b, color.a);
}

void line(RPainter* painter, float x0, float y0, float x1, float y1, float thickness, const Color& color) {
    Rdraw_line(painter, x0, y0, x1, y1, thickness, color.r, color.g, color.b, color.a);
}

Color parse_color(const std::string& raw, const std::map<std::string, std::string>& variables, float opacity = 1.0f) {
    Color fallback{0.12f, 0.14f, 0.13f, opacity};
    std::string value = raw;
    if (!value.empty() && value.front() == '$') {
        const auto found = variables.find(value.substr(1));
        if (found == variables.end()) return fallback;
        value = found->second;
    }
    if (value.size() < 4 || value.front() != '#') return fallback;
    const std::string hex = value.substr(1);
    if (!std::all_of(hex.begin(), hex.end(), [](unsigned char c) { return std::isxdigit(c); })) return fallback;
    std::string full;
    if (hex.size() == 3) {
        for (const char c : hex) { full.push_back(c); full.push_back(c); }
    } else if (hex.size() == 6 || hex.size() == 8) {
        full = hex;
    } else {
        return fallback;
    }
    const auto channel = [&](std::size_t offset) {
        return static_cast<float>(std::stoi(full.substr(offset, 2), nullptr, 16)) / 255.0f;
    };
    const float alpha = full.size() == 8 ? channel(6) : 1.0f;
    return {channel(0), channel(2), channel(4), alpha * opacity};
}

float number(const json& object, const char* key, float fallback) {
    const auto found = object.find(key);
    return found != object.end() && found->is_number() ? found->get<float>() : fallback;
}

TesseractDimension dimension(const json& object, const char* key, TesseractDimension fallback) {
    const auto found = object.find(key);
    if (found == object.end()) return fallback;
    if (found->is_number()) return {TesseractSizeKind::Fixed, std::max(0.0f, found->get<float>())};
    if (found->is_string()) {
        const auto value = found->get<std::string>();
        if (value == "fill_container") return {TesseractSizeKind::Fill, 0.0f};
        if (value == "fit_content") return {TesseractSizeKind::Fit, 0.0f};
    }
    throw std::runtime_error(std::string("Invalid ") + key + "; use a number, fit_content, or fill_container");
}

TesseractNode parse_node(const json& value, bool root, int& next_id, std::set<std::string>& ids, int& count) {
    if (++count > 2000) throw std::runtime_error("Tesseract supports at most 2,000 nodes");
    if (!value.is_object()) throw std::runtime_error("Every Tesseract node must be a JSON object");
    auto type = value.value("type", "");
    if (type.empty()) {
        if (value.contains("content")) type = "text";
        else if (root || value.contains("children") || value.contains("layout") || value.contains("padding") || value.contains("gap")) type = "frame";
        else type = "rect";
    }
    TesseractNode node;
    if (type == "frame") node.kind = TesseractNodeKind::Frame;
    else if (type == "text") node.kind = TesseractNodeKind::Text;
    else if (type == "rect") node.kind = TesseractNodeKind::Rect;
    else throw std::runtime_error("Unsupported node type '" + type + "'; use frame, text, or rect");

    node.id = value.value("id", "");
    if (node.id.empty()) node.id = type + "-" + std::to_string(next_id++);
    if (!ids.insert(node.id).second) throw std::runtime_error("Duplicate node id '" + node.id + "'");
    node.name = value.value("name", node.id);
    node.content = value.value("content", "");
    node.layout = value.value("layout", "horizontal");
    if (node.layout != "horizontal" && node.layout != "vertical" && node.layout != "none") {
        throw std::runtime_error("Invalid layout on '" + node.id + "'; use horizontal, vertical, or none");
    }
    node.justify_content = value.value("justifyContent", "start");
    node.align_items = value.value("alignItems", "start");
    node.fill = value.value("fill", "");
    node.stroke = value.value("stroke", "");
    node.font_family = value.value("fontFamily", "sans");
    if (value.contains("fontWeight")) {
        const auto& weight = value["fontWeight"];
        node.font_weight = weight.is_string() ? weight.get<std::string>() : weight.dump();
    }
    node.text_align = value.value("textAlign", "left");
    node.text_growth = value.value("textGrowth", "auto");
    node.width = dimension(value, "width", root && node.kind == TesseractNodeKind::Frame
        ? TesseractDimension{TesseractSizeKind::Fixed, 360.0f}
        : node.kind == TesseractNodeKind::Rect ? TesseractDimension{TesseractSizeKind::Fixed, 120.0f}
        : TesseractDimension{});
    node.height = dimension(value, "height", node.kind == TesseractNodeKind::Rect
        ? TesseractDimension{TesseractSizeKind::Fixed, 80.0f} : TesseractDimension{});
    node.x = number(value, "x", 0.0f);
    node.y = number(value, "y", 0.0f);
    node.has_x = value.contains("x");
    node.has_y = value.contains("y");
    if (value.contains("padding")) {
        const auto& padding = value["padding"];
        if (padding.is_number()) {
            const float all = std::max(0.0f, padding.get<float>());
            std::fill(std::begin(node.padding), std::end(node.padding), all);
        } else if (padding.is_array() && (padding.size() == 2 || padding.size() == 4)) {
            if (padding.size() == 2) {
                node.padding[0] = node.padding[2] = std::max(0.0f, padding[0].get<float>());
                node.padding[1] = node.padding[3] = std::max(0.0f, padding[1].get<float>());
            } else {
                for (std::size_t i = 0; i < 4; ++i) node.padding[i] = std::max(0.0f, padding[i].get<float>());
            }
        } else {
            throw std::runtime_error("Padding on '" + node.id + "' must be a number, [vertical, horizontal], or [top, right, bottom, left]");
        }
    }
    node.gap = std::max(0.0f, number(value, "gap", 0.0f));
    node.corner_radius = std::max(0.0f, number(value, "cornerRadius", 0.0f));
    node.stroke_width = std::max(0.0f, number(value, "strokeWidth", node.stroke.empty() ? 0.0f : 1.0f));
    node.font_size = std::clamp(number(value, "fontSize", 14.0f), 4.0f, 240.0f);
    node.line_height = std::clamp(number(value, "lineHeight", 1.3f), 0.8f, 3.0f);
    node.opacity = std::clamp(number(value, "opacity", 1.0f), 0.0f, 1.0f);
    node.clip = value.value("clip", false);
    if (root && node.kind == TesseractNodeKind::Frame && node.fill.empty()) node.fill = "#FFFFFF";
    if (value.contains("children")) {
        if (!value["children"].is_array()) throw std::runtime_error("Children on '" + node.id + "' must be an array");
        for (const auto& child : value["children"]) node.children.push_back(parse_node(child, false, next_id, ids, count));
    }
    return node;
}

Rid font_for(const TesseractNode& node) {
    Rid font = node.font_family == "mono" ? Rfont_get_monospace() : Rfont_get_default();
    int numeric_weight = 400;
    if (!node.font_weight.empty()) {
        if (node.font_weight == "bold" || node.font_weight == "semibold" || node.font_weight == "demibold") numeric_weight = 700;
        else {
            try { numeric_weight = std::stoi(node.font_weight); } catch (const std::exception&) {}
        }
    }
    return numeric_weight >= 600 ? Rfont_get_variant(font, R_FONT_BOLD) : font;
}

float text_width(Rid font, float size, const std::string& text) {
    float width = 0.0f;
    float height = 0.0f;
    Rfont_measure_text(font, size, text.c_str(), &width, &height);
    return width;
}

std::vector<std::string> wrap_text(const TesseractNode& node, float max_width) {
    std::vector<std::string> lines;
    const Rid font = font_for(node);
    std::size_t paragraph_start = 0;
    while (paragraph_start <= node.content.size()) {
        const auto paragraph_end = node.content.find('\n', paragraph_start);
        const auto paragraph = node.content.substr(paragraph_start,
            paragraph_end == std::string::npos ? std::string::npos : paragraph_end - paragraph_start);
        if (paragraph.empty()) {
            lines.emplace_back();
        } else if (max_width <= 0.0f) {
            lines.push_back(paragraph);
        } else {
            std::string line;
            std::size_t at = 0;
            while (at < paragraph.size()) {
                while (at < paragraph.size() && paragraph[at] == ' ') ++at;
                if (at >= paragraph.size()) break;
                const auto end = paragraph.find(' ', at);
                const auto word = paragraph.substr(at, end == std::string::npos ? std::string::npos : end - at);
                const auto candidate = line.empty() ? word : line + " " + word;
                if (!line.empty() && text_width(font, node.font_size, candidate) > max_width) {
                    lines.push_back(line);
                    line = word;
                } else {
                    line = candidate;
                }
                if (end == std::string::npos) break;
                at = end + 1;
            }
            if (!line.empty()) lines.push_back(line);
        }
        if (paragraph_end == std::string::npos) break;
        paragraph_start = paragraph_end + 1;
    }
    if (lines.empty()) lines.emplace_back();
    return lines;
}

void measure_node(TesseractNode& node, float available_width, float available_height) {
    float width = node.width.kind == TesseractSizeKind::Fixed ? node.width.value
        : node.width.kind == TesseractSizeKind::Fill ? std::max(0.0f, available_width) : 0.0f;
    float height = node.height.kind == TesseractSizeKind::Fixed ? node.height.value
        : node.height.kind == TesseractSizeKind::Fill ? std::max(0.0f, available_height) : 0.0f;

    if (node.kind == TesseractNodeKind::Text) {
        const bool wraps = node.text_growth != "auto" && (node.width.kind != TesseractSizeKind::Fit || available_width > 0.0f);
        const float wrap_width = wraps ? (node.width.kind == TesseractSizeKind::Fit ? available_width : width) : 0.0f;
        const auto lines = wrap_text(node, wrap_width);
        node.line_count = lines.size();
        float natural_width = 0.0f;
        const Rid font = font_for(node);
        for (const auto& line : lines) natural_width = std::max(natural_width, text_width(font, node.font_size, line));
        const float natural_height = static_cast<float>(lines.size()) * node.font_size * node.line_height;
        if (node.width.kind == TesseractSizeKind::Fit) width = natural_width;
        if (node.height.kind == TesseractSizeKind::Fit) height = natural_height;
    } else if (node.kind == TesseractNodeKind::Frame) {
        const float horizontal = node.padding[1] + node.padding[3];
        const float vertical = node.padding[0] + node.padding[2];
        const float inner_width = std::max(0.0f, (width > 0.0f ? width : available_width) - horizontal);
        const float inner_height = std::max(0.0f, (height > 0.0f ? height : available_height) - vertical);
        float children_width = 0.0f;
        float children_height = 0.0f;
        std::size_t flow_children = 0;
        for (auto& child : node.children) {
            measure_node(child, inner_width, inner_height);
            if (node.layout != "none" && (child.has_x || child.has_y)) continue;
            ++flow_children;
            if (node.layout == "vertical") {
                children_width = std::max(children_width, child.measured_width);
                children_height += child.measured_height;
            } else if (node.layout == "horizontal") {
                children_width += child.measured_width;
                children_height = std::max(children_height, child.measured_height);
            } else {
                children_width = std::max(children_width, child.x + child.measured_width);
                children_height = std::max(children_height, child.y + child.measured_height);
            }
        }
        if (flow_children > 1 && node.layout != "none") {
            const float gaps = node.gap * static_cast<float>(flow_children - 1);
            if (node.layout == "vertical") children_height += gaps;
            else children_width += gaps;
        }
        if (node.width.kind == TesseractSizeKind::Fit) width = children_width + horizontal;
        if (node.height.kind == TesseractSizeKind::Fit) height = children_height + vertical;
    }
    node.measured_width = std::max(0.0f, width);
    node.measured_height = std::max(0.0f, height);
}

void place_node(TesseractNode& node, float x, float y, float width, float height) {
    node.bounds = {x, y, std::max(0.0f, width), std::max(0.0f, height)};
    if (node.kind != TesseractNodeKind::Frame) return;
    const float left = x + node.padding[3];
    const float top = y + node.padding[0];
    const float inner_width = std::max(0.0f, width - node.padding[1] - node.padding[3]);
    const float inner_height = std::max(0.0f, height - node.padding[0] - node.padding[2]);
    if (node.layout == "none") {
        for (auto& child : node.children) {
            const float child_width = child.width.kind == TesseractSizeKind::Fill ? inner_width : child.measured_width;
            const float child_height = child.height.kind == TesseractSizeKind::Fill ? inner_height : child.measured_height;
            place_node(child, left + child.x, top + child.y, child_width, child_height);
        }
        return;
    }

    const bool vertical = node.layout == "vertical";
    const float inner_main = vertical ? inner_height : inner_width;
    float fixed_main = 0.0f;
    std::size_t fill_count = 0;
    std::size_t flow_children = 0;
    for (const auto& child : node.children) {
        if (child.has_x || child.has_y) continue;
        ++flow_children;
        const auto& dimension = vertical ? child.height : child.width;
        if (dimension.kind == TesseractSizeKind::Fill) ++fill_count;
        else fixed_main += vertical ? child.measured_height : child.measured_width;
    }
    const float gaps = flow_children > 1 ? node.gap * static_cast<float>(flow_children - 1) : 0.0f;
    const float fill_main = fill_count > 0 ? std::max(0.0f, inner_main - fixed_main - gaps) / static_cast<float>(fill_count) : 0.0f;
    float content_main = gaps;
    for (const auto& child : node.children) {
        if (child.has_x || child.has_y) continue;
        const auto& dimension = vertical ? child.height : child.width;
        content_main += dimension.kind == TesseractSizeKind::Fill ? fill_main : vertical ? child.measured_height : child.measured_width;
    }
    float cursor = vertical ? top : left;
    const float extra = std::max(0.0f, inner_main - content_main);
    float gap = node.gap;
    if (node.justify_content == "center") cursor += extra * 0.5f;
    else if (node.justify_content == "end") cursor += extra;
    else if (node.justify_content == "space_between" && flow_children > 1 && fill_count == 0) {
        gap += extra / static_cast<float>(flow_children - 1);
    }

    for (auto& child : node.children) {
        if (child.has_x || child.has_y) {
            const float child_width = child.width.kind == TesseractSizeKind::Fill ? inner_width : child.measured_width;
            const float child_height = child.height.kind == TesseractSizeKind::Fill ? inner_height : child.measured_height;
            place_node(child, left + child.x, top + child.y, child_width, child_height);
            continue;
        }
        const float child_main = (vertical ? child.height : child.width).kind == TesseractSizeKind::Fill
            ? fill_main : vertical ? child.measured_height : child.measured_width;
        const float inner_cross = vertical ? inner_width : inner_height;
        const float child_cross = (vertical ? child.width : child.height).kind == TesseractSizeKind::Fill
            ? inner_cross : vertical ? child.measured_width : child.measured_height;
        float cross = vertical ? left : top;
        if (node.align_items == "center") cross += (inner_cross - child_cross) * 0.5f;
        else if (node.align_items == "end") cross += inner_cross - child_cross;
        if (vertical) place_node(child, cross, cursor, child_cross, child_main);
        else place_node(child, cursor, cross, child_main, child_cross);
        cursor += child_main + gap;
    }
}

bool intersects(const TesseractRect& a, const TesseractRect& b) {
    return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
}

const TesseractNode* find_in(const std::vector<TesseractNode>& nodes, const std::string& id) {
    for (const auto& node : nodes) {
        if (node.id == id) return &node;
        if (const auto* found = find_in(node.children, id)) return found;
    }
    return nullptr;
}

TesseractNode* find_in(std::vector<TesseractNode>& nodes, const std::string& id) {
    for (auto& node : nodes) {
        if (node.id == id) return &node;
        if (auto* found = find_in(node.children, id)) return found;
    }
    return nullptr;
}

const TesseractNode* parent_in(const std::vector<TesseractNode>& nodes, const std::string& id) {
    for (const auto& node : nodes) {
        for (const auto& child : node.children) if (child.id == id) return &node;
        if (const auto* found = parent_in(node.children, id)) return found;
    }
    return nullptr;
}

void assign_path(TesseractNode& node, const std::vector<std::size_t>& path) {
    node.path = path;
    for (std::size_t i = 0; i < node.children.size(); ++i) {
        auto child_path = path;
        child_path.push_back(i);
        assign_path(node.children[i], child_path);
    }
}

const TesseractNode* deepest_at(const TesseractNode& node, float x, float y) {
    for (auto it = node.children.rbegin(); it != node.children.rend(); ++it) {
        const auto& bounds = it->bounds;
        if (x >= bounds.x && x <= bounds.x + bounds.width && y >= bounds.y && y <= bounds.y + bounds.height) {
            return deepest_at(*it, x, y);
        }
    }
    return &node;
}

std::string default_document() {
    return R"({
  "version": "stellar-tesseract/1",
  "variables": {
    "accent": "#65C18C",
    "ink": "#131714",
    "paper": "#FFFFFF"
  },
  "children": []
}
)";
}

void centered_text(RPainter* painter, float x, float y, const std::string& text, float size, const Color& color) {
    const float width = text_width(Rfont_get_default(), size, text);
    Rdraw_text(painter, Rfont_get_default(), size, x - width * 0.5f, y, text.c_str(), color.r, color.g, color.b, color.a);
}

} // namespace

void TesseractCanvas::open(const std::filesystem::path& file) {
    file_ = file;
    roots_.clear();
    variables_.clear();
    selected_id_.clear();
    hovered_id_.clear();
    error_.clear();
    has_modified_ = false;
    fitted_ = false;
    navigating_ = false;
    fit_width_ = fit_height_ = 0.0f;
    zoom_ = 0.85f;
    pan_x_ = pan_y_ = 40.0f;
    next_poll_ = {};
    load();
}

void TesseractCanvas::load() {
    std::error_code error;
    if (!std::filesystem::exists(file_, error)) {
        write_file(file_, default_document());
    }
    const auto modified = std::filesystem::last_write_time(file_, error);
    if (error) throw std::runtime_error("Cannot inspect Tesseract document: " + error.message());
    try {
        const auto document = json::parse(read_file(file_));
        if (!document.is_object() || !document.contains("children") || !document["children"].is_array()) {
            throw std::runtime_error("Tesseract document needs a children array");
        }
        std::vector<TesseractNode> roots;
        std::map<std::string, std::string> variables;
        if (document.contains("variables") && document["variables"].is_object()) {
            for (const auto& [key, value] : document["variables"].items()) {
                if (value.is_string()) variables[key] = value.get<std::string>();
                else if (value.is_number()) variables[key] = value.dump();
            }
        }
        int next_id = 1;
        int count = 0;
        std::set<std::string> ids;
        for (const auto& value : document["children"]) {
            auto node = parse_node(value, true, next_id, ids, count);
            if (node.kind != TesseractNodeKind::Frame) throw std::runtime_error("Root nodes must be frames");
            roots.push_back(std::move(node));
        }
        const bool changed_emptiness = roots_.empty() != roots.empty();
        for (std::size_t i = 0; i < roots.size(); ++i) assign_path(roots[i], {i});
        roots_ = std::move(roots);
        variables_ = std::move(variables);
        document_ = document;
        saved_document_ = document_.dump(2) + "\n";
        if (changed_emptiness) fitted_ = false;
        error_.clear();
        modified_ = modified;
        has_modified_ = true;
        if (!selected_id_.empty() && !find(selected_id_)) selected_id_.clear();
        layout();
    } catch (const std::exception& parse_error) {
        error_ = parse_error.what();
        modified_ = modified;
        has_modified_ = true;
    }
}

bool TesseractCanvas::reload_if_changed() {
    if (tracking_) return false;
    const auto now = std::chrono::steady_clock::now();
    if (now < next_poll_) return false;
    next_poll_ = now + std::chrono::milliseconds(350);
    std::error_code error;
    if (!std::filesystem::exists(file_, error)) {
        if (!has_modified_ && roots_.empty()) return false;
        roots_.clear();
        selected_id_.clear();
        has_modified_ = false;
        error_.clear();
        return true;
    }
    const auto modified = std::filesystem::last_write_time(file_, error);
    if (error || (has_modified_ && modified == modified_)) return false;
    load();
    // Someone else (usually the agent) rewrote the document.
    undo_stack_.clear();
    redo_stack_.clear();
    return true;
}

bool TesseractCanvas::restore(std::vector<std::string>& from, std::vector<std::string>& to) {
    if (from.empty()) return false;
    try {
        const auto previous = saved_document_;
        write_file(file_, from.back());
        from.pop_back();
        to.push_back(previous);
        load();
        return error_.empty();
    } catch (const std::exception& restore_error) {
        error_ = restore_error.what();
        return false;
    }
}

bool TesseractCanvas::undo() { return restore(undo_stack_, redo_stack_); }

bool TesseractCanvas::redo() { return restore(redo_stack_, undo_stack_); }

void TesseractCanvas::layout() {
    std::vector<TesseractRect> placed;
    for (auto& root : roots_) {
        measure_node(root, 360.0f, 240.0f);
        if (root.has_x || root.has_y) {
            TesseractRect bounds{root.x, root.y, root.measured_width, root.measured_height};
            root.bounds = bounds;
            placed.push_back(bounds);
        }
    }
    float cursor_x = 0.0f;
    float cursor_y = 0.0f;
    float row_height = 0.0f;
    int column = 0;
    for (auto& root : roots_) {
        if (root.has_x || root.has_y) continue;
        TesseractRect candidate{cursor_x, cursor_y, root.measured_width, root.measured_height};
        int attempts = 0;
        bool moving = true;
        while (moving && attempts++ < 10000) {
            moving = false;
            for (const auto& other : placed) {
                if (!intersects(candidate, other)) continue;
                candidate.x = other.x + other.width + 40.0f;
                moving = true;
            }
            if (candidate.x > 1600.0f) {
                candidate.x = 0.0f;
                candidate.y += row_height + 40.0f;
                row_height = 0.0f;
                moving = true;
            }
        }
        root.bounds = candidate;
        placed.push_back(candidate);
        cursor_x = candidate.x + candidate.width + 40.0f;
        row_height = std::max(row_height, candidate.height);
        if (++column >= 3) {
            column = 0;
            cursor_x = 0.0f;
            cursor_y = candidate.y + row_height + 40.0f;
            row_height = 0.0f;
        }
    }
    for (auto& root : roots_) place_node(root, root.bounds.x, root.bounds.y, root.measured_width, root.measured_height);
}

const std::filesystem::path& TesseractCanvas::file() const { return file_; }
const std::vector<TesseractNode>& TesseractCanvas::roots() const { return roots_; }
const std::string& TesseractCanvas::selected_id() const { return selected_id_; }
const std::string& TesseractCanvas::error() const { return error_; }
float TesseractCanvas::zoom_percent() const { return zoom_ * 100.0f; }

const TesseractNode* TesseractCanvas::find(const std::string& id) const { return find_in(roots_, id); }
TesseractNode* TesseractCanvas::find_mutable(const std::string& id) { return find_in(roots_, id); }

const TesseractNode* TesseractCanvas::parent(const std::string& id) const { return parent_in(roots_, id); }

const TesseractNode* TesseractCanvas::root(const std::string& id) const {
    for (const auto& candidate : roots_) if (find_in(candidate.children, id) || candidate.id == id) return &candidate;
    return nullptr;
}

nlohmann::json* TesseractCanvas::raw_node(const TesseractNode& node) {
    auto* current = &document_;
    for (const auto index : node.path) {
        if (!current->is_object() || !current->contains("children") || !(*current)["children"].is_array() ||
            index >= (*current)["children"].size()) return nullptr;
        current = &(*current)["children"][index];
    }
    return current;
}

bool TesseractCanvas::save_selected() {
    const auto* selected = find(selected_id_);
    if (!selected || !raw_node(*selected)) return false;
    return commit_document();
}

// Writes the in-memory document as one undoable step.
bool TesseractCanvas::commit_document() {
    try {
        const auto updated = document_.dump(2) + "\n";
        if (updated != saved_document_) {
            undo_stack_.push_back(saved_document_);
            if (undo_stack_.size() > 100) undo_stack_.erase(undo_stack_.begin());
            redo_stack_.clear();
        }
        write_file(file_, updated);
        load();
        return error_.empty();
    } catch (const std::exception& save_error) {
        error_ = save_error.what();
        return false;
    }
}

void TesseractCanvas::select(const std::string& id) {
    selected_id_ = find(id) ? id : "";
}

void TesseractCanvas::clear_selection() {
    selected_id_.clear();
    text_edit_requested_ = false;
}

TesseractRect TesseractCanvas::screen_bounds(const std::string& id) const {
    const auto* node = find(id);
    if (!node) return {};
    return {pan_x_ + node->bounds.x * zoom_, pan_y_ + node->bounds.y * zoom_,
            node->bounds.width * zoom_, node->bounds.height * zoom_};
}

bool TesseractCanvas::consume_text_edit_request() {
    const bool requested = text_edit_requested_;
    text_edit_requested_ = false;
    return requested;
}

void TesseractCanvas::set_editing(const std::string& id) { editing_id_ = id; }

bool TesseractCanvas::delete_selected() {
    const auto* selected = find(selected_id_);
    if (!selected || selected->path.empty()) return false;
    auto* parent = &document_;
    for (std::size_t i = 0; i + 1 < selected->path.size(); ++i) {
        if (!parent->contains("children") || !(*parent)["children"].is_array()) return false;
        parent = &(*parent)["children"][selected->path[i]];
    }
    if (!parent->contains("children") || !(*parent)["children"].is_array()) return false;
    auto& children = (*parent)["children"];
    const auto index = selected->path.back();
    if (index >= children.size()) return false;
    children.erase(children.begin() + static_cast<std::ptrdiff_t>(index));
    selected_id_.clear();
    return commit_document();
}

TesseractCanvas::TesseractColor TesseractCanvas::text_color(const TesseractNode& node) const {
    const auto color = parse_color(node.fill.empty() ? "$ink" : node.fill, variables_, node.opacity);
    return {color.r, color.g, color.b, color.a};
}

bool TesseractCanvas::update_selected_text(const std::string& text) {
    auto* selected = find_mutable(selected_id_);
    if (!selected || selected->kind != TesseractNodeKind::Text) return false;
    auto* raw = raw_node(*selected);
    if (!raw) return false;
    selected->content = text;
    (*raw)["content"] = text;
    return save_selected();
}

std::string TesseractCanvas::selection_context() const {
    const auto* selected = find(selected_id_);
    if (!selected) return {};
    const auto* selected_root = root(selected_id_);
    if (selected == selected_root) {
        return "The user selected root frame '" + selected->id + "' named '" + selected->name +
            "'. Read its current JSON first and limit the requested changes to that frame unless the user explicitly asks for broader changes.";
    }
    const auto kind = selected->kind == TesseractNodeKind::Text ? "text" : selected->kind == TesseractNodeKind::Rect ? "rect" : "frame";
    return "The user selected " + std::string(kind) + " node '" + selected->id + "' named '" + selected->name +
        "' inside root frame '" + (selected_root ? selected_root->id : "") +
        "'. Read its current JSON first and limit the requested changes to that node unless the user explicitly asks for broader changes.";
}

std::string TesseractCanvas::agent_guide(const std::optional<std::string>& frozen_selection) const {
    auto guide = std::string(R"(TESSERACT CANVAS CONTRACT:
Create or refine the native UI document at `.stellar/tesseract.json`; do not replace it with HTML, Markdown, or source-code instructions. Use Read before editing and Write/Edit to keep a complete valid JSON document. Raven reloads it automatically about every 350 ms, so make small coherent changes that the user can see appear live. Preserve unrelated existing frames and never discard a design merely to start over.
Prioritize visible progress: your first tool call may Read `.stellar/tesseract.json`; your next tool call must Write a complete first version. Do not explore the workspace, wiki, or source files before that first render.

Schema: {"version":"stellar-tesseract/1","variables":{"accent":"#65C18C","ink":"#131714","paper":"#FFFFFF"},"children":[ROOT_FRAMES]}.
Every node needs an explicit "type", including nested children. Complete example:
{"type":"frame","id":"login","name":"Login","width":420,"height":480,"layout":"vertical","padding":32,"gap":16,"fill":"$paper","children":[{"type":"text","id":"title","content":"Sign in","fontSize":28,"fill":"$ink"},{"type":"frame","id":"email","width":"fill_container","height":48,"padding":12,"stroke":"#D8DDD9","strokeWidth":1,"children":[{"type":"text","id":"email-placeholder","content":"you@company.com","fill":"#68736C"}]},{"type":"rect","id":"accent-rule","width":"fill_container","height":4,"fill":"$accent"}]}
Only frame, text, and rect are supported. Every root child must be a frame with a unique stable id and a useful name. Supported common fields: id, name, x, y, width, height, fill, stroke, strokeWidth, cornerRadius, opacity. Width/height accept a number, "fit_content", or "fill_container". Root frames default to 360px wide and are auto-placed without overlap when x/y are omitted.
Frames: layout "vertical"|"horizontal"|"none", justifyContent "start"|"center"|"end"|"space_between", alignItems "start"|"center"|"end", gap, padding (number, [vertical,horizontal], or [top,right,bottom,left]), clip, children. Prefer vertical/horizontal layouts and fit_content/fill_container for new structures. A nested node with x/y is absolutely positioned relative to its parent's padded content and leaves other siblings in the automatic flow; preserve those coordinates as user-authored direct edits.
Text: content, fontSize, fontWeight, fontFamily "sans"|"mono", fill, lineHeight, textAlign "left"|"center"|"right", textGrowth "auto"|"fixed-width". Set a fixed/fill width when text should wrap. Text has no padding; use a parent frame for that.
Rect: a colored geometric surface; width and height default to 120×80.
Colors: #RGB, #RRGGBB, #RRGGBBAA, or "$variable" from the document's flat variables map. Set text fill deliberately for contrast. No images, icons, gradients, browsers, refs, notes, slides, or arbitrary CSS.

Build the outer frame first, then refine sections with subsequent edits. Use real product copy and coherent spacing; avoid decorative cards inside cards. Do not implement the design in repository code unless the user separately asks for implementation. Finish only after the JSON parses and the requested visual structure is complete.)");
    const auto selection = frozen_selection ? *frozen_selection : selection_context();
    if (!selection.empty()) guide += "\n\nSELECTION:\n" + selection;
    return guide;
}

void TesseractCanvas::fit() {
    navigating_ = false;
    fitted_ = width_ > 0.0f && height_ > 0.0f;
    fit_width_ = width_;
    fit_height_ = height_;
    if (roots_.empty() || !fitted_) {
        zoom_ = 0.85f;
        pan_x_ = pan_y_ = 40.0f;
        return;
    }
    float min_x = 100000.0f;
    float min_y = 100000.0f;
    float max_x = -100000.0f;
    float max_y = -100000.0f;
    for (const auto& root : roots_) {
        min_x = std::min(min_x, root.bounds.x);
        min_y = std::min(min_y, root.bounds.y - 28.0f);
        max_x = std::max(max_x, root.bounds.x + root.bounds.width);
        max_y = std::max(max_y, root.bounds.y + root.bounds.height);
    }
    // Frame the design in the free area: below the title and hints (top 76px) and above the
    // floating prompt box (bottom 156px), with a margin on the sides.
    const float top = 76.0f;
    const float bottom = 156.0f;
    const float available_width = std::max(100.0f, width_ - 100.0f);
    const float available_height = std::max(100.0f, height_ - top - bottom);
    zoom_ = std::clamp(std::min(available_width / std::max(1.0f, max_x - min_x),
                                available_height / std::max(1.0f, max_y - min_y)), 0.15f, 1.6f);
    pan_x_ = width_ * 0.5f - (min_x + max_x) * 0.5f * zoom_;
    pan_y_ = top + std::max(100.0f, height_ - top - bottom) * 0.5f - (min_y + max_y) * 0.5f * zoom_;
}

void TesseractCanvas::zoom_at(float x, float y, float factor) {
    navigating_ = true;
    const float next = std::clamp(zoom_ * factor, 0.15f, 4.0f);
    const float world_x = (x - pan_x_) / zoom_;
    const float world_y = (y - pan_y_) / zoom_;
    pan_x_ = x - world_x * next;
    pan_y_ = y - world_y * next;
    zoom_ = next;
}

void TesseractCanvas::zoom_by(float factor) { zoom_at(width_ * 0.5f, height_ * 0.5f, factor); }

const TesseractNode* TesseractCanvas::hit_root(float x, float y) const {
    const float world_x = (x - pan_x_) / zoom_;
    const float world_y = (y - pan_y_) / zoom_;
    for (auto it = roots_.rbegin(); it != roots_.rend(); ++it) {
        const auto& bounds = it->bounds;
        if (world_x >= bounds.x && world_x <= bounds.x + bounds.width &&
            world_y >= bounds.y - 24.0f && world_y <= bounds.y + bounds.height) return &*it;
    }
    return nullptr;
}

const TesseractNode* TesseractCanvas::hit_deepest(float x, float y) const {
    const auto* root_node = hit_root(x, y);
    if (!root_node) return nullptr;
    const float world_x = (x - pan_x_) / zoom_;
    const float world_y = (y - pan_y_) / zoom_;
    return deepest_at(*root_node, world_x, world_y);
}

std::vector<const TesseractNode*> TesseractCanvas::chain_at(float x, float y) const {
    std::vector<const TesseractNode*> chain;
    const auto* node = hit_root(x, y);
    const float world_x = (x - pan_x_) / zoom_;
    const float world_y = (y - pan_y_) / zoom_;
    while (node) {
        chain.push_back(node);
        const TesseractNode* next = nullptr;
        for (auto it = node->children.rbegin(); it != node->children.rend(); ++it) {
            const auto& b = it->bounds;
            if (world_x >= b.x && world_x <= b.x + b.width && world_y >= b.y && world_y <= b.y + b.height) {
                next = &*it;
                break;
            }
        }
        node = next;
    }
    return chain;
}

const TesseractNode* TesseractCanvas::scoped_pick(const std::vector<const TesseractNode*>& chain, bool deeper) const {
    if (chain.empty()) return nullptr;
    int selected = -1;
    for (std::size_t i = 0; i < chain.size(); ++i) if (chain[i]->id == selected_id_) selected = static_cast<int>(i);
    if (selected >= 0) {
        // Inside the selection: keep it (so it can be dragged), or step one level in.
        if (deeper && selected + 1 < static_cast<int>(chain.size())) return chain[static_cast<std::size_t>(selected) + 1];
        return chain[static_cast<std::size_t>(selected)];
    }
    // A sibling of the selection, or of one of its ancestors, at the same level.
    if (const auto* parent_node = parent(selected_id_)) {
        for (std::size_t i = 0; i + 1 < chain.size(); ++i) {
            if (chain[i]->id == parent_node->id) return chain[i + 1];
        }
    }
    return chain.size() > 1 ? chain[1] : chain[0];
}

void TesseractCanvas::paint_node(RPainter* painter, const TesseractNode& node) {
    const float x = pan_x_ + node.bounds.x * zoom_;
    const float y = pan_y_ + node.bounds.y * zoom_;
    const float width = node.bounds.width * zoom_;
    const float height = node.bounds.height * zoom_;
    if (node.kind == TesseractNodeKind::Frame || node.kind == TesseractNodeKind::Rect) {
        if (!node.fill.empty()) {
            const auto fill = parse_color(node.fill, variables_, node.opacity);
            Rdraw_rect(painter, x, y, width, height, node.corner_radius * zoom_, fill.r, fill.g, fill.b, fill.a);
        }
        if (!node.stroke.empty() && node.stroke_width > 0.0f) {
            const auto stroke = parse_color(node.stroke, variables_, node.opacity);
            Rdraw_rect_outline(painter, x, y, width, height, node.corner_radius * zoom_, std::max(0.5f, node.stroke_width * zoom_),
                               stroke.r, stroke.g, stroke.b, stroke.a);
        }
    } else {
        // The text being edited inline is drawn by the editor on top; painting it here too doubled it.
        if (node.id == editing_id_) return;
        const auto fill = parse_color(node.fill.empty() ? "$ink" : node.fill, variables_, node.opacity);
        const Rid font = font_for(node);
        const auto lines = wrap_text(node, node.text_growth == "auto" ? 0.0f : node.bounds.width);
        const float line_step = node.font_size * node.line_height * zoom_;
        for (std::size_t i = 0; i < lines.size(); ++i) {
            float line_x = x;
            const float measured = text_width(font, node.font_size * zoom_, lines[i]);
            if (node.text_align == "center") line_x += (width - measured) * 0.5f;
            else if (node.text_align == "right") line_x += width - measured;
            Rdraw_text(painter, font, node.font_size * zoom_, line_x, y + static_cast<float>(i) * line_step,
                       lines[i].c_str(), fill.r, fill.g, fill.b, fill.a);
        }
        return;
    }
    if (node.clip) Rdraw_push_clip_rect(painter, x, y, width, height);
    for (const auto& child : node.children) paint_node(painter, child);
    if (node.clip) Rdraw_pop_clip_rect(painter);
}

bool TesseractCanvas::empty() const { return roots_.empty(); }

bool TesseractCanvas::advance(double delta, bool reduce_motion) {
    if (reduce_motion || !empty()) return false;
    time_ += std::max(delta, 0.0);
    if (time_ - animation_frame_ < 1.0 / 30.0) return false;
    animation_frame_ = time_;
    return true;
}

void TesseractCanvas::paint_empty(Rid widget, RPainter* painter) {
    const auto content = Rwindow_get_content(Rwidget_get_window(widget));
    const auto text = theme_color(widget, R_COLOR_TEXT);
    const auto muted = theme_color(widget, R_COLOR_TEXT_PLACEHOLDER);
    const auto accent = theme_color(widget, R_COLOR_ACCENT);
    const auto link = theme_color(widget, R_COLOR_LINK);
    const auto success = theme_color(widget, R_COLOR_SUCCESS);
    const float scale = std::clamp(std::min(width_ / 760.0f, height_ / 640.0f), 0.55f, 1.35f);
    const float center_x = width_ * 0.5f;
    const float center_y = height_ * 0.34f;
    const float clock = static_cast<float>(time_);

    for (int i = 0; i < 150; ++i) {
        const auto hash = hash_text("tesseract:star:" + std::to_string(i));
        const float x = static_cast<float>(hash % 10000) / 10000.0f * width_;
        const float y = static_cast<float>((hash / 10000) % 10000) / 10000.0f * height_;
        const float phase = static_cast<float>(hash % 628) / 100.0f;
        const float alpha = 0.10f + 0.20f * (0.5f + 0.5f * std::sin(clock * 0.7f + phase));
        circle(painter, x, y, 0.35f + static_cast<float>(hash % 4) * 0.18f, mix_color(muted, text, 0.45f, alpha));
    }

    struct ProjectedVertex {
        float x;
        float y;
        float depth;
        bool outer;
    };
    ProjectedVertex vertices[16];
    const float xw = 0.48f + clock * 0.30f;
    const float yw = 0.36f + clock * 0.21f;
    const float zw = 0.22f + clock * 0.16f;
    const float xy = 0.30f + clock * 0.10f;
    for (int i = 0; i < 16; ++i) {
        float x = (i & 1) ? 1.0f : -1.0f;
        float y = (i & 2) ? 1.0f : -1.0f;
        float z = (i & 4) ? 1.0f : -1.0f;
        float w = (i & 8) ? 1.0f : -1.0f;
        const bool outer = w > 0.0f;
        {
            const float nx = x * std::cos(xw) - w * std::sin(xw);
            w = x * std::sin(xw) + w * std::cos(xw);
            x = nx;
        }
        {
            const float ny = y * std::cos(yw) - w * std::sin(yw);
            w = y * std::sin(yw) + w * std::cos(yw);
            y = ny;
        }
        {
            const float nz = z * std::cos(zw) - w * std::sin(zw);
            w = z * std::sin(zw) + w * std::cos(zw);
            z = nz;
        }
        {
            const float nx = x * std::cos(xy) - y * std::sin(xy);
            y = x * std::sin(xy) + y * std::cos(xy);
            x = nx;
        }
        const float perspective4 = 2.8f / (3.4f - w);
        const float perspective3 = 3.2f / (4.0f - z * perspective4);
        const float depth = perspective3 * perspective4;
        vertices[i] = {center_x + x * perspective4 * perspective3 * 92.0f * scale,
                       center_y + y * perspective4 * perspective3 * 92.0f * scale, depth, outer};
    }
    for (int i = 0; i < 16; ++i) {
        for (int bit = 1; bit <= 8; bit <<= 1) {
            const int other = i ^ bit;
            if (other < i) continue;
            const auto& a = vertices[i];
            const auto& b = vertices[other];
            const float depth = std::clamp((a.depth + b.depth) * 0.5f, 0.45f, 1.65f);
            const auto base = a.outer && b.outer ? accent : !(a.outer || b.outer) ? link : success;
            const auto color = mix_color(base, text, std::clamp(depth * 0.28f, 0.0f, 0.45f),
                                         std::clamp(0.18f + depth * 0.42f, 0.0f, 0.88f));
            line(painter, a.x, a.y, b.x, b.y, (0.65f + depth * 0.55f) * scale, color);
        }
    }
    for (const auto& vertex : vertices) {
        const auto base = vertex.outer ? accent : link;
        circle(painter, vertex.x, vertex.y, (0.85f + vertex.depth * 1.15f) * scale,
               mix_color(base, text, 0.45f, std::clamp(vertex.depth * 0.62f, 0.35f, 1.0f)));
    }

    centered_text(painter, center_x, center_y + 155.0f * scale, "Tesseract", 30.0f, text);
    centered_text(painter, center_x, center_y + 197.0f * scale, "Describe an interface. Watch it take shape.", 14.0f, muted);
    centered_text(painter, center_x, center_y + 223.0f * scale, "Edit directly, or select a frame and ask the agent to refine it.", 12.0f, muted);
}

void TesseractCanvas::paint(Rid widget, RPainter* painter, float width, float height) {
    width_ = width;
    height_ = height;
    // The first size frames the design; later size changes (a divider, a folded panel, the window)
    // keep the zoom and the view's center instead of reframing, which made the design jump.
    if (!fitted_) {
        fit();
    } else if (std::abs(width_ - fit_width_) > 0.5f || std::abs(height_ - fit_height_) > 0.5f) {
        pan_x_ += (width_ - fit_width_) * 0.5f;
        pan_y_ += (height_ - fit_height_) * 0.5f;
        fit_width_ = width_;
        fit_height_ = height_;
    }
    const auto content = Rwindow_get_content(Rwidget_get_window(widget));
    const auto window = theme_color(content, R_COLOR_WINDOW);
    const auto text = theme_color(widget, R_COLOR_TEXT);
    const auto muted = theme_color(widget, R_COLOR_TEXT_PLACEHOLDER);
    const auto accent = theme_color(widget, R_COLOR_ACCENT);
    const auto danger = theme_color(widget, R_COLOR_DANGER);
    Rdraw_rect(painter, 0.0f, 0.0f, width, height, 0.0f, window.r, window.g, window.b, 1.0f);

    if (!roots_.empty()) {
        float step = 24.0f * zoom_;
        while (step < 16.0f || (width / step) * (height / step) > 4000.0f) step *= 2.0f;
        const float offset_x = std::fmod(pan_x_, step);
        const float offset_y = std::fmod(pan_y_, step);
        for (float y = offset_y; y < height; y += step) {
            for (float x = offset_x; x < width; x += step) {
                Rdraw_circle(painter, x, y, 0.65f, muted.r, muted.g, muted.b, 0.20f);
            }
        }
    }

    const auto* selected_root = root(selected_id_);
    const auto* hovered_root = root(hovered_id_);
    for (const auto& root_node : roots_) {
        const bool active = selected_root == &root_node || hovered_root == &root_node;
        const float caption_size = std::max(8.0f, 11.0f * zoom_);
        Rdraw_text(painter, Rfont_get_default(), caption_size,
                   pan_x_ + root_node.bounds.x * zoom_, pan_y_ + (root_node.bounds.y - 22.0f) * zoom_,
                   root_node.name.c_str(), active ? accent.r : muted.r, active ? accent.g : muted.g,
                   active ? accent.b : muted.b, 1.0f);
        paint_node(painter, root_node);
    }
    // Selection chrome follows the element exactly: a hairline on its bounds, square handles on the
    // corners (on the sides for text, which only resizes sideways), and its size underneath.
    const auto accent_text = theme_color(content, R_COLOR_ACCENT_TEXT);
    const auto draw_outline = [&](const TesseractNode* node, bool selected) {
        if (!node) return;
        const float x = pan_x_ + node->bounds.x * zoom_;
        const float y = pan_y_ + node->bounds.y * zoom_;
        const float w = node->bounds.width * zoom_;
        const float h = node->bounds.height * zoom_;
        Rdraw_rect_outline(painter, x, y, w, h, 0.0f, selected ? 1.5f : 1.0f, accent.r, accent.g, accent.b, selected ? 1.0f : 0.55f);
        if (!selected) return;
        const auto handle = [&](float cx, float cy) {
            Rdraw_rect(painter, cx - 4.0f, cy - 4.0f, 8.0f, 8.0f, 1.5f, 1.0f, 1.0f, 1.0f, 1.0f);
            Rdraw_rect_outline(painter, cx - 4.0f, cy - 4.0f, 8.0f, 8.0f, 1.5f, 1.0f, accent.r, accent.g, accent.b, 1.0f);
        };
        if (node->kind == TesseractNodeKind::Text) {
            handle(x, y + h * 0.5f);
            handle(x + w, y + h * 0.5f);
        } else {
            handle(x, y);
            handle(x + w, y);
            handle(x, y + h);
            handle(x + w, y + h);
        }
        const auto size = std::to_string(static_cast<int>(std::lround(node->bounds.width))) + " × " +
            std::to_string(static_cast<int>(std::lround(node->bounds.height)));
        const float label_width = text_width(Rfont_get_default(), 11.0f, size) + 12.0f;
        const float label_x = x + w * 0.5f - label_width * 0.5f;
        const float label_y = y + h + 8.0f;
        Rdraw_rect(painter, label_x, label_y, label_width, 18.0f, 4.0f, accent.r, accent.g, accent.b, 1.0f);
        Rdraw_text(painter, Rfont_get_default(), 11.0f, label_x + 6.0f, label_y + 2.0f, size.c_str(),
                   accent_text.r, accent_text.g, accent_text.b, 1.0f);
    };
    if (!hovered_id_.empty() && hovered_id_ != selected_id_) draw_outline(find(hovered_id_), false);
    draw_outline(find(selected_id_), true);
    // Reordering inside an automatic layout: the dragged ghost and the insertion line.
    if (reorder_index_ >= 0 && dragging_) {
        Rdraw_rect(painter, drag_ghost_.x, drag_ghost_.y, drag_ghost_.width, drag_ghost_.height, 2.0f,
                   accent.r, accent.g, accent.b, 0.12f);
        Rdraw_rect_outline(painter, drag_ghost_.x, drag_ghost_.y, drag_ghost_.width, drag_ghost_.height, 2.0f, 1.0f,
                           accent.r, accent.g, accent.b, 0.7f);
        Rdraw_rect(painter, reorder_line_.x, reorder_line_.y, reorder_line_.width, reorder_line_.height, 1.0f,
                   accent.r, accent.g, accent.b, 1.0f);
    }
    // Alignment guides while a root frame snaps to another.
    for (const float gx : guide_x_) {
        const float sx = pan_x_ + gx * zoom_;
        Rdraw_rect(painter, sx - 0.5f, 0.0f, 1.0f, height, 0.0f, 1.0f, 0.27f, 0.52f, 0.95f);
    }
    for (const float gy : guide_y_) {
        const float sy = pan_y_ + gy * zoom_;
        Rdraw_rect(painter, 0.0f, sy - 0.5f, width, 1.0f, 0.0f, 1.0f, 0.27f, 0.52f, 0.95f);
    }

    if (roots_.empty()) {
        paint_empty(widget, painter);
    } else {
        Rdraw_text(painter, Rfont_get_default(), 14.0f, 16.0f, 18.0f, "Tesseract", text.r, text.g, text.b, 1.0f);
        const auto summary = std::to_string(roots_.size()) + (roots_.size() == 1 ? " frame" : " frames");
        Rdraw_text(painter, Rfont_get_default(), 11.0f, 16.0f, 40.0f, summary.c_str(), muted.r, muted.g, muted.b, 1.0f);
        Rdraw_text(painter, Rfont_get_default(), 10.0f, 16.0f, 58.0f, 
#ifdef __APPLE__
            "Double-click goes inside · ⌘-click selects the deepest · Drag edges to resize · ⌘D duplicates · ⌘Z undo"
#else
            "Double-click goes inside · Ctrl+click selects the deepest · Drag edges to resize · Ctrl+D duplicates · Ctrl+Z undo"
#endif
, muted.r, muted.g, muted.b, 1.0f);
    }
    if (!error_.empty()) {
        const float error_y = height - 146.0f;
        Rdraw_rect(painter, 12.0f, error_y - 8.0f, width - 24.0f, 42.0f, 6.0f, window.r, window.g, window.b, 0.96f);
        Rdraw_text(painter, Rfont_get_default(), 11.0f, 16.0f, error_y, "Document not updated · fix the JSON to recover.", danger.r, danger.g, danger.b, 1.0f);
        Rdraw_text(painter, Rfont_get_default(), 10.0f, 16.0f, error_y + 18.0f, error_.c_str(), muted.r, muted.g, muted.b, 1.0f);
    }
}

namespace {

RCursor cursor_for_edges(int edges) {
    const bool left = edges & 1, right = edges & 2, top = edges & 4, bottom = edges & 8;
    if ((left && top) || (right && bottom)) return R_CURSOR_RESIZE_NWSE;
    if ((right && top) || (left && bottom)) return R_CURSOR_RESIZE_NESW;
    if (left || right) return R_CURSOR_RESIZE_H;
    if (top || bottom) return R_CURSOR_RESIZE_V;
    return R_CURSOR_ARROW;
}

void collect_ids(const nlohmann::json& node, std::set<std::string>& ids) {
    if (!node.is_object()) return;
    if (node.contains("id") && node["id"].is_string()) ids.insert(node["id"].get<std::string>());
    if (node.contains("children") && node["children"].is_array())
        for (const auto& child : node["children"]) collect_ids(child, ids);
}

void rename_ids(nlohmann::json& node, std::set<std::string>& ids) {
    if (!node.is_object()) return;
    if (node.contains("id") && node["id"].is_string()) {
        const auto base = node["id"].get<std::string>() + "-copy";
        auto id = base;
        for (int n = 2; ids.count(id); ++n) id = base + "-" + std::to_string(n);
        ids.insert(id);
        node["id"] = id;
    }
    if (node.contains("name") && node["name"].is_string()) node["name"] = node["name"].get<std::string>() + " copy";
    if (node.contains("children") && node["children"].is_array())
        for (auto& child : node["children"]) rename_ids(child, ids);
}

} // namespace

// The selection's handles: within 6 px of a corner or a side. Text resizes only sideways (its
// height follows its lines), so it answers to the left and right sides alone.
int TesseractCanvas::edges_at(float x, float y) const {
    const auto* node = find(selected_id_);
    if (!node) return EdgeNone;
    const auto b = screen_bounds(selected_id_);
    const float reach = 6.0f;
    if (x < b.x - reach || x > b.x + b.width + reach || y < b.y - reach || y > b.y + b.height + reach) return EdgeNone;
    int edges = EdgeNone;
    const float to_left = std::abs(x - b.x);
    const float to_right = std::abs(x - (b.x + b.width));
    if (std::min(to_left, to_right) <= reach) edges |= to_left <= to_right ? EdgeLeft : EdgeRight;
    if (node->kind != TesseractNodeKind::Text) {
        const float to_top = std::abs(y - b.y);
        const float to_bottom = std::abs(y - (b.y + b.height));
        if (std::min(to_top, to_bottom) <= reach) edges |= to_top <= to_bottom ? EdgeTop : EdgeBottom;
    }
    return edges;
}

// Edges and centers of the other root frames along one axis: what a root frame snaps to.
std::vector<float> TesseractCanvas::snap_targets(bool horizontal) const {
    std::vector<float> targets;
    const auto* moving = root(selected_id_);
    for (const auto& other : roots_) {
        if (&other == moving) continue;
        const float start = horizontal ? other.bounds.x : other.bounds.y;
        const float size = horizontal ? other.bounds.width : other.bounds.height;
        targets.insert(targets.end(), {start, start + size * 0.5f, start + size});
    }
    return targets;
}

// Snaps `value` to the nearest target within 6 screen pixels; `guide` receives the target, or NaN.
float TesseractCanvas::snap_axis(float value, const std::vector<float>& targets, float& guide) const {
    guide = std::numeric_limits<float>::quiet_NaN();
    float best = 6.0f / zoom_;
    float snapped = value;
    for (const float target : targets) {
        if (std::abs(target - value) < best) {
            best = std::abs(target - value);
            snapped = target;
            guide = target;
        }
    }
    return snapped;
}

void TesseractCanvas::apply_move(TesseractNode& node, float dx, float dy, bool snap) {
    float x = start_x_ + dx;
    float y = start_y_ + dy;
    guide_x_.clear();
    guide_y_.clear();
    // Root frames snap any of their edges or their center to the other frames'.
    if (snap && root(node.id) == &node) {
        const auto snap_rect = [&](float& position, float size, const std::vector<float>& targets, std::vector<float>& guides) {
            float best = 6.0f / zoom_;
            float shift = 0.0f;
            float found = std::numeric_limits<float>::quiet_NaN();
            for (const float anchor : {0.0f, size * 0.5f, size}) {
                for (const float target : targets) {
                    const float delta = target - (position + anchor);
                    if (std::abs(delta) < best) {
                        best = std::abs(delta);
                        shift = delta;
                        found = target;
                    }
                }
            }
            position += shift;
            if (!std::isnan(found)) guides.push_back(found);
        };
        snap_rect(x, node.bounds.width, snap_targets(true), guide_x_);
        snap_rect(y, node.bounds.height, snap_targets(false), guide_y_);
    }
    node.x = std::round(x);
    node.y = std::round(y);
    node.has_x = node.has_y = true;
}

// Resizes from the dragged sides: the opposite side stays put (or the center with Alt), Shift keeps
// the proportions on a corner, and root frames snap the moving side to the other frames.
void TesseractCanvas::apply_resize(TesseractNode& node, float dx, float dy, unsigned modifiers) {
    const bool left = resize_edges_ & EdgeLeft, right = resize_edges_ & EdgeRight;
    const bool top = resize_edges_ & EdgeTop, bottom = resize_edges_ & EdgeBottom;
    const bool centered = modifiers & R_MOD_ALT;
    const bool proportional = (modifiers & R_MOD_SHIFT) && (left || right) && (top || bottom);
    const float factor = centered ? 2.0f : 1.0f;
    float width = start_width_ + (right ? dx : left ? -dx : 0.0f) * factor;
    float height = start_height_ + (bottom ? dy : top ? -dy : 0.0f) * factor;
    if (proportional && start_width_ > 0.0f && start_height_ > 0.0f) {
        const float ratio = start_width_ / start_height_;
        if (std::abs(width / start_width_ - 1.0f) >= std::abs(height / start_height_ - 1.0f)) height = width / ratio;
        else width = height * ratio;
    }
    float x = start_x_;
    float y = start_y_;
    guide_x_.clear();
    guide_y_.clear();
    if (root(node.id) == &node && !centered && !proportional) {
        float guide = 0.0f;
        if (right) {
            width = snap_axis(start_x_ + width, snap_targets(true), guide) - start_x_;
            if (!std::isnan(guide)) guide_x_.push_back(guide);
        } else if (left) {
            const float edge = snap_axis(start_x_ + start_width_ - width, snap_targets(true), guide);
            width = start_x_ + start_width_ - edge;
            if (!std::isnan(guide)) guide_x_.push_back(guide);
        }
        if (bottom) {
            height = snap_axis(start_y_ + height, snap_targets(false), guide) - start_y_;
            if (!std::isnan(guide)) guide_y_.push_back(guide);
        } else if (top) {
            const float edge = snap_axis(start_y_ + start_height_ - height, snap_targets(false), guide);
            height = start_y_ + start_height_ - edge;
            if (!std::isnan(guide)) guide_y_.push_back(guide);
        }
    }
    width = std::max(1.0f, std::round(width));
    height = std::max(1.0f, std::round(height));
    if (centered) {
        x = start_x_ - (width - start_width_) * 0.5f;
        y = start_y_ - (height - start_height_) * 0.5f;
    } else {
        if (left) x = start_x_ + start_width_ - width;
        if (top) y = start_y_ + start_height_ - height;
    }
    if (left || right) {
        node.width = {TesseractSizeKind::Fixed, width};
        if (node.kind == TesseractNodeKind::Text) node.text_growth = "fixed-width";
        // A child in an automatic layout keeps its place in the flow; only free nodes move.
        if ((left || centered) && !in_flow_) {
            node.x = std::round(x);
            node.has_x = true;
        }
    }
    if ((top || bottom) && node.kind != TesseractNodeKind::Text) {
        node.height = {TesseractSizeKind::Fixed, height};
        if ((top || centered) && !in_flow_) {
            node.y = std::round(y);
            node.has_y = true;
        }
    }
}

// Where a dragged child of an automatic layout would land: before the first sibling whose center
// lies past the pointer along the layout's axis.
void TesseractCanvas::update_reorder(float x, float y) {
    reorder_index_ = -1;
    const auto* node = find(selected_id_);
    const auto* parent_node = parent(selected_id_);
    if (!node || !parent_node) return;
    const bool vertical = parent_node->layout == "vertical";
    const float pointer = vertical ? (y - pan_y_) / zoom_ : (x - pan_x_) / zoom_;
    int index = 0;
    int self = -1;
    const TesseractNode* before = nullptr;
    const TesseractNode* last = nullptr;
    for (std::size_t i = 0; i < parent_node->children.size(); ++i) {
        const auto& sibling = parent_node->children[i];
        if (sibling.id == node->id) {
            self = static_cast<int>(i);
            continue;
        }
        if (sibling.has_x || sibling.has_y) continue;
        last = &sibling;
        const float center = vertical ? sibling.bounds.y + sibling.bounds.height * 0.5f : sibling.bounds.x + sibling.bounds.width * 0.5f;
        if (pointer > center) index = static_cast<int>(i) + 1 - (self >= 0 ? 1 : 0);
        else if (!before) before = &sibling;
    }
    reorder_index_ = index;
    // The insertion line sits in the gap before `before`, or after the last sibling.
    const float gap = parent_node->gap * 0.5f;
    const float cross_start = vertical ? parent_node->bounds.x + parent_node->padding[3] : parent_node->bounds.y + parent_node->padding[0];
    const float cross_size = vertical ? parent_node->bounds.width - parent_node->padding[1] - parent_node->padding[3]
                                      : parent_node->bounds.height - parent_node->padding[0] - parent_node->padding[2];
    float along = 0.0f;
    if (before) along = (vertical ? before->bounds.y : before->bounds.x) - gap;
    else if (last) along = (vertical ? last->bounds.y + last->bounds.height : last->bounds.x + last->bounds.width) + gap;
    else along = vertical ? node->bounds.y : node->bounds.x;
    if (vertical) reorder_line_ = {pan_x_ + cross_start * zoom_, pan_y_ + along * zoom_ - 1.0f, cross_size * zoom_, 2.0f};
    else reorder_line_ = {pan_x_ + along * zoom_ - 1.0f, pan_y_ + cross_start * zoom_, 2.0f, cross_size * zoom_};
}

// Moves the selected child to another position among its siblings, as one undoable step.
bool TesseractCanvas::move_in_flow(int to_index) {
    const auto* selected = find(selected_id_);
    if (!selected || selected->path.size() < 2 || to_index < 0) return false;
    auto* parent_json = &document_;
    for (std::size_t i = 0; i + 1 < selected->path.size(); ++i) parent_json = &(*parent_json)["children"][selected->path[i]];
    auto& siblings = (*parent_json)["children"];
    const auto from = selected->path.back();
    if (!siblings.is_array() || from >= siblings.size()) return false;
    const auto target = std::min<std::size_t>(static_cast<std::size_t>(to_index), siblings.size() - 1);
    if (target == from) return false;
    auto node = siblings[from];
    siblings.erase(siblings.begin() + static_cast<std::ptrdiff_t>(from));
    siblings.insert(siblings.begin() + static_cast<std::ptrdiff_t>(target), node);
    return commit_document();
}

// Writes what a finished drag changed into the document as one undoable step.
void TesseractCanvas::persist_drag(DragMode mode) {
    auto* selected = find_mutable(selected_id_);
    if (!selected) return;
    auto* raw = raw_node(*selected);
    if (!raw) return;
    if (mode == DragMode::Move) {
        (*raw)["x"] = selected->x;
        (*raw)["y"] = selected->y;
    } else {
        if (resize_edges_ & (EdgeLeft | EdgeRight)) {
            (*raw)["width"] = selected->width.value;
            if (selected->kind == TesseractNodeKind::Text) (*raw)["textGrowth"] = "fixed-width";
        }
        if ((resize_edges_ & (EdgeTop | EdgeBottom)) && selected->kind != TesseractNodeKind::Text) {
            (*raw)["height"] = selected->height.value;
        }
        if (selected->has_x) (*raw)["x"] = selected->x;
        if (selected->has_y) (*raw)["y"] = selected->y;
    }
    save_selected();
}

bool TesseractCanvas::nudge_selected(float dx, float dy) {
    auto* node = find_mutable(selected_id_);
    if (!node) return false;
    float x = node->x;
    float y = node->y;
    if (const auto* parent_node = parent(selected_id_)) {
        if (!node->has_x) x = node->bounds.x - parent_node->bounds.x - parent_node->padding[3];
        if (!node->has_y) y = node->bounds.y - parent_node->bounds.y - parent_node->padding[0];
    } else {
        if (!node->has_x) x = node->bounds.x;
        if (!node->has_y) y = node->bounds.y;
    }
    auto* raw = raw_node(*node);
    if (!raw) return false;
    (*raw)["x"] = std::round(x + dx);
    (*raw)["y"] = std::round(y + dy);
    return commit_document();
}

bool TesseractCanvas::duplicate_selected() {
    const auto* selected = find(selected_id_);
    if (!selected || selected->path.empty()) return false;
    auto* parent_json = &document_;
    for (std::size_t i = 0; i + 1 < selected->path.size(); ++i) parent_json = &(*parent_json)["children"][selected->path[i]];
    auto& siblings = (*parent_json)["children"];
    const auto index = selected->path.back();
    if (!siblings.is_array() || index >= siblings.size()) return false;
    auto copy = siblings[index];
    std::set<std::string> ids;
    collect_ids(document_, ids);
    rename_ids(copy, ids);
    if (selected->path.size() == 1) {
        // A root copy sits to the right of the original, like a new artboard.
        copy["x"] = std::round(selected->bounds.x + selected->bounds.width + 40.0f);
        copy["y"] = std::round(selected->bounds.y);
    } else if (copy.contains("x") || copy.contains("y")) {
        copy["x"] = copy.value("x", 0.0f) + 16.0f;
        copy["y"] = copy.value("y", 0.0f) + 16.0f;
    }
    const auto new_id = copy["id"].get<std::string>();
    siblings.insert(siblings.begin() + static_cast<std::ptrdiff_t>(index + 1), copy);
    if (!commit_document()) return false;
    select(new_id);
    return true;
}

int TesseractCanvas::event(Rid widget, const REvent* event) {
    switch (event->type) {
    case R_EVENT_MOUSE_DOWN: {
        Rwidget_focus(widget);
        if (event->button != R_MOUSE_LEFT) return R_TRUE;
        down_x_ = event->local_x;
        down_y_ = event->local_y;
        dragging_ = false;
        drag_mode_ = DragMode::None;
        // Ctrl+click on Linux and Windows (Cmd+click on macOS) picks the deepest element.
        const auto chain = chain_at(down_x_, down_y_);
        const TesseractNode* node = (event->modifiers & (R_MOD_CTRL | R_MOD_SUPER)) ? (chain.empty() ? nullptr : chain.back())
                                                                                   : scoped_pick(chain, event->clicks >= 2);
        // A handle of the current selection wins over whatever lies under it.
        if (edges_at(down_x_, down_y_) != EdgeNone) node = find(selected_id_);
        if (space_down_) node = nullptr;
        if (!node) {
            selected_id_.clear();
            drag_mode_ = DragMode::Pan;
            tracking_ = true;
            return R_TRUE;
        }
        selected_id_ = node->id;
        if (event->clicks >= 2 && node->kind == TesseractNodeKind::Text) {
            text_edit_requested_ = true;
            tracking_ = false;
            Rwidget_invalidate(widget);
            return R_TRUE;
        }
        resize_edges_ = edges_at(down_x_, down_y_);
        drag_mode_ = resize_edges_ != EdgeNone ? DragMode::Resize : DragMode::Move;
        start_width_ = node->bounds.width;
        start_height_ = node->bounds.height;
        start_x_ = node->x;
        start_y_ = node->y;
        in_flow_ = false;
        if (const auto* parent_node = parent(selected_id_)) {
            if (!node->has_x) start_x_ = node->bounds.x - parent_node->bounds.x - parent_node->padding[3];
            if (!node->has_y) start_y_ = node->bounds.y - parent_node->bounds.y - parent_node->padding[0];
            in_flow_ = parent_node->layout != "none" && !node->has_x && !node->has_y;
        } else {
            if (!node->has_x) start_x_ = node->bounds.x;
            if (!node->has_y) start_y_ = node->bounds.y;
        }
        tracking_ = true;
        Rwidget_invalidate(widget);
        return R_TRUE;
    }
    case R_EVENT_MOUSE_MOVE:
        if (tracking_) {
            const float dx = event->local_x - down_x_;
            const float dy = event->local_y - down_y_;
            if (dx * dx + dy * dy > 12.0f) dragging_ = true;
            if (dragging_) {
                navigating_ = true;
                if (drag_mode_ == DragMode::Pan) {
                    pan_x_ += event->delta_x;
                    pan_y_ += event->delta_y;
                    Rwidget_set_cursor(widget, R_CURSOR_MOVE);
                } else if (auto* selected = find_mutable(selected_id_)) {
                    if (drag_mode_ == DragMode::Move && in_flow_) {
                        // A child of an automatic layout is reordered, not placed: it stays in
                        // the flow while a ghost follows the pointer and a line shows where it lands.
                        const auto origin = screen_bounds(selected_id_);
                        drag_ghost_ = {origin.x + dx, origin.y + dy, origin.width, origin.height};
                        update_reorder(event->local_x, event->local_y);
                        Rwidget_set_cursor(widget, R_CURSOR_MOVE);
                        Rwidget_invalidate(widget);
                        return R_TRUE;
                    } else if (drag_mode_ == DragMode::Move) {
                        // Ctrl/Cmd while dragging turns snapping off for fine placement.
                        apply_move(*selected, dx / zoom_, dy / zoom_, !(event->modifiers & (R_MOD_CTRL | R_MOD_SUPER)));
                        Rwidget_set_cursor(widget, R_CURSOR_MOVE);
                    } else if (drag_mode_ == DragMode::Resize) {
                        apply_resize(*selected, dx / zoom_, dy / zoom_, event->modifiers);
                        Rwidget_set_cursor(widget, cursor_for_edges(resize_edges_));
                    }
                    layout();
                }
                Rwidget_invalidate(widget);
            }
        } else {
            const auto chain = chain_at(event->local_x, event->local_y);
            const auto* node = (event->modifiers & (R_MOD_CTRL | R_MOD_SUPER)) ? (chain.empty() ? nullptr : chain.back())
                                                                               : scoped_pick(chain, false);
            const std::string next = node ? node->id : "";
            if (next != hovered_id_) {
                hovered_id_ = next;
                Rwidget_invalidate(widget);
            }
            const int edges = edges_at(event->local_x, event->local_y);
            Rwidget_set_cursor(widget, edges != EdgeNone ? cursor_for_edges(edges) : node ? R_CURSOR_HAND : R_CURSOR_ARROW);
        }
        return R_TRUE;
    case R_EVENT_MOUSE_UP: {
        if (event->button != R_MOUSE_LEFT) return R_TRUE;
        const auto completed_mode = drag_mode_;
        const bool commit = tracking_ && dragging_ && (completed_mode == DragMode::Move || completed_mode == DragMode::Resize);
        tracking_ = false;
        dragging_ = false;
        drag_mode_ = DragMode::None;
        guide_x_.clear();
        guide_y_.clear();
        if (commit && completed_mode == DragMode::Move && in_flow_) {
            const int index = reorder_index_;
            const auto id = selected_id_;
            reorder_index_ = -1;
            if (move_in_flow(index)) select(id);
        } else if (commit) {
            persist_drag(completed_mode);
        }
        reorder_index_ = -1;
        Rwidget_set_cursor(widget, R_CURSOR_ARROW);
        Rwidget_invalidate(widget);
        return R_TRUE;
    }
    case R_EVENT_MOUSE_WHEEL:
        // A trackpad swipe moves the view (pinch zooms, see App::pump); a mouse wheel, or Ctrl/Cmd
        // with the trackpad, zooms at the pointer.
        if (platform::precise_scrolling() && !(event->modifiers & (R_MOD_CTRL | R_MOD_SUPER))) {
            pan_x_ += event->wheel_x;
            pan_y_ += event->wheel_y;
        } else {
            zoom_at(event->local_x, event->local_y, std::exp(event->wheel_y * 0.12f));
        }
        Rwidget_invalidate(widget);
        return R_TRUE;
    case R_EVENT_MOUSE_LEAVE:
        if (!tracking_) {
            hovered_id_.clear();
            Rwidget_invalidate(widget);
        }
        return tracking_ ? R_TRUE : R_FALSE;
    case R_EVENT_KEY_DOWN:
        // Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y (Cmd on macOS) undo and redo canvas edits.
        if ((event->modifiers & (R_MOD_CTRL | R_MOD_SUPER)) && (event->key == R_KEY_Z || event->key == R_KEY_Y)) {
            const bool redo_requested = event->key == R_KEY_Y || (event->modifiers & R_MOD_SHIFT);
            if (redo_requested ? redo() : undo()) Rwidget_invalidate(widget);
            return R_TRUE;
        }
        // Escape during a drag puts the element back where it was.
        if (event->key == R_KEY_ESCAPE && tracking_) {
            tracking_ = dragging_ = false;
            drag_mode_ = DragMode::None;
            guide_x_.clear();
            guide_y_.clear();
            reorder_index_ = -1;
            load();
            Rwidget_set_cursor(widget, R_CURSOR_ARROW);
            Rwidget_invalidate(widget);
            return R_TRUE;
        }
        // Ctrl+D (Cmd+D on macOS) duplicates the selection.
        if ((event->modifiers & (R_MOD_CTRL | R_MOD_SUPER)) && event->key == R_KEY_D) {
            if (duplicate_selected()) Rwidget_invalidate(widget);
            return R_TRUE;
        }
        // Arrows move the selection by a pixel (ten with Shift); with nothing selected they pan.
        if (!selected_id_.empty() && (event->key == R_KEY_LEFT || event->key == R_KEY_RIGHT ||
                                      event->key == R_KEY_UP || event->key == R_KEY_DOWN)) {
            // Inside an automatic layout the arrows along its axis reorder instead.
            const auto* node = find(selected_id_);
            const auto* parent_node = parent(selected_id_);
            if (node && parent_node && parent_node->layout != "none" && !node->has_x && !node->has_y) {
                const bool vertical = parent_node->layout == "vertical";
                const int index = static_cast<int>(node->path.back());
                int to = index;
                if (vertical && event->key == R_KEY_UP) to = index - 1;
                else if (vertical && event->key == R_KEY_DOWN) to = index + 1;
                else if (!vertical && event->key == R_KEY_LEFT) to = index - 1;
                else if (!vertical && event->key == R_KEY_RIGHT) to = index + 1;
                const auto id = selected_id_;
                if (to != index && to >= 0 && move_in_flow(to)) {
                    select(id);
                    Rwidget_invalidate(widget);
                }
                return R_TRUE;
            }
            const float step = (event->modifiers & R_MOD_SHIFT) ? 10.0f : 1.0f;
            const float nx = event->key == R_KEY_LEFT ? -step : event->key == R_KEY_RIGHT ? step : 0.0f;
            const float ny = event->key == R_KEY_UP ? -step : event->key == R_KEY_DOWN ? step : 0.0f;
            if (nudge_selected(nx, ny)) Rwidget_invalidate(widget);
            return R_TRUE;
        }
        // Delete or Backspace removes the selected element (undoable with Cmd/Ctrl+Z).
        if ((event->key == R_KEY_DELETE || event->key == R_KEY_BACKSPACE) && !(event->modifiers & (R_MOD_CTRL | R_MOD_SUPER | R_MOD_ALT))) {
            if (delete_selected()) Rwidget_invalidate(widget);
            return R_TRUE;
        }
        if (event->key == R_KEY_SPACE) space_down_ = true;
        else if (event->key == R_KEY_0) fit();
        else if (event->key == R_KEY_EQUAL || event->key == R_KEY_KP_ADD) zoom_by(1.15f);
        else if (event->key == R_KEY_MINUS || event->key == R_KEY_KP_SUBTRACT) zoom_by(1.0f / 1.15f);
        else if (event->key == R_KEY_LEFT) pan_x_ += 30.0f;
        else if (event->key == R_KEY_RIGHT) pan_x_ -= 30.0f;
        else if (event->key == R_KEY_UP) pan_y_ += 30.0f;
        else if (event->key == R_KEY_DOWN) pan_y_ -= 30.0f;
        else if (event->key == R_KEY_ENTER) {
            const auto* selected = find(selected_id_);
            if (selected && (event->modifiers & R_MOD_SHIFT)) {
                if (const auto* parent_node = parent(selected_id_)) selected_id_ = parent_node->id;
            } else if (selected && selected->kind == TesseractNodeKind::Text) text_edit_requested_ = true;
            else if (selected && !selected->children.empty()) selected_id_ = selected->children.front().id;
        }
        else if (event->key == R_KEY_ESCAPE) clear_selection();
        else return R_FALSE;
        Rwidget_invalidate(widget);
        return R_TRUE;
    case R_EVENT_KEY_UP:
        if (event->key == R_KEY_SPACE) {
            space_down_ = false;
            return R_TRUE;
        }
        return R_FALSE;
    default:
        return R_FALSE;
    }
}

} // namespace raven
