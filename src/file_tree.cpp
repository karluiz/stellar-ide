#include "file_tree.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

namespace raven {
namespace {

std::string fold(const std::string& text) {
    std::string folded = text;
    std::transform(folded.begin(), folded.end(), folded.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return folded;
}

// Signed distance from (x, y) to a rounded box centred on (cx, cy) with half extents (hx, hy).
float rounded_box(float x, float y, float cx, float cy, float hx, float hy, float radius) {
    const float qx = std::abs(x - cx) - hx + radius;
    const float qy = std::abs(y - cy) - hy + radius;
    const float outside = std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f));
    return outside + std::min(std::max(qx, qy), 0.0f) - radius;
}

float fill(float distance) { return std::clamp(0.5f - distance, 0.0f, 1.0f); }
float stroke(float distance, float width) { return std::clamp(0.5f - (std::abs(distance) - width * 0.5f), 0.0f, 1.0f); }

} // namespace

std::vector<FileTreeEntry> list_file_tree(const std::filesystem::path& directory) {
    std::vector<FileTreeEntry> entries;
    std::error_code error;
    std::filesystem::directory_iterator it(directory, std::filesystem::directory_options::skip_permission_denied, error);
    if (error) return entries;
    for (const auto end = std::filesystem::directory_iterator(); it != end; it.increment(error)) {
        if (error) break;
        const auto name = it->path().filename().string();
        if (name.empty() || name[0] == '.') continue;
        std::error_code kind_error;
        entries.push_back({it->path(), it->is_directory(kind_error)});
    }
    std::sort(entries.begin(), entries.end(), [](const FileTreeEntry& left, const FileTreeEntry& right) {
        if (left.directory != right.directory) return left.directory;
        const auto a = fold(left.path.filename().string());
        const auto b = fold(right.path.filename().string());
        return a != b ? a < b : left.path.filename() < right.path.filename();
    });
    return entries;
}

std::vector<std::uint8_t> file_tree_icon(FileTreeIcon icon, int size, float r, float g, float b, float a) {
    std::vector<std::uint8_t> pixels(static_cast<size_t>(size) * static_cast<size_t>(size) * 4, 0);
    // Drawn on a 40-unit grid; the glyph keeps a margin so a 26px row shows it at about 13px.
    const float unit = static_cast<float>(size) / 40.0f;
    for (int py = 0; py < size; ++py) {
        for (int px = 0; px < size; ++px) {
            const float x = (static_cast<float>(px) + 0.5f) / unit;
            const float y = (static_cast<float>(py) + 0.5f) / unit;
            float coverage = 0.0f;
            if (icon == FileTreeIcon::Folder) {
                // A tab on the upper left joined to a wider body.
                const float tab = rounded_box(x, y, 14.5f, 13.5f, 5.5f, 3.0f, 1.6f);
                const float body = rounded_box(x, y, 20.0f, 21.5f, 11.0f, 7.5f, 2.2f);
                coverage = fill(std::min(tab, body) * unit);
            } else {
                // An outlined page with two lines of text.
                const float page = rounded_box(x, y, 20.0f, 20.0f, 8.0f, 11.0f, 2.2f);
                coverage = stroke(page * unit, 2.0f * unit);
                const float line1 = rounded_box(x, y, 20.0f, 17.0f, 3.8f, 0.9f, 0.9f);
                const float line2 = rounded_box(x, y, 18.6f, 22.0f, 2.4f, 0.9f, 0.9f);
                coverage = std::max(coverage, fill(std::min(line1, line2) * unit));
            }
            auto* pixel = &pixels[(static_cast<size_t>(py) * static_cast<size_t>(size) + static_cast<size_t>(px)) * 4];
            pixel[0] = static_cast<std::uint8_t>(std::lround(std::clamp(r, 0.0f, 1.0f) * 255.0f));
            pixel[1] = static_cast<std::uint8_t>(std::lround(std::clamp(g, 0.0f, 1.0f) * 255.0f));
            pixel[2] = static_cast<std::uint8_t>(std::lround(std::clamp(b, 0.0f, 1.0f) * 255.0f));
            pixel[3] = static_cast<std::uint8_t>(std::lround(std::clamp(coverage * a, 0.0f, 1.0f) * 255.0f));
        }
    }
    return pixels;
}

} // namespace raven
