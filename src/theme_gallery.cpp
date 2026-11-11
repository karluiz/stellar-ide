#include "theme_gallery.hpp"
#include "planet_art.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <initializer_list>

namespace raven {
namespace {

constexpr float pi = 3.14159265358979323846f;

struct Color {
    float r;
    float g;
    float b;
    float a;
};

struct Point {
    float x;
    float y;
};

constexpr Color rgb(uint32_t hex, float alpha = 1.0f) {
    return {static_cast<float>((hex >> 16) & 0xff) / 255.0f,
            static_cast<float>((hex >> 8) & 0xff) / 255.0f,
            static_cast<float>(hex & 0xff) / 255.0f, alpha};
}

std::string xml_tag(const std::string& xml, const char* name) {
    const auto start = xml.find(name);
    if (start == std::string::npos) return {};
    const auto end = xml.find('>', start);
    return end == std::string::npos ? std::string{} : xml.substr(start, end - start + 1);
}

std::string xml_attribute(const std::string& tag, const std::string& name) {
    const std::string needle = name + "=\"";
    const auto start = tag.find(needle);
    if (start == std::string::npos) return {};
    const auto value = start + needle.size();
    const auto end = tag.find('"', value);
    return end == std::string::npos ? std::string{} : tag.substr(value, end - value);
}

std::uint32_t hex_color(const std::string& value) {
    if (value.size() < 7 || value[0] != '#') return 0;
    return static_cast<std::uint32_t>(std::stoul(value.substr(1, 6), nullptr, 16));
}

std::string theme_filename(const std::string& name) {
    static const std::pair<const char*, const char*> files[] = {
        {"EarthPlanet", "earth-planet.xml"}, {"EarthPlanetLight", "earth-planet-light.xml"},
        {"EarthPlanetGlass", "earth-planet-glass.xml"}, {"MillerPlanet", "miller-planet.xml"},
        {"MillerPlanetLight", "miller-planet-light.xml"}, {"MillerPlanetGlass", "miller-planet-glass.xml"},
        {"MannPlanet", "mann-planet.xml"}, {"MannPlanetDark", "mann-planet-dark.xml"},
        {"MannPlanetGlass", "mann-planet-glass.xml"}, {"EdmundsPlanet", "edmunds-planet.xml"},
        {"EdmundsPlanetDark", "edmunds-planet-dark.xml"}, {"EdmundsPlanetGlass", "edmunds-planet-glass.xml"},
        {"Gargantua", "gargantua.xml"}, {"GargantuaLight", "gargantua-light.xml"},
        {"GargantuaGlass", "gargantua-glass.xml"}
    };
    for (const auto& [theme, file] : files) if (name == theme) return file;
    return {};
}

ThemePreviewPalette read_palette(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) return {};
    const std::string xml((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    const auto root = xml_tag(xml, "<Theme");
    const auto colors = xml_tag(xml, "<Colors");
    ThemePreviewPalette palette;
    const auto base = xml_attribute(root, "Base");
    if (!base.empty() && base != "Dark" && base != "Light") {
        const auto parent = theme_filename(base);
        if (!parent.empty()) palette = read_palette(path.parent_path() / parent);
    }
    static const char* color_keys[] = {
        "Window", "Panel", "Input", "Text", "TextDisabled", "Border", "Control", "Selection",
        "Header", "TabActive", "Accent", "AccentText", "Link"
    };
    for (const auto* key : color_keys) {
        const auto value = xml_attribute(key == std::string("Accent") || key == std::string("AccentText") ? root : colors, key);
        if (!value.empty()) palette[key] = hex_color(value);
    }
    return palette;
}

Color palette_color(const ThemePreviewPalette& palette, const char* key, std::uint32_t fallback, float alpha = 1.0f) {
    const auto found = palette.find(key);
    return rgb(found == palette.end() ? fallback : found->second, alpha);
}

void circle(RPainter* painter, float x, float y, float radius, Color color) {
    Rdraw_circle(painter, x, y, radius, color.r, color.g, color.b, color.a);
}

void line(RPainter* painter, float x0, float y0, float x1, float y1, float thickness, Color color) {
    Rdraw_line(painter, x0, y0, x1, y1, thickness, color.r, color.g, color.b, color.a);
}

void polygon(RPainter* painter, std::initializer_list<Point> points, Color color) {
    if (points.size() < 3) return;
    const auto first = points.begin();
    for (auto point = first + 1; point + 1 != points.end(); ++point) {
        Rdraw_triangle(painter, first->x, first->y, point->x, point->y, (point + 1)->x, (point + 1)->y,
                       color.r, color.g, color.b, color.a);
    }
}

Point rotated(float cx, float cy, float x, float y, float rotation) {
    return {cx + x * std::cos(rotation) - y * std::sin(rotation),
            cy + x * std::sin(rotation) + y * std::cos(rotation)};
}

void ellipse(RPainter* painter, float cx, float cy, float rx, float ry, float rotation,
             float thickness, Color color, bool upper_only = false) {
    constexpr int segments = 40;
    const int first = upper_only ? segments / 2 : 0;
    for (int i = first; i < segments; ++i) {
        const auto a = rotated(cx, cy, std::cos(static_cast<float>(i) * 2.0f * pi / segments) * rx,
                               std::sin(static_cast<float>(i) * 2.0f * pi / segments) * ry, rotation);
        const auto b = rotated(cx, cy, std::cos(static_cast<float>(i + 1) * 2.0f * pi / segments) * rx,
                               std::sin(static_cast<float>(i + 1) * 2.0f * pi / segments) * ry, rotation);
        line(painter, a.x, a.y, b.x, b.y, thickness, color);
    }
}

void sky(RPainter* painter, float x, float y, float width, float height, bool dark, float time) {
    const auto background = rgb(dark ? 0x071117 : 0xEAF2EF);
    Rdraw_rect(painter, x, y, width, height, std::min(4.0f, height * 0.05f),
               background.r, background.g, background.b, 1.0f);
    if (height < 42.0f) return;
    for (int i = 0; i < 28; ++i) {
        const float star_x = x + (0.03f + static_cast<float>((i * 37) % 93) / 100.0f) * width;
        const float star_y = y + (0.05f + static_cast<float>((i * 53) % 89) / 100.0f) * height;
        const float twinkle = 0.25f + 0.25f * (1.0f + std::sin(time * 0.8f + static_cast<float>(i) * 1.7f));
        circle(painter, star_x, star_y, i % 4 == 0 ? 0.9f : 0.55f, rgb(dark ? 0xD7EEEF : 0x6B8582, twinkle));
    }
}

void sphere(RPainter* painter, float cx, float cy, float radius, Color base, bool dark) {
    circle(painter, cx + 2.0f, cy + 3.0f, radius + 2.0f, rgb(dark ? 0x000000 : 0x536863, 0.28f));
    circle(painter, cx, cy, radius + 1.2f, rgb(base.r * 255.0f > 180 ? 0xB8E4E5 : 0x77B9C4, 0.20f));
    circle(painter, cx, cy, radius, base);
    circle(painter, cx - radius * 0.30f, cy - radius * 0.32f, radius * 0.48f, rgb(0xFFFFFF, 0.12f));
    ellipse(painter, cx, cy + radius * 0.28f, radius * 0.74f, radius * 0.26f, 0.08f, 0.8f, rgb(0x061318, 0.20f), true);
}

void earth(RPainter* painter, float cx, float cy, float radius, float time, bool light) {
    sphere(painter, cx, cy, radius, rgb(light ? 0x318EB5 : 0x15597D), !light);
    const float drift = std::sin(time * 0.32f) * radius * 0.20f;
    const auto land = rgb(light ? 0x55A66B : 0x347B51);
    const auto highland = rgb(light ? 0x91C778 : 0x4C9562);
    polygon(painter, {{cx - radius * 0.64f + drift, cy - radius * 0.30f},
                      {cx - radius * 0.22f + drift, cy - radius * 0.58f},
                      {cx + radius * 0.02f + drift, cy - radius * 0.28f},
                      {cx - radius * 0.10f + drift, cy - radius * 0.02f},
                      {cx - radius * 0.30f + drift, cy + radius * 0.10f},
                      {cx - radius * 0.52f + drift, cy - radius * 0.02f}}, land);
    polygon(painter, {{cx + radius * 0.12f + drift, cy - radius * 0.18f},
                      {cx + radius * 0.56f + drift, cy - radius * 0.34f},
                      {cx + radius * 0.70f + drift, cy - radius * 0.02f},
                      {cx + radius * 0.38f + drift, cy + radius * 0.18f},
                      {cx + radius * 0.24f + drift, cy + radius * 0.58f},
                      {cx + radius * 0.02f + drift, cy + radius * 0.30f}}, highland);
    for (int band = -1; band <= 1; ++band) {
        const float phase = time * 0.55f + static_cast<float>(band) * 1.8f;
        const float yy = cy + static_cast<float>(band) * radius * 0.42f;
        Rdraw_curve(painter, cx - radius * 0.72f, yy,
                    cx - radius * 0.22f, yy - std::sin(phase) * radius * 0.20f,
                    cx + radius * 0.22f, yy + std::cos(phase) * radius * 0.16f,
                    cx + radius * 0.72f, yy - radius * 0.05f,
                    1.2f, 0.94f, 0.99f, 1.0f, 0.58f);
    }
}

void miller(RPainter* painter, float cx, float cy, float radius, float time, bool light) {
    sphere(painter, cx, cy, radius, rgb(light ? 0x3E9BB8 : 0x1D6987), !light);
    for (int band = -3; band <= 3; ++band) {
        const float yy = cy + static_cast<float>(band) * radius * 0.22f;
        const float half = std::sqrt(std::max(0.0f, radius * radius - (yy - cy) * (yy - cy))) * 0.90f;
        for (int segment = 0; segment < 10; ++segment) {
            const float x0 = cx - half + 2.0f * half * static_cast<float>(segment) / 10.0f;
            const float x1 = cx - half + 2.0f * half * static_cast<float>(segment + 1) / 10.0f;
            const float phase0 = static_cast<float>(segment) * pi / 2.5f + time * 1.15f + band;
            const float phase1 = static_cast<float>(segment + 1) * pi / 2.5f + time * 1.15f + band;
            line(painter, x0, yy + std::sin(phase0) * radius * 0.055f,
                 x1, yy + std::sin(phase1) * radius * 0.055f,
                 band % 2 ? 1.0f : 1.5f, rgb(0xC4F2F2, 0.60f));
        }
    }
    for (int i = 0; i < 5; ++i) {
        const float angle = time * 0.45f + static_cast<float>(i) * 2.0f * pi / 5.0f;
        circle(painter, cx + std::cos(angle) * radius * 1.30f, cy + std::sin(angle) * radius * 0.54f,
               1.0f, rgb(0xB6E4E8, 0.45f));
    }
}

void mann(RPainter* painter, float cx, float cy, float radius, float time, bool light) {
    sphere(painter, cx, cy, radius, rgb(light ? 0xB8D8DE : 0x5D8290), !light);
    const auto ice = rgb(0xE9F7F6, 0.72f);
    polygon(painter, {{cx - radius * 0.70f, cy - radius * 0.22f}, {cx - radius * 0.20f, cy - radius * 0.70f},
                      {cx + radius * 0.12f, cy - radius * 0.20f}, {cx - radius * 0.18f, cy + radius * 0.02f}}, ice);
    polygon(painter, {{cx + radius * 0.10f, cy + radius * 0.06f}, {cx + radius * 0.66f, cy - radius * 0.20f},
                      {cx + radius * 0.48f, cy + radius * 0.44f}, {cx - radius * 0.10f, cy + radius * 0.62f}}, rgb(0xD2ECEF, 0.64f));
    const float shimmer = std::sin(time * 0.65f) * radius * 0.55f;
    line(painter, cx - radius * 0.58f + shimmer, cy - radius * 0.56f,
         cx + radius * 0.10f + shimmer, cy + radius * 0.52f, 2.0f, rgb(0xFFFFFF, 0.32f));
    ellipse(painter, cx, cy, radius * 1.28f, radius * 0.36f, -0.12f, 0.8f, rgb(0xD9F2F2, 0.35f));
}

void edmunds(RPainter* painter, float cx, float cy, float radius, float time, bool light) {
    sphere(painter, cx, cy, radius, rgb(light ? 0xD3B57C : 0x987444), !light);
    circle(painter, cx - radius * 0.32f, cy - radius * 0.18f, radius * 0.22f, rgb(0x6F5131, 0.30f));
    circle(painter, cx - radius * 0.35f, cy - radius * 0.22f, radius * 0.14f, rgb(0xF0D6A0, 0.24f));
    circle(painter, cx + radius * 0.30f, cy + radius * 0.26f, radius * 0.17f, rgb(0x6F5131, 0.24f));
    ellipse(painter, cx, cy + radius * 0.04f, radius * 0.84f, radius * 0.24f, 0.18f, 1.0f, rgb(0x6F5131, 0.36f));
    for (int i = 0; i < 12; ++i) {
        const float phase = time * 0.28f + static_cast<float>(i) * 2.39996323f;
        const float orbit = radius * (1.18f + static_cast<float>(i % 3) * 0.16f);
        circle(painter, cx + std::cos(phase) * orbit, cy + std::sin(phase) * orbit * 0.40f,
               0.7f + static_cast<float>(i % 2) * 0.4f, rgb(light ? 0xA37C42 : 0xE1BD7C, 0.48f));
    }
}

void gargantua(RPainter* painter, float cx, float cy, float radius, float time, bool light) {
    const auto gold = rgb(light ? 0xA87328 : 0xF2B35D);
    const float pulse = 1.0f + 0.025f * std::sin(time * 0.85f);
    const float rotation = -0.18f + 0.025f * std::sin(time * 0.22f);
    for (int ring = 4; ring >= 0; --ring) {
        ellipse(painter, cx, cy, radius * (1.55f + ring * 0.22f) * pulse,
                radius * (0.30f + ring * 0.065f) * pulse, rotation,
                ring == 0 ? 2.0f : 0.9f, rgb(light ? 0xA87328 : 0xF2B35D, 0.20f + (4 - ring) * 0.11f));
    }
    for (int i = 0; i < 28; ++i) {
        const float phase = time * (0.34f + static_cast<float>(i % 4) * 0.025f) + static_cast<float>(i) * 2.39996323f;
        const float orbit = radius * (1.45f + static_cast<float>(i % 6) * 0.15f);
        const auto point = rotated(cx, cy, std::cos(phase) * orbit, std::sin(phase) * orbit * 0.24f, rotation);
        circle(painter, point.x, point.y, 0.65f + static_cast<float>(i % 3) * 0.35f,
               rgb(light ? 0xA87328 : 0xF8D49A, 0.35f + static_cast<float>(i % 4) * 0.12f));
    }
    circle(painter, cx + 1.5f, cy + 2.5f, radius * 1.08f, rgb(light ? 0x9B8870 : 0x6A5232, 0.30f));
    circle(painter, cx, cy, radius, rgb(0x010203));
    ellipse(painter, cx, cy, radius * 1.55f * pulse, radius * 0.30f * pulse, rotation, 1.8f, gold, true);
    circle(painter, cx - radius * 0.18f, cy - radius * 0.20f, radius * 0.72f, rgb(0x000000, 0.70f));
}

int family_for_theme(int index) {
    return index >= 12 && index <= 14 ? 0 : index == 3 || index == 8 || index == 15 ? 1 :
        index == 4 || index == 9 || index == 16 ? 2 : index == 5 || index == 10 || index == 17 ? 3 :
        index == 6 || index == 11 || index == 18 ? 4 : 5;
}

void paint_planet(RPainter* painter, int index, float cx, float cy, float radius, float time, Rid texture,
                  Rid shader, bool animate) {
    const int family = family_for_theme(index);
    const bool light = index == 1 || index == 4 || index == 5 || index == 7 || index == 8 || index == 11 || index == 13;
    const float size = radius * 2.44f;
    if (family >= 0 && family < 5 && Rshader_is_valid(shader)) {
        RShaderLook look;
        Rshader_look_init(&look);
        look.params[0] = static_cast<float>(family);
        look.params[1] = light ? 1.0f : 0.0f;
        look.texture = R_INVALID_ID;
        look.pointer = R_FALSE;
        look.animate = animate ? R_TRUE : R_FALSE;
        Rdraw_shader(painter, shader, cx - size * 0.5f, cy - size * 0.5f, size, size, &look);
        return;
    }
    if (family >= 0 && family < 5 && Rtexture_is_valid(texture)) {
        Rdraw_image(painter, texture, cx - size * 0.5f, cy - size * 0.5f, size, size, 1.0f, 1.0f, 1.0f, 1.0f);
        if (family == 4 && radius > 12.0f) {
            for (int i = 0; i < 14; ++i) {
                const float phase = time * 0.24f + static_cast<float>(i) * 2.39996323f;
                const float orbit = radius * (1.03f + static_cast<float>(i % 3) * 0.10f);
                const auto point = rotated(cx, cy, std::cos(phase) * orbit, std::sin(phase) * orbit * 0.28f, -0.18f);
                circle(painter, point.x, point.y, std::max(0.45f, radius * 0.009f), rgb(0xFFD898, 0.36f + (i % 3) * 0.12f));
            }
        }
        return;
    }
    switch (family) {
    case 0: earth(painter, cx, cy, radius, time, light); break;
    case 1: miller(painter, cx, cy, radius, time, light); break;
    case 2: mann(painter, cx, cy, radius, time, light); break;
    case 3: edmunds(painter, cx, cy, radius, time, light); break;
    case 4: gargantua(painter, cx, cy, radius, time, light); break;
    default: sphere(painter, cx, cy, radius, rgb(0x799CA4), !light); break;
    }
}

void paint_scene(RPainter* painter, int index, float x, float y, float width, float height,
                 float time, float hover, Rid texture, Rid shader) {
    const bool light = index == 1 || index == 4 || index == 5 || index == 7 || index == 8 || index == 11 || index == 13;
    sky(painter, x, y, width, height, !light, time);
    if (height > 42.0f) {
        circle(painter, x + width * 0.50f, y + height * 0.56f, std::min(width, height) * 0.43f,
               rgb(light ? 0xFFFFFF : 0x3A6870, light ? 0.20f : 0.12f));
    }
    const float radius = std::min(width, height) * 0.29f * (1.0f + hover * 0.06f);
    paint_planet(painter, index, x + width * 0.5f, y + height * 0.53f - hover * 2.0f, radius, time, texture, shader, false);
}

void paint_workspace(RPainter* painter, int index, const ThemePreviewPalette& palette,
                     float x, float y, float width, float height, float time, Rid texture, Rid shader, bool animate) {
    const bool glass = index >= 14 && index <= 18;
    const auto window = palette_color(palette, "Window", 0x071411);
    const auto panel = palette_color(palette, "Panel", 0x0D211C);
    const auto input = palette_color(palette, "Input", 0x123028);
    const auto text = palette_color(palette, "Text", 0xEAF5EF);
    const auto muted = palette_color(palette, "TextDisabled", 0x9AB8AA);
    const auto border = palette_color(palette, "Border", 0x31594A);
    const auto control = palette_color(palette, "Control", 0x15372C);
    const auto selection = palette_color(palette, "Selection", 0x1D5543);
    const auto accent = palette_color(palette, "Accent", 0x70D6B0);
    const auto accent_text = palette_color(palette, "AccentText", 0x071411);
    const auto header = palette_color(palette, "Header", 0x102A22);
    const auto link = palette_color(palette, "Link", 0x8FD8E8);
    const auto rect = [&](float rx, float ry, float rw, float rh, float radius, Color color) {
        Rdraw_rect(painter, rx, ry, rw, rh, radius, color.r, color.g, color.b, color.a);
    };
    const auto outline = [&](float rx, float ry, float rw, float rh, float radius, Color color) {
        Rdraw_rect_outline(painter, rx, ry, rw, rh, radius, 1.0f, color.r, color.g, color.b, color.a);
    };
    const auto surface = [&](float rx, float ry, float rw, float rh, float radius, Color color) {
        if (glass) Rdraw_glass(painter, rx, ry, rw, rh, radius, color.r, color.g, color.b, 0.52f);
        else rect(rx, ry, rw, rh, radius, color);
    };
    const auto label = [&](float lx, float ly, float size, const char* value, Color color) {
        Rdraw_text(painter, R_INVALID_ID, size, lx, ly, value, color.r, color.g, color.b, color.a);
    };
    const float tiny = std::clamp(height * 0.017f, 5.0f, 9.0f);
    const float small = std::clamp(height * 0.021f, 6.5f, 10.5f);
    const float body = std::clamp(height * 0.025f, 7.5f, 12.0f);
    const float title = std::clamp(height * 0.030f, 9.0f, 14.0f);
    const float header_height = height * 0.12f;
    const float tabs_height = height * 0.072f;
    const float status_height = height * 0.055f;
    const float content_y = y + header_height + tabs_height;
    const float content_height = height - header_height - tabs_height - status_height;
    const float left_width = width * 0.205f;
    const float right_width = width * 0.235f;
    const float center_x = x + left_width;
    const float center_width = width - left_width - right_width;
    const float right_x = x + width - right_width;

    rect(x, y, width, height, 0.0f, window);
    if (glass) {
        for (int step = 14; step >= 1; --step) {
            const float fraction = static_cast<float>(step) / 14.0f;
            circle(painter, center_x + center_width * 0.26f, content_y + content_height * 0.74f,
                   content_height * 0.68f * fraction, {accent.r, accent.g, accent.b, 0.005f + (1.0f - fraction) * 0.004f});
            circle(painter, center_x + center_width * 0.88f, content_y + content_height * 0.10f,
                   content_height * 0.54f * fraction, {link.r, link.g, link.b, 0.005f + (1.0f - fraction) * 0.004f});
        }
    }
    rect(x, y, width, header_height, 0.0f, header);
    rect(x, y + header_height, left_width, height - header_height - status_height, 0.0f,
         {panel.r, panel.g, panel.b, glass ? 0.84f : 1.0f});
    rect(right_x, y + header_height, right_width, height - header_height - status_height, 0.0f,
         {panel.r, panel.g, panel.b, glass ? 0.84f : 1.0f});
    rect(x, y + height - status_height, width, status_height, 0.0f, header);
    line(painter, x, y + header_height, x + width, y + header_height, 1.0f, border);
    line(painter, center_x, y + header_height, center_x, y + height - status_height, 1.0f, border);
    line(painter, right_x, y + header_height, right_x, y + height - status_height, 1.0f, border);
    line(painter, x, content_y, x + width, content_y, 1.0f, border);
    line(painter, x, y + height - status_height, x + width, y + height - status_height, 1.0f, border);

    label(x + width * 0.026f, y + header_height * 0.34f, title, "STELLAR", text);
    label(x + width * 0.115f, y + header_height * 0.39f, small, "Open", muted);
    label(x + width * 0.165f, y + header_height * 0.39f, small, "New chat", muted);
    label(x + width * 0.90f, y + header_height * 0.39f, tiny, "READY", muted);
    label(center_x + center_width * 0.055f, y + header_height + tabs_height * 0.29f, small, "Agent", text);
    label(center_x + center_width * 0.18f, y + header_height + tabs_height * 0.29f, small, "Editor", muted);
    label(center_x + center_width * 0.31f, y + header_height + tabs_height * 0.29f, small, "Missions", muted);
    label(center_x + center_width * 0.47f, y + header_height + tabs_height * 0.29f, small, "More", muted);
    rect(center_x + center_width * 0.052f, content_y - 2.0f, center_width * 0.09f, 2.0f, 0.0f, accent);
    label(right_x + right_width * 0.08f, y + header_height + tabs_height * 0.29f, small, "Activity", text);
    label(right_x + right_width * 0.36f, y + header_height + tabs_height * 0.29f, small, "Wiki", muted);
    label(right_x + right_width * 0.54f, y + header_height + tabs_height * 0.29f, small, "Knowledge", muted);
    rect(right_x + right_width * 0.075f, content_y - 2.0f, right_width * 0.22f, 2.0f, 0.0f, accent);

    const float search_x = x + left_width * 0.08f;
    const float search_y = y + header_height + tabs_height * 0.24f;
    rect(search_x, search_y, left_width * 0.84f, tabs_height * 0.56f, tabs_height * 0.18f, input);
    circle(painter, search_x + left_width * 0.08f, search_y + tabs_height * 0.27f, tiny * 0.42f, muted);
    label(search_x + left_width * 0.16f, search_y + tabs_height * 0.10f, small, "Search…", muted);
    label(x + left_width * 0.07f, y + header_height + tabs_height * 1.13f, small, "Chats", text);
    label(x + left_width * 0.34f, y + header_height + tabs_height * 1.13f, small, "Files", muted);
    label(x + left_width * 0.57f, y + header_height + tabs_height * 1.13f, small, "Agents", muted);
    label(x + left_width * 0.82f, y + header_height + tabs_height * 1.13f, small, "Wiki", muted);
    rect(x + left_width * 0.06f, content_y - 2.0f, left_width * 0.21f, 2.0f, 0.0f, accent);
    label(x + left_width * 0.09f, content_y + content_height * 0.050f, tiny, "Conversations", muted);
    const float thread_y = content_y + content_height * 0.12f;
    rect(x + left_width * 0.055f, thread_y, left_width * 0.89f, content_height * 0.075f, height * 0.008f, selection);
    circle(painter, x + left_width * 0.12f, thread_y + content_height * 0.036f, tiny * 0.38f, accent);
    label(x + left_width * 0.18f, thread_y + content_height * 0.022f, small, "Memory universe", text);
    label(x + left_width * 0.10f, thread_y + content_height * 0.12f, small, "Refine theme gallery", muted);
    label(x + left_width * 0.10f, thread_y + content_height * 0.19f, small, "Review agent memory", muted);

    const int planet_family = family_for_theme(index);
    const bool black_hole = planet_family == 4;
    const float user_width = center_width * 0.48f;
    const float user_x = center_x + center_width - user_width - center_width * 0.065f;
    const float user_y = content_y + content_height * 0.045f;
    surface(user_x, user_y, user_width, content_height * 0.082f, height * 0.018f, control);
    label(user_x + center_width * 0.025f, user_y + content_height * 0.023f, body, "Make the universe feel alive", text);
    const float lead_x = center_x + center_width * 0.065f;
    label(lead_x, content_y + content_height * 0.175f, tiny, "ORCHESTRATOR", accent);
    label(lead_x, content_y + content_height * 0.215f, body, "A living map of reviewed memories,", text);
    label(lead_x, content_y + content_height * 0.255f, body, "with every world rendered locally.", muted);

    const float planet_cx = center_x + center_width * 0.67f;
    const float planet_cy = content_y + content_height * 0.52f;
    const float planet_radius = std::min(center_width, content_height) * (black_hole ? 0.24f : 0.22f);
    if (!black_hole) {
        ellipse(painter, planet_cx, planet_cy, planet_radius * 1.60f, planet_radius * 0.48f, -0.18f, 0.8f,
                {link.r, link.g, link.b, 0.32f});
        ellipse(painter, planet_cx, planet_cy, planet_radius * 1.34f, planet_radius * 0.92f, 0.32f, 0.6f,
                {accent.r, accent.g, accent.b, 0.22f});
        if (planet_family != 1) {
            const float phase = time * (0.34f + planet_family * 0.04f) + planet_family;
            const auto satellite = rotated(planet_cx, planet_cy, std::cos(phase) * planet_radius * 1.60f,
                                           std::sin(phase) * planet_radius * 0.48f, -0.18f);
            circle(painter, satellite.x, satellite.y, std::max(1.2f, planet_radius * 0.025f), accent);
            circle(painter, satellite.x, satellite.y, std::max(0.5f, planet_radius * 0.009f), text);
        }
    }
    paint_planet(painter, index, planet_cx, planet_cy, planet_radius, time, texture, shader, animate);
    label(lead_x, content_y + content_height * 0.43f, tiny, "MEMORY UNIVERSE", link);
    label(lead_x, content_y + content_height * 0.48f, small, "Reviewed knowledge", text);
    label(lead_x, content_y + content_height * 0.53f, small, "Local · inspectable", muted);

    const float activity_x = right_x + right_width * 0.08f;
    const float activity_width = right_width * 0.84f;
    const float activity_y = content_y + content_height * 0.045f;
    surface(activity_x, activity_y, activity_width, content_height * 0.31f, height * 0.020f, input);
    outline(activity_x, activity_y, activity_width, content_height * 0.31f, height * 0.020f, {border.r, border.g, border.b, 0.72f});
    label(activity_x + activity_width * 0.09f, activity_y + content_height * 0.035f, body, "Orchestrator", text);
    circle(painter, activity_x + activity_width * 0.85f, activity_y + content_height * 0.047f, tiny * 0.36f, accent);
    label(activity_x + activity_width * 0.09f, activity_y + content_height * 0.09f, tiny, "Claude Code · workspace", muted);
    label(activity_x + activity_width * 0.09f, activity_y + content_height * 0.14f, small, "Rendering universe", text);
    line(painter, activity_x + activity_width * 0.09f, activity_y + content_height * 0.205f,
         activity_x + activity_width * 0.91f, activity_y + content_height * 0.205f, 1.0f, border);
    label(activity_x + activity_width * 0.09f, activity_y + content_height * 0.235f, tiny, "5 systems · 15 memories", muted);

    const float composer_x = center_x + center_width * 0.065f;
    const float composer_width = center_width * 0.87f;
    const float composer_y = content_y + content_height * 0.79f;
    surface(composer_x, composer_y, composer_width, content_height * 0.155f, height * 0.022f, input);
    outline(composer_x, composer_y, composer_width, content_height * 0.155f, height * 0.022f, border);
    label(composer_x + composer_width * 0.035f, composer_y + content_height * 0.035f, small,
          "Describe a task · / skills · @ files…", muted);
    const float send_x = composer_x + composer_width * 0.92f;
    const float send_y = composer_y + content_height * 0.105f;
    const float send_radius = height * 0.020f;
    circle(painter, send_x, send_y, send_radius, accent);
    line(painter, send_x, send_y + send_radius * 0.38f, send_x, send_y - send_radius * 0.38f, 1.2f, accent_text);
    line(painter, send_x, send_y - send_radius * 0.38f, send_x - send_radius * 0.28f, send_y - send_radius * 0.08f, 1.2f, accent_text);
    line(painter, send_x, send_y - send_radius * 0.38f, send_x + send_radius * 0.28f, send_y - send_radius * 0.08f, 1.2f, accent_text);
    label(composer_x + composer_width * 0.035f, composer_y + content_height * 0.098f, small, "≡  ↶", muted);

    label(x + width * 0.040f, y + height - status_height * 0.68f, tiny, "Workspace ready · 2 agents", muted);
    static const char* names[] = {"Earth", "Miller's Planet", "Mann's Planet", "Edmunds' Planet", "Gargantua"};
    static const char* finishes[] = {"Dark", "Light", "Glass"};
    const int family = family_for_theme(index);
    const int finish = index >= 14 ? 2 : (index == 1 || index == 4 || index == 5 || index == 7 || index == 8 || index == 11 || index == 13) ? 1 : 0;
    if (family >= 0 && family < 5) {
        label(x + width * 0.76f, y + height - status_height * 0.68f, tiny, names[family], link);
        label(x + width * 0.93f, y + height - status_height * 0.68f, tiny, finishes[finish], muted);
    }
}

struct CardRect {
    float x;
    float y;
    float width;
    float height;
};

float wrapped_relative(int family, double position) {
    float relative = std::remainder(static_cast<float>(family) - static_cast<float>(position), 5.0f);
    if (relative > 2.5f) relative -= 5.0f;
    if (relative < -2.5f) relative += 5.0f;
    return relative;
}

CardRect card_rect(float width, float height, float relative) {
    const float distance = std::min(std::abs(relative), 1.0f);
    const float central_width = std::min(width * 0.54f, height * 1.52f);
    const float central_height = std::min(height * 0.88f, central_width * 0.61f);
    const float card_width = central_width * (1.0f - distance * 0.36f);
    const float card_height = central_height * (1.0f - distance * 0.20f);
    return {width * 0.5f + relative * central_width * 0.69f - card_width * 0.5f,
            (height - card_height) * 0.5f + distance * height * 0.035f, card_width, card_height};
}

} // namespace

void ThemeGallery::load_palettes(const std::filesystem::path& theme_directory) {
    static const std::pair<int, const char*> files[] = {
        {3, "miller-planet.xml"}, {4, "mann-planet.xml"}, {5, "edmunds-planet.xml"},
        {6, "gargantua.xml"}, {8, "miller-planet-light.xml"}, {9, "mann-planet-dark.xml"},
        {10, "edmunds-planet-dark.xml"}, {11, "gargantua-light.xml"}, {12, "earth-planet.xml"},
        {13, "earth-planet-light.xml"}, {14, "earth-planet-glass.xml"}, {15, "miller-planet-glass.xml"},
        {16, "mann-planet-glass.xml"}, {17, "edmunds-planet-glass.xml"}, {18, "gargantua-glass.xml"}
    };
    for (const auto& [index, file] : files) palettes_[index] = read_palette(theme_directory / file);
    auto shader_path = theme_directory.parent_path() / "shaders" / "planet.spv";
#ifdef RAVEN_IDE_BUILD_PLANET_SHADER
    if (std::filesystem::exists(RAVEN_IDE_BUILD_PLANET_SHADER)) shader_path = RAVEN_IDE_BUILD_PLANET_SHADER;
#endif
    planet_shader_ = Rshader_create_from_file(shader_path.c_str(), R_SHADER_ANIMATED);
    if (!Rshader_is_valid(planet_shader_)) planet_textures_ = create_planet_textures();
}

bool ThemeGallery::advance(double delta, bool reduce_motion) {
    reduce_motion_ = reduce_motion;
    const double step = std::max(delta, 0.0);
    bool changed = false;
    if (reduce_motion) {
        changed = position_ != target_;
        position_ = target_;
        return changed;
    }
    time_ += step;
    const float blend = 1.0f - std::exp(-static_cast<float>(step) * 13.0f);
    position_ += (target_ - position_) * blend;
    if (std::abs(target_ - position_) < 0.001) position_ = target_;
    if (time_ - animation_frame_ < 1.0 / 30.0) return false;
    animation_frame_ = time_;
    return true;
}

void ThemeGallery::set_family(int family, bool animate) {
    family = std::clamp(family, 0, 4);
    const int difference = family - family_;
    const int wrapped = (difference + 7) % 5 - 2;
    target_ += wrapped;
    family_ = family;
    if (!animate) position_ = target_;
}

void ThemeGallery::set_finish(int finish) { finish_ = std::clamp(finish, 0, 2); }

int ThemeGallery::move(int delta, bool animate) {
    target_ += delta;
    family_ = (family_ + delta % 5 + 5) % 5;
    if (!animate) position_ = target_;
    return family_;
}

int ThemeGallery::family() const { return family_; }

int ThemeGallery::hit_direction(float x, float y) const {
    if (width_ <= 0.0f || height_ <= 0.0f) return 0;
    int direction = 0;
    float nearest = 0.25f;
    for (int family = 0; family < 5; ++family) {
        const float relative = wrapped_relative(family, position_);
        const auto rect = card_rect(width_, height_, relative);
        const float distance = std::abs(relative);
        if (distance >= nearest || x < rect.x || x > rect.x + rect.width || y < rect.y || y > rect.y + rect.height) continue;
        nearest = distance;
        direction = relative < 0.0f ? -1 : 1;
    }
    return direction;
}

int ThemeGallery::event(Rid widget, const REvent* event) {
    if (event->type == R_EVENT_MOUSE_WHEEL) {
        if (std::abs(event->wheel_y) < 0.0001f) return 0;
        const auto now = std::chrono::steady_clock::now();
        if (now - last_wheel_ < std::chrono::milliseconds(300)) return 0;
        last_wheel_ = now;
        return event->wheel_y > 0.0f ? -1 : 1;
    }
    if (event->type == R_EVENT_MOUSE_DOWN) {
        Rwidget_focus(widget);
        tracking_ = event->button == R_MOUSE_LEFT;
        return 0;
    }
    if (event->type == R_EVENT_MOUSE_MOVE) {
        hovered_direction_ = hit_direction(event->local_x, event->local_y);
        Rwidget_set_cursor(widget, hovered_direction_ == 0 ? R_CURSOR_ARROW : R_CURSOR_HAND);
        return 0;
    }
    if (event->type == R_EVENT_MOUSE_LEAVE) {
        hovered_direction_ = 0;
        return R_FALSE;
    }
    if (event->type == R_EVENT_MOUSE_UP && event->button == R_MOUSE_LEFT) {
        const int direction = tracking_ ? hit_direction(event->local_x, event->local_y) : 0;
        tracking_ = false;
        return direction;
    }
    return 0;
}

void ThemeGallery::paint(Rid widget, RPainter* painter, float width, float height) const {
    const int index = static_cast<int>(reinterpret_cast<intptr_t>(Rwidget_get_user_handle(widget))) - 1;
    // Graphite is not a planet world: its chip is a plain accent dot.
    if (index == 19 || index == 20) {
        const float radius = std::min(width, height) * 0.3f;
        circle(painter, width * 0.5f, height * 0.5f, radius, rgb(index == 19 ? 0x4C9AFF : 0x0A6CDB));
        return;
    }
    static RSpring hover_springs[19];
    const float hover = index >= 0 && index < 19 ? Rdraw_spring(painter, &hover_springs[index], Rwidget_is_hovered(widget) ? 1.0f : 0.0f, R_MOTION_SMOOTH) : 0.0f;
    paint_scene(painter, index, 0.0f, 0.0f, width, height, static_cast<float>(time_), hover,
                planet_textures_[std::clamp(family_for_theme(index), 0, 4)], planet_shader_);
}

void ThemeGallery::paint_carousel(Rid widget, RPainter* painter, float width, float height) {
    width_ = width;
    height_ = height;
    static const int indices[5][3] = {
        {12, 13, 14}, {3, 8, 15}, {9, 4, 16}, {10, 5, 17}, {6, 11, 18}
    };
    struct Card {
        int family;
        float relative;
    };
    Card cards[5];
    for (int family = 0; family < 5; ++family) cards[family] = {family, wrapped_relative(family, position_)};
    std::sort(cards, cards + 5, [](const Card& left, const Card& right) {
        return std::abs(left.relative) > std::abs(right.relative);
    });
    float accent[4];
    float muted[4];
    float window[4];
    Rwidget_get_color(widget, R_COLOR_ACCENT, &accent[0], &accent[1], &accent[2], &accent[3]);
    Rwidget_get_color(widget, R_COLOR_TEXT_DISABLED, &muted[0], &muted[1], &muted[2], &muted[3]);
    Rwidget_get_color(widget, R_COLOR_WINDOW, &window[0], &window[1], &window[2], &window[3]);
    for (const auto& card : cards) {
        if (std::abs(card.relative) > 1.75f) continue;
        const auto rect = card_rect(width, height, card.relative);
        const float distance = std::abs(card.relative);
        Rdraw_push_clip_rect(painter, rect.x, rect.y, rect.width, rect.height);
        const int index = indices[card.family][finish_];
        paint_workspace(painter, index, palettes_[index], rect.x, rect.y, rect.width, rect.height,
                        static_cast<float>(time_), planet_textures_[std::clamp(family_for_theme(index), 0, 4)],
                        planet_shader_, !reduce_motion_);
        if (distance > 0.08f) {
            const bool hovered = (card.relative < 0.0f ? -1 : 1) == hovered_direction_;
            Rdraw_rect(painter, rect.x, rect.y, rect.width, rect.height, 4.0f,
                       window[0], window[1], window[2], hovered ? 0.34f : 0.52f);
        }
        const float selected = std::clamp(1.0f - distance, 0.0f, 1.0f);
        Rdraw_pop_clip_rect(painter);
        Rdraw_rect_outline(painter, rect.x, rect.y, rect.width, rect.height, 4.0f, 1.0f,
                           muted[0], muted[1], muted[2], 0.38f);
        if (selected > 0.0f) {
            Rdraw_rect_outline(painter, rect.x, rect.y, rect.width, rect.height, 4.0f, 1.5f,
                               accent[0], accent[1], accent[2], selected);
        }
    }
}

} // namespace raven
