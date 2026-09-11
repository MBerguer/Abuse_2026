#ifndef REMASTER_SHADERS_H
#define REMASTER_SHADERS_H

namespace RemasterShaders
{

// Fullscreen Quad Vertex Shader
static const char *quad_vs = R"(#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoords;

out vec2 TexCoords;
uniform float u_flip_y;

void main()
{
    TexCoords = aTexCoords;
    float y = (u_flip_y > 0.5) ? -aPos.y : aPos.y;
    gl_Position = vec4(aPos.x, y, 0.0, 1.0);
}
)";

// Classic Framebuffer Blit (with optional CRT filter)
static const char *classic_fs = R"(#version 330 core
in vec2 TexCoords;
out vec4 FragColor;

uniform sampler2D u_screen;
uniform int u_crt_enabled;

void main()
{
    vec2 uv = TexCoords;
    if (u_crt_enabled == 1)
    {
        vec2 cc = uv - 0.5;
        float dist = dot(cc, cc);
        uv = uv + cc * (dist * 0.12);
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
        {
            FragColor = vec4(0.0, 0.0, 0.0, 1.0);
            return;
        }
    }

    vec4 col = texture(u_screen, uv);

    if (u_crt_enabled == 1)
    {
        float scanline = sin(uv.y * 640.0) * 0.08;
        col.rgb -= scanline;
        // subtle vignette
        float vig = 16.0 * uv.x * uv.y * (1.0 - uv.x) * (1.0 - uv.y);
        col.rgb *= clamp(pow(vig, 0.15), 0.0, 1.0);
    }

    FragColor = col;
}
)";

// G-Buffer Generation: Separates Albedo, Generates Normals (Sobel filter), Extracts Emission & Occlusion
static const char *gbuffer_fs = R"(#version 330 core
in vec2 TexCoords;
layout (location = 0) out vec4 gAlbedo;
layout (location = 1) out vec4 gNormal;
layout (location = 2) out vec4 gEmission;
layout (location = 3) out vec4 gOcclusion;

uniform sampler2D u_scene;
uniform vec2 u_texel_size;
uniform float u_normal_strength;
uniform vec4 u_ui_rect;

void main()
{
    vec4 col = texture(u_scene, TexCoords);
    gAlbedo = col;

    bool is_ui = (TexCoords.x >= u_ui_rect.x && TexCoords.x <= u_ui_rect.z &&
                  TexCoords.y >= u_ui_rect.y && TexCoords.y <= u_ui_rect.w);

    if (is_ui)
    {
        // UI Layer (Higher Z-Index): flat surface normal, never occludes or casts shadows into world
        gNormal = vec4(0.5, 0.5, 1.0, 1.0);
        gOcclusion = vec4(0.0, 0.0, 0.0, 1.0);

        // Subtle digital LED emission for green HUD readouts (health & ammo)
        if (col.g > 0.35 && col.r < 0.25 && col.b < 0.25)
        {
            gEmission = vec4(col.rgb * 1.5, 1.0);
        }
        else
        {
            gEmission = vec4(0.0, 0.0, 0.0, 1.0);
        }
        return;
    }

    // Normal generation via Sobel operator on luminance
    float l = dot(texture(u_scene, TexCoords - vec2(u_texel_size.x, 0.0)).rgb, vec3(0.299, 0.587, 0.114));
    float r = dot(texture(u_scene, TexCoords + vec2(u_texel_size.x, 0.0)).rgb, vec3(0.299, 0.587, 0.114));
    float d = dot(texture(u_scene, TexCoords - vec2(0.0, u_texel_size.y)).rgb, vec3(0.299, 0.587, 0.114));
    float u = dot(texture(u_scene, TexCoords + vec2(0.0, u_texel_size.y)).rgb, vec3(0.299, 0.587, 0.114));

    float dx = (l - r) * u_normal_strength;
    float dy = (d - u) * u_normal_strength;
    vec3 n = normalize(vec3(dx, dy, 1.0));
    gNormal = vec4(n * 0.5 + 0.5, 1.0);

    // Emission detection: Lasers, plasma, computer screens, sparks, fire
    float lum = dot(col.rgb, vec3(0.2126, 0.7152, 0.0722));
    bool isLaser = (col.r > 0.65 && col.g < 0.35 && col.b < 0.35);
    bool isPlasma = (col.g > 0.65 && col.r < 0.4);
    bool isElectric = (col.b > 0.65 && col.g > 0.5);
    bool isHot = (col.r > 0.8 && col.g > 0.6 && col.b < 0.4);

    if (lum > 0.85 || isLaser || isPlasma || isElectric || isHot)
    {
        gEmission = vec4(col.rgb * 1.8, 1.0);
    }
    else
    {
        gEmission = vec4(0.0, 0.0, 0.0, 1.0);
    }

    // Occlusion map for 2D Ray Tracing: solid dense geometry casts shadows
    float occ = (lum < 0.03 && col.a > 0.8) ? 1.0 : 0.0;
    gOcclusion = vec4(vec3(occ), 1.0);
}
)";

// 2D Ray Tracing & Dynamic Lighting Shader (Raymarched Soft Shadows + Normal Mapping + PBR Specular)
static const char *raytracing_lighting_fs = R"(#version 330 core
in vec2 TexCoords;
out vec4 FragColor;

uniform sampler2D u_albedo;
uniform sampler2D u_normal;
uniform sampler2D u_emission;
uniform sampler2D u_occlusion;

struct Light {
    vec3 position; // x, y in [0, 1], z is virtual height above 2D plane
    vec3 color;
    float radius;
    float intensity;
    int is_spot;
    vec2 spot_dir;
    float spot_cutoff;
};

const int MAX_LIGHTS = 32;
uniform int u_num_lights;
uniform Light u_lights[MAX_LIGHTS];
uniform vec3 u_ambient_color;
uniform int u_raytracing_enabled;
uniform int u_soft_shadows;
uniform int u_shadow_quality;
uniform float u_light_intensity;
uniform vec4 u_ui_rect;

float trace_shadow(vec2 frag_pos, vec2 light_pos, float light_radius)
{
    if (u_raytracing_enabled == 0) return 1.0;

    vec2 ray = light_pos - frag_pos;
    float dist = length(ray);
    if (dist < 0.001) return 1.0;
    vec2 dir = ray / dist;

    int steps = (u_shadow_quality == 2) ? 28 : (u_shadow_quality == 1 ? 16 : 8);
    float step_size = dist / float(steps);
    float shadow = 1.0;

    // Start from step 2 to avoid self-shadowing at the surface
    for (int i = 2; i < steps; i++)
    {
        vec2 sample_pos = frag_pos + dir * (float(i) * step_size);
        float occ = texture(u_occlusion, sample_pos).r;
        if (occ > 0.5)
        {
            if (u_soft_shadows == 1)
            {
                // Soft shadow penumbra approximation based on distance
                float current_dist = float(i) * step_size;
                float penumbra = (dist - current_dist) / dist;
                shadow = min(shadow, penumbra * 0.55);
                if (shadow <= 0.05) return 0.0;
            }
            else
            {
                return 0.0;
            }
        }
    }
    return clamp(shadow, 0.0, 1.0);
}

uniform int u_volumetric_enabled;

void main()
{
    vec4 albedo = texture(u_albedo, TexCoords);

    bool is_ui = (TexCoords.x >= u_ui_rect.x && TexCoords.x <= u_ui_rect.z &&
                  TexCoords.y >= u_ui_rect.y && TexCoords.y <= u_ui_rect.w);

    if (is_ui)
    {
        // UI layer has a higher Z-Index: drawn on top of the 2D world.
        // It is unaffected by ambient darkness, shadow casting, or world lights.
        vec3 col = albedo.rgb;
        if (col.g > 0.35 && col.r < 0.25 && col.b < 0.25)
        {
            col = min(col * 1.3, vec3(1.0)); // Crisp glowing digital LED readout
        }
        FragColor = vec4(col, 1.0);
        return;
    }

    vec3 normal_raw = texture(u_normal, TexCoords).rgb * 2.0 - 1.0;
    vec3 normal = normalize(normal_raw);
    vec3 emission = texture(u_emission, TexCoords).rgb;

    vec3 total_diffuse = u_ambient_color;
    vec3 total_specular = vec3(0.0);
    vec3 total_volumetric = vec3(0.0);

    for (int i = 0; i < u_num_lights; i++)
    {
        Light light = u_lights[i];
        vec2 light_screen = light.position.xy;
        vec2 dir_2d = light_screen - TexCoords;
        float dist_2d = length(dir_2d);

        if (dist_2d > light.radius) continue;

        // Spot light check (e.g. player aim cone / flashlight)
        float spot_factor = 1.0;
        if (light.is_spot == 1)
        {
            vec2 to_pixel = normalize(TexCoords - light_screen);
            float angle_cos = dot(to_pixel, normalize(light.spot_dir));
            if (angle_cos < light.spot_cutoff)
            {
                spot_factor = smoothstep(light.spot_cutoff - 0.1, light.spot_cutoff, angle_cos);
            }
        }

        // Distance attenuation
        float atten = clamp(1.0 - (dist_2d / light.radius), 0.0, 1.0);
        atten = atten * atten * (3.0 - 2.0 * atten); // smooth falloff

        // Raymarched soft shadow
        float shadow = trace_shadow(TexCoords, light_screen, light.radius);

        // Volumetric in-scattering through atmosphere
        if (u_volumetric_enabled == 1 && shadow > 0.01)
        {
            total_volumetric += light.color * light.intensity * atten * spot_factor * shadow * 0.18;
        }

        if (shadow <= 0.001) continue;

        // 3D Direction to light
        vec3 light_dir = normalize(vec3(dir_2d, light.position.z));
        float diff = max(dot(normal, light_dir), 0.0);

        // Specular (Blinn-Phong)
        vec3 view_dir = vec3(0.0, 0.0, 1.0);
        vec3 half_dir = normalize(light_dir + view_dir);
        float spec = pow(max(dot(normal, half_dir), 0.0), 16.0);

        vec3 light_contrib = light.color * light.intensity * atten * shadow * spot_factor * u_light_intensity;
        total_diffuse += light_contrib * diff;
        total_specular += light_contrib * spec * 0.4;
    }

    vec3 final_color = albedo.rgb * total_diffuse + total_specular + emission + total_volumetric;
    FragColor = vec4(final_color, albedo.a);
}
)";

// Kawase / Gaussian Blur Pass for Bloom & Volumetrics
static const char *blur_fs = R"(#version 330 core
in vec2 TexCoords;
out vec4 FragColor;

uniform sampler2D u_image;
uniform vec2 u_offset;

void main()
{
    vec4 sum = vec4(0.0);
    sum += texture(u_image, TexCoords) * 0.227027;
    sum += texture(u_image, TexCoords + u_offset * 1.384615) * 0.316216;
    sum += texture(u_image, TexCoords - u_offset * 1.384615) * 0.316216;
    sum += texture(u_image, TexCoords + u_offset * 3.230769) * 0.070270;
    sum += texture(u_image, TexCoords - u_offset * 3.230769) * 0.070270;
    FragColor = sum;
}
)";

// Final Composite Shader (Bloom blending + Screen-Space Floor Reflections + ACES Tonemapping)
static const char *composite_fs = R"(#version 330 core
in vec2 TexCoords;
out vec4 FragColor;

uniform sampler2D u_lit_scene;
uniform sampler2D u_bloom;
uniform sampler2D u_albedo;
uniform float u_bloom_intensity;
uniform int u_bloom_enabled;
uniform int u_reflections_enabled;
uniform float u_time;
uniform vec4 u_ui_rect;

// ACES Filmic Tonemapping
vec3 aces_tonemap(vec3 x)
{
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main()
{
    vec4 base = texture(u_lit_scene, TexCoords);
    vec3 color = base.rgb;

    bool is_ui = (TexCoords.x >= u_ui_rect.x && TexCoords.x <= u_ui_rect.z &&
                  TexCoords.y >= u_ui_rect.y && TexCoords.y <= u_ui_rect.w);

    // Floor / Puddle reflections (screen-space reflection) - game world only
    if (u_reflections_enabled == 1 && !is_ui)
    {
        // Detect wet metal / water surfaces (bottom half, reflective color)
        if (TexCoords.y > 0.6)
        {
            float ripple = sin(TexCoords.x * 40.0 + u_time * 3.0) * 0.003;
            vec2 refl_uv = vec2(TexCoords.x + ripple, TexCoords.y - (TexCoords.y - 0.6) * 0.5);
            vec3 reflected = texture(u_lit_scene, refl_uv).rgb;
            color = mix(color, color + reflected * 0.35, 0.25);
        }
    }

    // Bloom combination
    if (u_bloom_enabled == 1)
    {
        vec3 bloom = texture(u_bloom, TexCoords).rgb;
        color += bloom * u_bloom_intensity;
    }

    // Tonemapping & color grading
    color = aces_tonemap(color);

    // Subtle atmospheric vignette (applied to the game world, leaving the UI layer crisp and bright)
    if (!is_ui)
    {
        float vig = 16.0 * TexCoords.x * TexCoords.y * (1.0 - TexCoords.x) * (1.0 - TexCoords.y);
        color *= clamp(pow(vig, 0.08), 0.0, 1.0);
    }

    FragColor = vec4(color, base.a);
}
)";

// Remaster HUD & In-game Settings Overlay FS
static const char *hud_overlay_fs = R"(#version 330 core
in vec2 TexCoords;
out vec4 FragColor;

uniform sampler2D u_hud_texture;

void main()
{
    FragColor = texture(u_hud_texture, TexCoords);
}
)";

} // namespace RemasterShaders

#endif // REMASTER_SHADERS_H
