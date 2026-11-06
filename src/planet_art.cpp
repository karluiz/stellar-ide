#include "planet_art.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace raven {
namespace {

constexpr int texture_size = 512;

struct Color {
    float r;
    float g;
    float b;
    float a;
};

float clamp(float value, float low = 0.0f, float high = 1.0f) {
    return std::clamp(value, low, high);
}

Color mix(Color a, Color b, float amount) {
    amount = clamp(amount);
    return {a.r + (b.r - a.r) * amount, a.g + (b.g - a.g) * amount,
            a.b + (b.b - a.b) * amount, a.a + (b.a - a.a) * amount};
}

Color scale(Color color, float amount) {
    return {color.r * amount, color.g * amount, color.b * amount, color.a};
}

std::uint32_t hash(int x, int y, int z) {
    std::uint32_t value = static_cast<std::uint32_t>(x) * 374761393u +
        static_cast<std::uint32_t>(y) * 668265263u + static_cast<std::uint32_t>(z) * 2246822519u;
    value = (value ^ (value >> 13)) * 1274126177u;
    return value ^ (value >> 16);
}

float random(int x, int y, int z) {
    return static_cast<float>(hash(x, y, z) & 0x00ffffffu) / static_cast<float>(0x01000000u);
}

float smooth(float value) {
    return value * value * (3.0f - 2.0f * value);
}

float noise(float x, float y, float z) {
    const int ix = static_cast<int>(std::floor(x));
    const int iy = static_cast<int>(std::floor(y));
    const int iz = static_cast<int>(std::floor(z));
    const float fx = smooth(x - static_cast<float>(ix));
    const float fy = smooth(y - static_cast<float>(iy));
    const float fz = smooth(z - static_cast<float>(iz));
    float result = 0.0f;
    for (int dz = 0; dz <= 1; ++dz) {
        for (int dy = 0; dy <= 1; ++dy) {
            for (int dx = 0; dx <= 1; ++dx) {
                const float weight = (dx ? fx : 1.0f - fx) * (dy ? fy : 1.0f - fy) * (dz ? fz : 1.0f - fz);
                result += random(ix + dx, iy + dy, iz + dz) * weight;
            }
        }
    }
    return result;
}

float fbm(float x, float y, float z, int octaves = 4) {
    float value = 0.0f;
    float amplitude = 0.55f;
    float normalization = 0.0f;
    for (int octave = 0; octave < octaves; ++octave) {
        value += noise(x, y, z) * amplitude;
        normalization += amplitude;
        x = x * 2.03f + 7.1f;
        y = y * 2.03f - 3.8f;
        z = z * 2.03f + 1.7f;
        amplitude *= 0.48f;
    }
    return value / normalization;
}

void over(float& dr, float& dg, float& db, float& da, Color source) {
    const float alpha = source.a + da * (1.0f - source.a);
    if (alpha <= 0.00001f) return;
    dr = (source.r * source.a + dr * da * (1.0f - source.a)) / alpha;
    dg = (source.g * source.a + dg * da * (1.0f - source.a)) / alpha;
    db = (source.b * source.a + db * da * (1.0f - source.a)) / alpha;
    da = alpha;
}

Color atmosphere(int family) {
    static const Color colors[] = {
        {0.28f, 0.72f, 0.95f, 1.0f}, {0.24f, 0.78f, 0.92f, 1.0f},
        {0.65f, 0.86f, 0.95f, 1.0f}, {0.91f, 0.65f, 0.35f, 1.0f},
        {0.98f, 0.65f, 0.25f, 1.0f}
    };
    return colors[family];
}

Color surface(int family, float x, float y, float z, float& specular) {
    const float broad = fbm(x * 2.1f + family * 9.3f, y * 2.1f - 2.7f, z * 2.1f + 4.2f);
    const float detail = fbm(x * 8.5f + 3.1f, y * 8.5f + family * 3.7f, z * 8.5f - 5.4f);
    specular = 0.0f;
    if (family == 0) {
        const float warp = fbm(x * 3.0f + 12.0f, y * 3.0f, z * 3.0f) - 0.5f;
        const float continents = broad + warp * 0.20f;
        const float land = clamp((continents - 0.535f) * 24.0f);
        Color ocean = mix({0.012f, 0.075f, 0.15f, 1.0f}, {0.025f, 0.25f, 0.43f, 1.0f}, detail);
        Color ground = mix({0.08f, 0.20f, 0.095f, 1.0f}, {0.38f, 0.43f, 0.20f, 1.0f}, detail);
        if (std::abs(y) > 0.68f) ground = mix(ground, {0.77f, 0.86f, 0.84f, 1.0f}, clamp((std::abs(y) - 0.68f) * 4.0f));
        Color color = mix(ocean, ground, land);
        const float cloud_field = fbm(x * 5.2f + 21.0f, y * 5.2f - 8.0f, z * 5.2f + 4.0f);
        const float clouds = clamp((cloud_field - 0.57f) * 5.5f) * (0.45f + detail * 0.35f);
        color = mix(color, {0.91f, 0.96f, 0.98f, 1.0f}, clouds);
        specular = (1.0f - land) * (1.0f - clouds) * 0.65f;
        return color;
    }
    if (family == 1) {
        const float current = fbm(x * 3.8f + 4.0f, y * 3.8f, z * 3.8f + 11.0f);
        const float wave = 0.5f + 0.5f * std::sin((y + current * 0.16f) * 74.0f + x * 13.0f);
        const float foam = clamp((wave * 0.58f + detail * 0.42f - 0.72f) * 6.0f);
        Color color = mix({0.008f, 0.10f, 0.18f, 1.0f}, {0.025f, 0.38f, 0.51f, 1.0f}, broad * 0.72f + wave * 0.28f);
        color = mix(color, {0.55f, 0.91f, 0.94f, 1.0f}, foam * 0.72f);
        specular = 0.95f;
        return color;
    }
    if (family == 2) {
        const float glacier = fbm(x * 4.6f - 7.0f, y * 4.6f + 3.0f, z * 4.6f);
        const float ridge = 1.0f - std::abs(2.0f * detail - 1.0f);
        const float cracks = clamp((ridge - 0.91f) * 9.0f);
        Color color = mix({0.31f, 0.49f, 0.57f, 1.0f}, {0.84f, 0.93f, 0.94f, 1.0f}, glacier * 0.78f + 0.18f);
        color = mix(color, {0.15f, 0.34f, 0.45f, 1.0f}, cracks * 0.58f);
        const float exposed = clamp((broad - 0.69f) * 7.0f);
        color = mix(color, {0.18f, 0.23f, 0.25f, 1.0f}, exposed * 0.7f);
        specular = 0.28f + glacier * 0.25f;
        return color;
    }
    const float dune = 0.5f + 0.5f * std::sin((y + broad * 0.22f) * 61.0f + x * 9.0f);
    const float canyon = std::pow(1.0f - std::abs(2.0f * detail - 1.0f), 7.0f);
    Color color = mix({0.20f, 0.105f, 0.045f, 1.0f}, {0.67f, 0.43f, 0.20f, 1.0f}, broad * 0.75f + dune * 0.25f);
    color = mix(color, {0.12f, 0.065f, 0.035f, 1.0f}, canyon * 0.48f);
    color = mix(color, {0.83f, 0.63f, 0.35f, 1.0f}, clamp((detail - 0.70f) * 2.5f) * 0.45f);
    specular = 0.06f;
    return color;
}

std::vector<std::uint8_t> planet_pixels(int family) {
    std::vector<std::uint8_t> pixels(texture_size * texture_size * 4);
    const Color air = atmosphere(family);
    for (int py = 0; py < texture_size; ++py) {
        for (int px = 0; px < texture_size; ++px) {
            const float canvas_x = (static_cast<float>(px) + 0.5f) / (texture_size * 0.5f) - 1.0f;
            const float canvas_y = (static_cast<float>(py) + 0.5f) / (texture_size * 0.5f) - 1.0f;
            const float x = canvas_x / 0.82f;
            const float y = canvas_y / 0.82f;
            const float radius2 = x * x + y * y;
            float r = 0.0f;
            float g = 0.0f;
            float b = 0.0f;
            float a = 0.0f;
            if (radius2 <= 1.0f) {
                const float z = std::sqrt(std::max(0.0f, 1.0f - radius2));
                float specular = 0.0f;
                Color color = surface(family, x, y, z, specular);
                const float diffuse = std::max(0.0f, -0.48f * x - 0.58f * y + 0.68f * z);
                const float light = 0.14f + 0.86f * std::pow(diffuse, 0.72f);
                color = scale(color, light);
                const float half_vector = std::max(0.0f, -0.267f * x - 0.323f * y + 0.908f * z);
                const float glint = std::pow(half_vector, 42.0f) * specular * 0.72f;
                const float rim = std::pow(1.0f - z, 3.2f) * (0.24f + diffuse * 0.76f);
                color.r += glint + air.r * rim * 0.36f;
                color.g += glint + air.g * rim * 0.36f;
                color.b += glint + air.b * rim * 0.36f;
                over(r, g, b, a, {clamp(color.r), clamp(color.g), clamp(color.b), 1.0f});
            } else {
                const float radius = std::sqrt(radius2);
                const float glow = clamp((1.14f - radius) / 0.14f) * 0.24f;
                if (glow > 0.0f) over(r, g, b, a, {air.r, air.g, air.b, glow});
            }
            const auto offset = (py * texture_size + px) * 4;
            pixels[offset] = static_cast<std::uint8_t>(clamp(r) * 255.0f + 0.5f);
            pixels[offset + 1] = static_cast<std::uint8_t>(clamp(g) * 255.0f + 0.5f);
            pixels[offset + 2] = static_cast<std::uint8_t>(clamp(b) * 255.0f + 0.5f);
            pixels[offset + 3] = static_cast<std::uint8_t>(clamp(a) * 255.0f + 0.5f);
        }
    }
    return pixels;
}

Color disk_color(float radius, float angle, float x) {
    const float heat = clamp(1.0f - radius);
    const float turbulence = 0.72f + 0.28f * std::sin(angle * 11.0f + radius * 93.0f) * std::sin(angle * 5.0f - radius * 47.0f);
    const float doppler = 0.62f + 0.55f * clamp(-x * 0.5f + 0.5f);
    Color color = mix({0.88f, 0.25f, 0.035f, 1.0f}, {1.0f, 0.93f, 0.66f, 1.0f}, std::pow(heat, 0.42f));
    return scale(color, turbulence * doppler);
}

std::vector<std::uint8_t> gargantua_pixels() {
    std::vector<std::uint8_t> pixels(texture_size * texture_size * 4);
    const float rotation = -0.18f;
    const float cs = std::cos(rotation);
    const float sn = std::sin(rotation);
    for (int py = 0; py < texture_size; ++py) {
        for (int px = 0; px < texture_size; ++px) {
            const float x = (static_cast<float>(px) + 0.5f) / (texture_size * 0.5f) - 1.0f;
            const float y = (static_cast<float>(py) + 0.5f) / (texture_size * 0.5f) - 1.0f;
            const float rx = x * cs - y * sn;
            const float ry = x * sn + y * cs;
            const float disk_radius = std::sqrt(rx * rx + (ry / 0.255f) * (ry / 0.255f));
            const float radius = std::sqrt(x * x + y * y);
            const float angle = std::atan2(ry / 0.255f, rx);
            float r = 0.0f;
            float g = 0.0f;
            float b = 0.0f;
            float a = 0.0f;
            if (disk_radius > 0.27f && disk_radius < 1.0f) {
                Color disk = disk_color(disk_radius, angle, rx);
                disk.a = clamp((1.0f - disk_radius) * 1.25f) * 0.72f;
                over(r, g, b, a, disk);
            }
            const float lens = std::exp(-std::pow((radius - 0.30f) / 0.024f, 2.0f)) * 0.64f;
            over(r, g, b, a, {1.0f, 0.69f, 0.30f, lens});
            if (radius < 0.235f) over(r, g, b, a, {0.0005f, 0.001f, 0.002f, 1.0f});
            const float photon = std::exp(-std::pow((radius - 0.245f) / 0.011f, 2.0f)) * 0.88f;
            over(r, g, b, a, {1.0f, 0.86f, 0.52f, photon});
            if (ry > 0.0f && disk_radius > 0.24f && disk_radius < 0.91f) {
                Color front = disk_color(disk_radius, angle, rx);
                front.a = clamp((0.91f - disk_radius) * 2.4f) * 0.90f;
                over(r, g, b, a, front);
            }
            const auto offset = (py * texture_size + px) * 4;
            pixels[offset] = static_cast<std::uint8_t>(clamp(r) * 255.0f + 0.5f);
            pixels[offset + 1] = static_cast<std::uint8_t>(clamp(g) * 255.0f + 0.5f);
            pixels[offset + 2] = static_cast<std::uint8_t>(clamp(b) * 255.0f + 0.5f);
            pixels[offset + 3] = static_cast<std::uint8_t>(clamp(a) * 255.0f + 0.5f);
        }
    }
    return pixels;
}

} // namespace

std::array<Rid, 5> create_planet_textures() {
    std::array<Rid, 5> textures{};
    for (int family = 0; family < 5; ++family) {
        auto pixels = family == 4 ? gargantua_pixels() : planet_pixels(family);
        textures[family] = Rtexture_create_from_pixels(pixels.data(), texture_size, texture_size);
    }
    return textures;
}

} // namespace raven
