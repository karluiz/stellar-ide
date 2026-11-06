#version 450
#extension GL_GOOGLE_include_directive : require
#include "raven_effect.glsl"

float hash3(vec3 p) {
    p = fract(p * 0.3183099 + vec3(0.11, 0.17, 0.23));
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float noise3(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    vec3 u = f * f * (3.0 - 2.0 * f);
    return mix(
        mix(mix(hash3(i), hash3(i + vec3(1, 0, 0)), u.x),
            mix(hash3(i + vec3(0, 1, 0)), hash3(i + vec3(1, 1, 0)), u.x), u.y),
        mix(mix(hash3(i + vec3(0, 0, 1)), hash3(i + vec3(1, 0, 1)), u.x),
            mix(hash3(i + vec3(0, 1, 1)), hash3(i + vec3(1, 1, 1)), u.x), u.y), u.z);
}

float fbm3(vec3 p) {
    float value = 0.0;
    float amplitude = 0.53;
    float total = 0.0;
    for (int i = 0; i < 5; ++i) {
        value += noise3(p) * amplitude;
        total += amplitude;
        p = p * 2.04 + vec3(5.7, -3.1, 8.3);
        amplitude *= 0.47;
    }
    return value / total;
}

vec3 rotate_y(vec3 p, float angle) {
    float c = cos(angle);
    float s = sin(angle);
    return vec3(c * p.x - s * p.z, p.y, s * p.x + c * p.z);
}

vec4 over(vec4 destination, vec4 source) {
    float alpha = source.a + destination.a * (1.0 - source.a);
    if (alpha < 0.00001) return vec4(0.0);
    return vec4((source.rgb * source.a + destination.rgb * destination.a * (1.0 - source.a)) / alpha, alpha);
}

vec3 atmosphere(int family) {
    if (family == 0) return vec3(0.24, 0.67, 0.96);
    if (family == 1) return vec3(0.20, 0.78, 0.94);
    if (family == 2) return vec3(0.66, 0.86, 0.96);
    return vec3(0.94, 0.61, 0.30);
}

vec3 surface(int family, vec3 p, float time, out float specular) {
    float broad = fbm3(p * 2.2 + vec3(family * 7.3, -1.7, 4.2));
    float detail = fbm3(p * 9.0 + vec3(3.1, family * 2.9, -5.4));
    specular = 0.0;
    if (family == 0) {
        float warp = fbm3(p * 3.4 + vec3(11.0, 2.0, -3.0)) - 0.5;
        float continents = broad + warp * 0.23;
        float land = smoothstep(0.545, 0.585, continents);
        vec3 ocean = mix(vec3(0.008, 0.055, 0.13), vec3(0.018, 0.23, 0.42), detail);
        vec3 ground = mix(vec3(0.055, 0.17, 0.075), vec3(0.35, 0.40, 0.18), detail);
        float ice = smoothstep(0.66, 0.86, abs(p.y) + detail * 0.08);
        ground = mix(ground, vec3(0.78, 0.87, 0.88), ice);
        vec3 color = mix(ocean, ground, land);
        vec3 cloud_p = rotate_y(p, time * 0.14) * 6.0 + vec3(17.0 + time * 0.055, -8.0, 4.0);
        float cloud_field = fbm3(cloud_p) + 0.055 * sin(p.y * 18.0 - time * 0.55);
        float clouds = smoothstep(0.56, 0.69, cloud_field) * 0.88;
        color *= 1.0 - clouds * 0.20;
        color = mix(color, vec3(0.93, 0.97, 1.0), clouds);
        specular = (1.0 - land) * (1.0 - clouds) * 0.85;
        return color;
    }
    if (family == 1) {
        float current = fbm3(p * 3.6 + vec3(4.0, time * 0.11, 11.0));
        float swell = 0.5 + 0.5 * sin((p.y + current * 0.28) * 28.0 + p.x * 4.0 - time * 1.65);
        float ripple = 0.5 + 0.5 * sin((p.x - current * 0.20) * 74.0 + p.y * 19.0 + time * 2.4);
        float giant = pow(0.5 + 0.5 * sin(p.y * 5.2 - time * 0.85 + current * 2.0), 10.0);
        float foam = smoothstep(0.69, 0.88, swell * 0.55 + ripple * 0.22 + detail * 0.23);
        foam += giant * smoothstep(0.48, 0.68, current) * 0.65;
        vec3 color = mix(vec3(0.004, 0.065, 0.14), vec3(0.018, 0.38, 0.52), broad * 0.45 + swell * 0.35 + giant * 0.20);
        color = mix(color, vec3(0.60, 0.94, 0.97), clamp(foam, 0.0, 1.0) * 0.74);
        specular = 0.80 + ripple * 0.20;
        return color;
    }
    if (family == 2) {
        float glacier = fbm3(p * 5.0 + vec3(-7.0, 3.0, time * 0.025));
        float ridge = 1.0 - abs(2.0 * detail - 1.0);
        float cracks = smoothstep(0.86, 0.97, ridge);
        vec3 color = mix(vec3(0.26, 0.43, 0.53), vec3(0.88, 0.95, 0.97), glacier * 0.80 + 0.16);
        color = mix(color, vec3(0.10, 0.28, 0.40), cracks * 0.58);
        float exposed = smoothstep(0.66, 0.77, broad);
        color = mix(color, vec3(0.16, 0.21, 0.24), exposed * 0.72);
        float weather = fbm3(p * 3.4 + vec3(time * 0.12, 2.0, -time * 0.07));
        float snow_clouds = smoothstep(0.53, 0.70, weather) * 0.32;
        color = mix(color, vec3(0.91, 0.97, 1.0), snow_clouds);
        specular = 0.34 + glacier * 0.30;
        return color;
    }
    float dune = 0.5 + 0.5 * sin((p.y + broad * 0.26) * 67.0 + p.x * 12.0);
    float canyon = pow(1.0 - abs(2.0 * detail - 1.0), 6.0);
    vec3 color = mix(vec3(0.16, 0.072, 0.030), vec3(0.66, 0.40, 0.17), broad * 0.72 + dune * 0.28);
    color = mix(color, vec3(0.095, 0.045, 0.022), canyon * 0.52);
    color = mix(color, vec3(0.85, 0.62, 0.32), smoothstep(0.65, 0.82, detail) * 0.42);
    float storm = fbm3(p * 4.2 + vec3(time * 0.16, 0.0, time * 0.06));
    float dust = smoothstep(0.52, 0.69, storm);
    color = mix(color, vec3(0.78, 0.42, 0.16), dust * 0.38);
    specular = 0.08;
    return color;
}

vec4 planet(RavenEffect fx, int family) {
    vec2 canvas = fx.uv * 2.0 - 1.0;
    vec2 p = canvas / 0.82;
    float radius = length(p);
    vec3 air = atmosphere(family);
    if (radius > 1.0) {
        float glow = exp(-(radius - 1.0) * 10.0) * 0.24;
        glow *= 0.88 + 0.12 * sin(fx.time * 0.9 + canvas.x * 4.0);
        return vec4(air, glow);
    }
    float z = sqrt(max(0.0, 1.0 - dot(p, p)));
    vec3 normal = vec3(p, z);
    float rotation = fx.time * (family == 1 ? 0.32 : family == 0 ? 0.22 : family == 2 ? 0.13 : 0.17);
    vec3 surface_p = rotate_y(normal, rotation);
    float specular = 0.0;
    vec3 color = surface(family, surface_p, fx.time, specular);
    vec3 light_dir = normalize(vec3(-0.52, -0.60, 0.72));
    float diffuse = max(0.0, dot(normal, light_dir));
    float light = 0.105 + 0.895 * pow(diffuse, 0.68);
    color *= light;
    vec3 half_dir = normalize(light_dir + vec3(0.0, 0.0, 1.0));
    float glint = pow(max(0.0, dot(normal, half_dir)), 58.0) * specular * 0.88;
    float rim = pow(1.0 - z, 3.0) * (0.20 + diffuse * 0.80);
    color += glint + air * rim * (0.38 + 0.04 * sin(fx.time * 0.9));
    if (family == 2) {
        float polar = 1.0 - smoothstep(-0.90, -0.05, p.y);
        float curtains = pow(0.5 + 0.5 * sin(p.x * 7.0 + p.y * 15.0 - fx.time * 1.1 + fbm3(normal * 3.0) * 3.0), 4.0);
        color += vec3(0.18, 1.0, 0.62) * curtains * polar * rim * 1.35;
    } else if (family == 3) {
        color += air * rim * 0.14 * (0.5 + 0.5 * sin(fx.time * 1.2 + p.x * 8.0));
    }
    float edge = 1.0 - smoothstep(0.995, 1.005, radius);
    return vec4(clamp(color, 0.0, 1.0), edge);
}

vec4 accretion(vec3 p, vec3 rd, float time) {
    float radius = length(p.xz);
    float angle = atan(p.z, p.x);
    float orbit = time * (0.42 + 0.38 / sqrt(max(radius, 0.1)));
    vec2 flow = vec2(cos(angle - orbit) * 3.5 + radius * 2.0,
                     sin(angle - orbit) * 3.5 + radius * 7.0);
    float clouds = raven_fbm(flow);
    float filaments = pow(0.5 + 0.5 * sin(angle * 18.0 + radius * 52.0 - orbit * 4.0 + clouds * 5.0), 2.4);
    float detail = raven_noise(vec2(angle * 12.0 - orbit * 2.0, radius * 70.0));
    float density = clamp(clouds * 0.46 + filaments * 0.40 + detail * 0.14, 0.0, 1.0);
    float inner = smoothstep(0.38, 0.50, radius);
    float outer = 1.0 - smoothstep(1.05, 2.05, radius);
    float alpha = inner * outer * (0.16 + density * 0.84) * 1.15;
    float heat = 1.0 - smoothstep(0.44, 1.90, radius);
    vec3 color = mix(vec3(1.0, 0.32, 0.06), vec3(1.0, 0.98, 0.87), pow(heat, 0.26));
    vec3 velocity = normalize(vec3(-p.z, 0.0, p.x));
    float doppler = clamp(0.74 - dot(velocity, rd) * 1.10, 0.20, 2.20);
    color *= doppler * (0.58 + density * 0.72);
    return vec4(clamp(color, 0.0, 1.0), alpha);
}

vec4 gargantua(RavenEffect fx) {
    vec2 screen = (fx.uv * 2.0 - 1.0) * 1.05;
    vec3 ro = vec3(0.0, 0.10 + sin(fx.time * 0.12) * 0.008, 3.4);
    vec3 forward = normalize(-ro);
    vec3 right = normalize(cross(vec3(0.0, 1.0, 0.0), forward));
    vec3 up = cross(forward, right);
    vec3 rd = normalize(forward * 1.75 + right * screen.x + up * screen.y);
    vec3 p = ro;
    float h2 = dot(cross(p, rd), cross(p, rd));
    float nearest = length(p);
    vec3 color = vec3(0.0);
    float alpha = 0.0;

    for (int i = 0; i < 64; ++i) {
        float radius = length(p);
        nearest = min(nearest, radius);
        if (radius < 0.16) {
            alpha = 1.0;
            break;
        }
        float step_size = clamp(radius * 0.075, 0.018, 0.10);
        vec3 gravity = -0.27 * h2 * p / pow(radius, 5.0);
        rd = normalize(rd + gravity * step_size);
        vec3 next = p + rd * step_size;
        float disk_radius = length(next.xz);
        if (abs(next.y) < 0.12 && disk_radius > 0.38 && disk_radius < 2.05 && alpha < 0.995) {
            vec4 matter = accretion(next, rd, fx.time);
            float core = exp(-pow(next.y / 0.026, 2.0));
            float halo = exp(-pow(next.y / 0.075, 2.0)) * 0.12;
            float grazing = 1.0 - smoothstep(0.025, 0.18, abs(rd.y));
            float optical_scale = mix(4.8, 1.6, grazing);
            float density = min(matter.a * step_size * optical_scale * (core + halo), 0.18);
            color += (1.0 - alpha) * matter.rgb * density;
            alpha += (1.0 - alpha) * density;
        }
        p = next;
        if (radius > 4.4 && dot(p, rd) > 0.0) break;
    }

    float photon = exp(-pow((nearest - 0.47) / 0.025, 2.0)) * 0.20;
    float glow = photon * (1.0 - alpha);
    color += vec3(1.0, 0.88, 0.68) * glow;
    alpha += glow;
    float edge = 1.0 - smoothstep(0.93, 1.04, length(screen));
    return vec4(clamp(color / max(alpha, 0.0001), 0.0, 1.0), clamp(alpha * edge, 0.0, 1.0));
}

vec4 raven_effect(RavenEffect fx) {
    int family = int(fx.params.x + 0.5);
    if (family == 4) return gargantua(fx);
    return planet(fx, family);
}
