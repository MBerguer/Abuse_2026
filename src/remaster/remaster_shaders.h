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

// G-Buffer Generation: Separates Albedo, Generates Normals (Sobel/Scharr filter), Extracts Emission & Occlusion
static const char *gbuffer_fs = R"(#version 330 core
in vec2 TexCoords;
layout (location = 0) out vec4 gAlbedo;
layout (location = 1) out vec4 gNormal;
layout (location = 2) out vec4 gEmission;
layout (location = 3) out vec4 gOcclusion;

uniform sampler2D u_scene;
uniform vec2 u_src_size;
uniform vec2 u_fbo_size;
uniform float u_normal_strength;
uniform int u_hd_scaler;
uniform sampler2DArray u_ai_materials;
uniform int u_has_ai_materials;
uniform vec2 u_cam_pos;
uniform sampler2D u_sprite_mask;
uniform isampler2D u_tile_grid;
uniform vec4 u_grid_bounds;
uniform vec2 u_tile_size;

const int MAX_UI_RECTS = 16;
uniform int u_num_ui_rects;
uniform vec4 u_ui_rects[MAX_UI_RECTS];

bool is_in_ui(vec2 uv)
{
    for (int i = 0; i < u_num_ui_rects; i++)
    {
        if (uv.x >= u_ui_rects[i].x && uv.x <= u_ui_rects[i].z &&
            uv.y >= u_ui_rects[i].y && uv.y <= u_ui_rects[i].w)
        {
            return true;
        }
    }
    return false;
}

// Perceptual color difference in YUV space
float xbr_df(vec4 c1, vec4 c2)
{
    vec3 yuv_w = vec3(0.299, 0.587, 0.114);
    float y1 = dot(c1.rgb, yuv_w);
    float y2 = dot(c2.rgb, yuv_w);
    float u1 = -0.14713 * c1.r - 0.28886 * c1.g + 0.436 * c1.b;
    float u2 = -0.14713 * c2.r - 0.28886 * c2.g + 0.436 * c2.b;
    float v1 = 0.615 * c1.r - 0.51499 * c1.g - 0.10001 * c1.b;
    float v2 = 0.615 * c2.r - 0.51499 * c2.g - 0.10001 * c2.b;
    vec3 diff = vec3((y1 - y2) * 4.0, u1 - u2, v1 - v2);
    float da = abs(c1.a - c2.a) * 2.0;
    return sqrt(dot(diff, diff) + da * da);
}

vec4 get_texel(vec2 base_pos, vec2 offset)
{
    vec2 sample_coord = (base_pos + offset + 0.5) / u_src_size;
    sample_coord = clamp(sample_coord, vec2(0.5) / u_src_size, vec2(1.0) - vec2(0.5) / u_src_size);
    return texture(u_scene, sample_coord);
}

// xBR Real-Time Edge-Directed High-Definition Reconstruction
vec4 sample_xbr_hd(vec2 uv)
{
    vec2 pos = uv * u_src_size;
    vec2 base = floor(pos);
    vec2 sub = fract(pos); // subpixel within texel [0, 1]

    // Determine quadrant direction and quadrant-relative subtexel coordinates
    vec2 dir = vec2(sub.x >= 0.5 ? 1.0 : -1.0, sub.y >= 0.5 ? 1.0 : -1.0);
    vec2 q = abs((sub - 0.5) * 2.0); // [0, 1] from center to corner

    vec4 ce = get_texel(base, vec2(0.0, 0.0));
    vec4 cf = get_texel(base, vec2(dir.x, 0.0));
    vec4 ch = get_texel(base, vec2(0.0, dir.y));
    vec4 ci = get_texel(base, vec2(dir.x, dir.y));

    vec4 cc = get_texel(base, vec2(dir.x, -dir.y));
    vec4 cg = get_texel(base, vec2(-dir.x, dir.y));
    vec4 cb = get_texel(base, vec2(0.0, -dir.y));
    vec4 cd = get_texel(base, vec2(-dir.x, 0.0));

    vec4 cf4 = get_texel(base, vec2(2.0 * dir.x, 0.0));
    vec4 ch5 = get_texel(base, vec2(0.0, 2.0 * dir.y));
    vec4 ci4 = get_texel(base, vec2(2.0 * dir.x, dir.y));
    vec4 ci5 = get_texel(base, vec2(dir.x, 2.0 * dir.y));

    // xBR Weight Calculation: Compares diagonal edge vs perpendicular edge
    float w_edge = xbr_df(ce, cc) + xbr_df(ce, cg) + xbr_df(ci, cf4) + xbr_df(ci, ch5) + 4.0 * xbr_df(ch, cf);
    float w_opp  = xbr_df(ch, cd) + xbr_df(ch, ci4) + xbr_df(cf, cb) + xbr_df(cf, ci5) + 4.0 * xbr_df(ce, ci);
    float contrast = xbr_df(ce, ci);

    // Only apply xBR corner slicing on REAL structural edges (contrast > 0.12)
    // Avoids turning subtle retro dither noise into weird watercolor slugs
    if (w_edge < w_opp && contrast > 0.12)
    {
        // Edge detected slicing this corner!
        vec4 c_edge = (q.x > q.y) ? cf : ch;
        if (xbr_df(ch, cf) < 0.18)
        {
            c_edge = mix(ch, cf, 0.5);
        }

        // 45-degree diagonal distance
        float d45 = (q.x + q.y - 1.0) / 1.41421356;

        // Extended slopes (steep & shallow)
        bool is_shallow = xbr_df(ce, cc) < xbr_df(ce, cg);
        float d_shallow = (2.0 * q.x + q.y - 2.0) / 2.23606797;

        bool is_steep = xbr_df(ce, cg) < xbr_df(ce, cc);
        float d_steep = (q.x + 2.0 * q.y - 2.0) / 2.23606797;

        float dist = d45;
        if (is_shallow && d_shallow > dist) dist = max(dist, d_shallow);
        if (is_steep && d_steep > dist) dist = max(dist, d_steep);

        // Sub-pixel anti-aliasing width tailored to HD resolution
        float aa_width = length(fwidth(q)) * 0.75;
        aa_width = clamp(aa_width, 0.04, 0.20);

        float blend = smoothstep(-aa_width, aa_width, dist);
        return mix(ce, c_edge, blend);
    }

    // Smart De-Dithering & Smooth Surface Reconstruction:
    // Completely dissolves 8-bit checkerboard dither patterns and pixel blockiness into clean, smooth sci-fi materials
    float wf = exp(-xbr_df(ce, cf) * 8.0);
    float wh = exp(-xbr_df(ce, ch) * 8.0);
    float wb = exp(-xbr_df(ce, cb) * 8.0);
    float wd = exp(-xbr_df(ce, cd) * 8.0);
    vec4 bilateral = (ce * 2.0 + cf * wf + ch * wh + cb * wb + cd * wd) / (2.0 + wf + wh + wb + wd);

    // Hermite sub-pixel smoothing across quadrant
    vec2 smooth_q = q * q * (3.0 - 2.0 * q);
    vec4 quad_lerp = mix(ce, (q.x > q.y ? cf : ch), smooth_q.x * 0.25 + smooth_q.y * 0.25);

    return mix(bilateral, quad_lerp, 0.35);
}

vec4 sample_pixel(vec2 uv)
{
    if (u_hd_scaler == 1)
        return sample_xbr_hd(uv);
    else
        return texture(u_scene, uv);
}

void main()
{
    vec4 col = sample_pixel(TexCoords);

    if (is_in_ui(TexCoords))
    {
        gAlbedo = col;
        // UI Layer (Higher Z-Index): flat surface normal, never occludes or casts shadows into world
        gNormal = vec4(0.5, 0.5, 1.0, 1.0);
        gOcclusion = vec4(0.0, 0.0, 0.0, 1.0);

        // Subtle digital LED emission specifically for bottom HUD readouts (health & ammo)
        if (TexCoords.y > 0.70 && col.g > 0.35 && col.r < 0.25 && col.b < 0.25)
        {
            gEmission = vec4(col.rgb * 1.5, 1.0);
        }
        else
        {
            gEmission = vec4(0.0, 0.0, 0.0, 1.0);
        }
        return;
    }

    // High-resolution Normal generation:
    vec2 step_offset = (u_hd_scaler == 1) ? (vec2(1.0) / u_fbo_size) : (vec2(1.0) / u_src_size);
    float l = dot(sample_pixel(TexCoords - vec2(step_offset.x, 0.0)).rgb, vec3(0.299, 0.587, 0.114));
    float r = dot(sample_pixel(TexCoords + vec2(step_offset.x, 0.0)).rgb, vec3(0.299, 0.587, 0.114));
    float d = dot(sample_pixel(TexCoords - vec2(0.0, step_offset.y)).rgb, vec3(0.299, 0.587, 0.114));
    float u = dot(sample_pixel(TexCoords + vec2(0.0, step_offset.y)).rgb, vec3(0.299, 0.587, 0.114));

    float dx = (l - r) * (u_normal_strength * (u_hd_scaler == 1 ? 2.5 : 1.0));
    float dy = (d - u) * (u_normal_strength * (u_hd_scaler == 1 ? 2.5 : 1.0));

    // Material detection: metallic, roughness, and water/liquid for PBR lighting & reflections
    float lum = dot(col.rgb, vec3(0.2126, 0.7152, 0.0722));
    float max_c = max(max(col.r, col.g), col.b);
    float min_c = min(min(col.r, col.g), col.b);
    float sat = (max_c > 0.01) ? (max_c - min_c) / max_c : 0.0;

    // Metallic & Industrial Alloy surfaces:
    // 1. Neutral bare steel / silver / titanium / machinery (low saturation, mid-to-high lum)
    bool isBareMetal = (sat < 0.35 && lum > 0.08);
    // 2. Painted military alloy / green industrial steel panels (olive drab / green modular plates)
    bool isPaintedSteel = (col.g > col.r * 0.82 && col.g > col.b && lum > 0.08 && lum < 0.70);
    // 3. Bronze / copper conduits / rusty industrial rails
    bool isAlloy = (col.r > col.b * 1.15 && lum > 0.10 && lum < 0.65);

    float metallic = 0.0;
    if (isBareMetal) metallic = clamp((1.0 - sat * 1.5) * smoothstep(0.10, 0.40, lum), 0.35, 0.95);
    else if (isPaintedSteel) metallic = 0.68;
    else if (isAlloy) metallic = 0.55;

    float roughness = clamp(0.18 + sat * 0.45 + (1.0 - lum) * 0.30, 0.15, 0.85);
    if (isPaintedSteel) roughness = 0.28;
    if (isBareMetal) roughness = 0.20;

    // Liquid & Wet Surface detection:
    bool isAcid = (col.g > 0.35 && col.r < 0.28 && col.b < 0.30);
    bool isWater = (col.b > 0.16 && col.b >= col.r && col.g >= col.r * 0.7 && lum < 0.45);
    bool isWetFloor = (metallic > 0.35 && lum < 0.30);

    float water = 0.0;
    if (isAcid)
    {
        water = 1.0;
        roughness = 0.04;
        metallic = 0.10;
    }
    else if (isWater)
    {
        water = 0.90;
        roughness = 0.05;
        metallic = 0.08;
    }
    else if (isWetFloor)
    {
        water = 0.50;
        roughness = 0.12;
    }

    // Dynamic sprite vs static environment mask
    float is_sprite = texture(u_sprite_mask, TexCoords).r;

    // High-Definition AI Master Material Synthesis (active when HD Remaster is ON)
    if (u_hd_scaler == 1 && u_has_ai_materials == 1 && is_sprite < 0.5)
    {
        vec2 world_pos = TexCoords * u_src_size + u_cam_pos;
        float layer = 0.0;
        vec2 uv_mat = fract(world_pos / 96.0);

        // Fetch exact tile information from the 2D Ray Tracing Grid (4 channels)
        vec2 tile_coord = floor(world_pos / u_tile_size);
        vec2 grid_coord = tile_coord - u_grid_bounds.xy;
        
        int tile_id_fg  = 0;
        int tile_id_bg  = 0;
        int tile_type   = 0;
        int floor_y_ref = 0;
        
        if (grid_coord.x >= 0.0 && grid_coord.x < u_grid_bounds.z &&
            grid_coord.y >= 0.0 && grid_coord.y < u_grid_bounds.w)
        {
            ivec2 itex_coord = ivec2(grid_coord);
            ivec4 tile_data = texelFetch(u_tile_grid, itex_coord, 0);
            tile_id_fg  = tile_data.r;
            tile_id_bg  = tile_data.g;
            tile_type   = tile_data.b;
            floor_y_ref = tile_data.a;
        }

        vec2 tile_local = mod(world_pos, u_tile_size);

        // Sub-tile surface distance & edge determination
        float is_floor_edge = 0.0;
        float is_floor_crease = 0.0;
        float is_in_ramp_tread = 0.0;

        // Tile 84 right cutoff (catwalk ends and pillar begins)
        if (tile_id_fg == 84 && tile_local.x > 6.0)
        {
            tile_type = 2; // Right vertical pillar
        }

        if (tile_type == 4)
        {
            // Walkable 45-degree slope (Ramp down-right, tile 18)
            float ramp_y = tile_local.x * (14.0 / 29.0);
            float dist = tile_local.y - ramp_y;
            if (dist < -0.8)
            {
                // Above ramp: air in alcove, showing room mesh wall behind
                layer = 2.0;
                uv_mat = fract(world_pos / 48.0);
            }
            else
            {
                // Ramp body
                layer = 3.0; // Diamond tread catwalk
                uv_mat = fract(world_pos / 48.0);
                
                // Physical crisp dividing line along the slope:
                float edge_dist = abs(dist);
                if (edge_dist <= 1.1)
                {
                    is_floor_edge = 1.0 - (edge_dist / 1.1);
                }
                else if (dist > 1.1 && dist <= 2.4)
                {
                    is_floor_crease = 1.0 - abs(dist - 1.75) / 0.65;
                }
                else if (dist > 2.4)
                {
                    is_in_ramp_tread = 1.0;
                }
            }
        }
        else if (tile_type == 5)
        {
            // Walkable slope down-left (tile 19)
            float ramp_y = (29.0 - tile_local.x) * (14.0 / 29.0);
            float dist = tile_local.y - ramp_y;
            if (dist < -0.8)
            {
                layer = 2.0;
                uv_mat = fract(world_pos / 48.0);
            }
            else
            {
                layer = 3.0;
                uv_mat = fract(world_pos / 48.0);
                float edge_dist = abs(dist);
                if (edge_dist <= 1.1)
                {
                    is_floor_edge = 1.0 - (edge_dist / 1.1);
                }
                else if (dist > 1.1 && dist <= 2.4)
                {
                    is_floor_crease = 1.0 - abs(dist - 1.75) / 0.65;
                }
                else if (dist > 2.4)
                {
                    is_in_ramp_tread = 1.0;
                }
            }
        }
        else if (tile_type == 3)
        {
            // Walkable flat catwalk platform (tiles 82, 83, 84)
            float plat_y = float(floor_y_ref);
            float dist = tile_local.y - plat_y;
            if (dist < -0.8)
            {
                layer = 2.0; // Air above catwalk: alcove mesh behind
                uv_mat = fract(world_pos / 48.0);
            }
            else
            {
                layer = 3.0; // Catwalk plate
                uv_mat = fract(world_pos / 48.0);
                float edge_dist = abs(dist);
                if (edge_dist <= 1.1)
                {
                    is_floor_edge = 1.0 - (edge_dist / 1.1);
                }
                else if (dist > 1.1 && dist <= 2.4)
                {
                    is_floor_crease = 1.0 - abs(dist - 1.75) / 0.65;
                }
            }
        }
        else if (tile_type == 2)
        {
            // Vertical framing columns / pillars (tiles 10, 11)
            layer = 0.0;
            uv_mat = fract(world_pos / 64.0);
        }
        else if (tile_type == 1)
        {
            // Interior room wall (ROJO) - Perforated mesh screen & warning panels
            if (tile_id_fg == 412 || (col.g > 0.38 && col.b > 0.38 && col.r < 0.45) || (col.g > 0.45 && col.r < 0.30))
            {
                layer = 1.0; // Tech / warning panel
                uv_mat = fract(world_pos / 64.0);
            }
            else
            {
                layer = 2.0; // Radiator / perforated metal mesh
                uv_mat = fract(world_pos / 48.0);
            }
        }
        else if (tile_type == 6)
        {
            // Under-ramp foundation / machinery chassis
            layer = 0.0;
            uv_mat = fract(world_pos / 64.0);
        }
        else
        {
            // Deep inaccessible background (AZUL)
            layer = 4.0;
            uv_mat = fract(world_pos / 128.0);
        }

        vec4 ai_samp = texture(u_ai_materials, vec3(uv_mat, layer));
        vec3 ai_rgb = ai_samp.rgb;

        // High-resolution Normal gradient from AI material
        vec2 eps = vec2(1.5 / 1024.0);
        float l_ai = dot(texture(u_ai_materials, vec3(uv_mat - vec2(eps.x, 0.0), layer)).rgb, vec3(0.299, 0.587, 0.114));
        float r_ai = dot(texture(u_ai_materials, vec3(uv_mat + vec2(eps.x, 0.0), layer)).rgb, vec3(0.299, 0.587, 0.114));
        float d_ai = dot(texture(u_ai_materials, vec3(uv_mat - vec2(0.0, eps.y), layer)).rgb, vec3(0.299, 0.587, 0.114));
        float u_ai = dot(texture(u_ai_materials, vec3(uv_mat + vec2(0.0, eps.y), layer)).rgb, vec3(0.299, 0.587, 0.114));

        dx += (l_ai - r_ai) * 2.2;
        dy += (d_ai - u_ai) * 2.2;

        if (layer == 3.0)
        {
            // VERDE: Walkable Floor & Ramp Surface
            // Blend non-slip diamond catwalk tread
            col.rgb = mix(col.rgb, col.rgb * (ai_rgb * 1.55), 0.45);
            metallic = 0.88;
            roughness = 0.20;

            // Crisp physical floor division line (as user requested)
            if (is_floor_edge > 0.01)
            {
                vec3 edge_spec = vec3(0.85, 0.90, 0.96); // Clean brushed metal chamfer rim
                col.rgb = mix(col.rgb * 1.45 + vec3(0.12, 0.15, 0.18), edge_spec, is_floor_edge * 0.70);
                metallic = 0.98;
                roughness = 0.05;
                dy -= 3.5 * is_floor_edge; // Upward facing chamfer reflection
            }
            else if (is_floor_crease > 0.01)
            {
                // Shadow crease directly under the edge lip (creates 3D physical shelf)
                col.rgb *= (1.0 - is_floor_crease * 0.45);
                dy += 2.5 * is_floor_crease;
            }

            // Stepped tread slats on the ramp
            if (is_in_ramp_tread > 0.5)
            {
                float tread_phase = fract(world_pos.y / 3.0);
                if (tread_phase < 0.35)
                {
                    col.rgb *= 1.20;
                    metallic = 0.95;
                    roughness = 0.14;
                    dy -= 0.8;
                }
                else
                {
                    col.rgb *= 0.80;
                    dy += 0.8;
                }
            }

            // Hazard light on catwalk underside (tile 83)
            if (tile_id_fg == 83 && tile_local.y > 10.5 && col.g > 0.30)
            {
                gEmission = vec4(col.rgb * 3.0, 1.0);
            }
        }
        else if (layer == 2.0)
        {
            // ROJO: Interior Room Wall (Perforated Mesh)
            // Sits in middle depth behind character, catching flashlight beam
            col.rgb = mix(col.rgb * 0.88, col.rgb * (ai_rgb * 1.35), 0.45);
            metallic = 0.65;
            roughness = 0.38;

            // Ambient occlusion shadow along pillar edges and floor base
            vec2 step_side = vec2(2.5 / u_src_size.x, 0.0);
            float lum_left = dot(sample_pixel(TexCoords - step_side).rgb, vec3(0.299, 0.587, 0.114));
            float lum_right = dot(sample_pixel(TexCoords + step_side).rgb, vec3(0.299, 0.587, 0.114));
            if (abs(lum_left - lum) > 0.15 || abs(lum_right - lum) > 0.15)
            {
                col.rgb *= 0.68; // Contact corner shadow
            }
        }
        else if (layer == 1.0)
        {
            // Warning panels & screens in alcove
            col.rgb = mix(col.rgb, ai_rgb, 0.65);
            bool is_led = (ai_rgb.r > 0.55 && ai_rgb.g > 0.35 && ai_rgb.b < 0.20) || (ai_rgb.g > 0.50 && ai_rgb.b > 0.50);
            if (is_led) gEmission = vec4(ai_rgb * 2.5, 1.0);
            roughness = 0.18;
            metallic = 0.40;
        }
        else if (tile_type == 2)
        {
            // VERDE: Solid Vertical Structural Columns (Pillars 10, 11)
            // Strong vertical column bevels with horizontal segmented joints
            col.rgb = mix(col.rgb, col.rgb * (ai_rgb * 1.25), 0.25);
            metallic = 0.85;
            roughness = 0.22;

            // Bevel edges on the columns
            if (tile_local.x < 4.0) dx += 2.0;
            else if (tile_local.x > 26.0) dx -= 2.0;
        }
        else if (layer == 4.0 || tile_type == 0)
        {
            // AZUL: Deep Inaccessible Outer Background (Shaft / Abyss)
            // Visual depth recession: cool atmospheric haze, lower contrast, soft specularity
            vec3 depth_haze = vec3(0.012, 0.022, 0.038);
            col.rgb = mix(col.rgb * 0.55, depth_haze, 0.42);
            metallic = 0.25;
            roughness = 0.70;
        }
        else
        {
            // Under-ramp truss / foundation
            col.rgb = mix(col.rgb * 0.80, col.rgb * (ai_rgb * 1.30), 0.35);
            metallic = 0.75;
            roughness = 0.32;
        }
    }
    else if (u_hd_scaler == 1 && is_sprite > 0.5)
    {
        // Dynamic character sprite: metallic armor and normal relief
        metallic = 0.65;
        roughness = 0.30;
    }
    else if (u_hd_scaler == 1 && metallic > 0.15)
    {
        vec2 world_pixel = TexCoords * u_fbo_size;
        float hash1 = fract(sin(dot(floor(world_pixel), vec2(12.9898, 78.233))) * 43758.5453);
        float hash2 = fract(sin(dot(floor(world_pixel), vec2(93.9898, 67.345))) * 24634.6345);
        
        // Fine brushed metallic micro-grain
        float micro_grain = (hash1 - 0.5) * 0.025 * metallic;
        col.rgb += vec3(micro_grain);

        // Micro-normal perturbation for subtle brushed steel anisotropic highlights
        vec2 micro_n = (vec2(hash1, hash2) - 0.5) * 0.08 * metallic;
        dx += micro_n.x;
        dy += micro_n.y;
    }

    gAlbedo = col;

    vec3 n = normalize(vec3(dx, dy, 1.0));
    gNormal = vec4(n * 0.5 + 0.5, roughness);

    // Emission detection: Lasers, plasma, computer screens, door sensors, switches, sparks, fire
    bool isLaser = (col.r > 0.65 && col.g < 0.28 && col.b < 0.28);
    bool isPlasma = (col.g > 0.65 && col.r < 0.40);
    bool isElectric = (col.b > 0.65 && col.g > 0.45);
    bool isHot = (col.r > 0.80 && col.g > 0.55 && col.b < 0.35);
    bool isConsoleScreen = (col.g > 0.50 && col.b > 0.50 && col.r < 0.45);
    bool isSensorLED = (col.r > 0.70 && col.g < 0.25 && col.b < 0.25) || (col.g > 0.70 && col.r < 0.30 && col.b < 0.30);

    if (lum > 0.85 || isLaser || isPlasma || isElectric || isHot || isConsoleScreen || isSensorLED)
    {
        gEmission = vec4(col.rgb * 2.0, 1.0);
    }
    else
    {
        gEmission = vec4(0.0, 0.0, 0.0, 1.0);
    }

    // Occlusion map for 2D Ray Tracing: solid dense geometry casts shadows
    // r: shadow caster, g: metallic, b: water/fluid, a: 1.0
    float occ = (lum < 0.03 && col.a > 0.8) ? 1.0 : 0.0;
    gOcclusion = vec4(occ, metallic, water, 1.0);
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

const int MAX_UI_RECTS = 16;
uniform int u_num_ui_rects;
uniform vec4 u_ui_rects[MAX_UI_RECTS];

bool is_in_ui(vec2 uv)
{
    for (int i = 0; i < u_num_ui_rects; i++)
    {
        if (uv.x >= u_ui_rects[i].x && uv.x <= u_ui_rects[i].z &&
            uv.y >= u_ui_rects[i].y && uv.y <= u_ui_rects[i].w)
        {
            return true;
        }
    }
    return false;
}

uniform float u_aspect;
uniform int u_volumetric_enabled;

float trace_shadow(vec2 frag_pos, vec2 light_pos, float light_radius)
{
    if (u_raytracing_enabled == 0) return 1.0;

    vec2 ray = light_pos - frag_pos;
    float dist = length(vec2(ray.x * u_aspect, ray.y));
    if (dist < 0.001) return 1.0;

    int steps = (u_shadow_quality == 2) ? 28 : (u_shadow_quality == 1 ? 16 : 8);
    float shadow = 1.0;

    // Start from step 2 to avoid self-shadowing at the surface
    for (int i = 2; i < steps; i++)
    {
        vec2 sample_pos = frag_pos + ray * (float(i) / float(steps));
        float occ = texture(u_occlusion, sample_pos).r;
        if (occ > 0.5)
        {
            if (u_soft_shadows == 1)
            {
                // Soft shadow penumbra approximation based on distance
                float penumbra = 1.0 - float(i) / float(steps);
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

void main()
{
    vec4 albedo = texture(u_albedo, TexCoords);

    if (is_in_ui(TexCoords))
    {
        // UI layer has a higher Z-Index: drawn on top of the 2D world.
        // It is unaffected by ambient darkness, shadow casting, or world lights.
        vec3 col = albedo.rgb;
        if (TexCoords.y > 0.70 && col.g > 0.35 && col.r < 0.25 && col.b < 0.25)
        {
            col = min(col * 1.3, vec3(1.0)); // Crisp glowing digital LED readout for status bar
        }
        FragColor = vec4(col, 1.0);
        return;
    }

    vec4 norm_data = texture(u_normal, TexCoords);
    vec3 normal = normalize(norm_data.rgb * 2.0 - 1.0);
    float roughness = norm_data.a;
    vec4 occ_data = texture(u_occlusion, TexCoords);
    float metallic = occ_data.g;
    float water = occ_data.b;
    vec3 emission = texture(u_emission, TexCoords).rgb;

    if (water > 0.1)
    {
        roughness = mix(roughness, 0.04, water);
    }

    vec3 total_diffuse = u_ambient_color;
    vec3 total_specular = vec3(0.0);
    vec3 total_volumetric = vec3(0.0);

    for (int i = 0; i < u_num_lights; i++)
    {
        Light light = u_lights[i];
        vec2 light_screen = light.position.xy;
        vec2 dir_to_light = light_screen - TexCoords;
        vec2 dir_to_light_aspect = vec2(dir_to_light.x * u_aspect, dir_to_light.y);
        float dist_2d = length(dir_to_light_aspect);

        if (dist_2d > light.radius) continue;

        // Spot light check (e.g. player weapon flashlight)
        float spot_factor = 1.0;
        float spot_cone = 0.0;
        if (light.is_spot == 1)
        {
            if (dist_2d > 0.0001)
            {
                vec2 to_pixel = -dir_to_light_aspect / dist_2d;
                float angle_cos = dot(to_pixel, normalize(light.spot_dir));
                
                // Continuous, smooth optical degradé from center axis to perimeter
                float t = clamp((angle_cos - light.spot_cutoff) / max(0.0001, 1.0 - light.spot_cutoff), 0.0, 1.0);
                float smooth_t = smoothstep(0.0, 1.0, t);
                spot_factor = pow(smooth_t, 1.35);
                spot_cone = smooth_t;
            }
            else
            {
                spot_factor = 1.0;
                spot_cone = 1.0;
            }
        }

        // Distance attenuation (smooth Hermite cubic falloff)
        float atten = clamp(1.0 - (dist_2d / light.radius), 0.0, 1.0);
        atten = atten * atten * (3.0 - 2.0 * atten);

        // Raymarched soft shadow
        float shadow = trace_shadow(TexCoords, light_screen, light.radius);

        // Volumetric atmospheric in-scattering inside beam and light halo
        if (u_volumetric_enabled == 1 && shadow > 0.01)
        {
            if (light.is_spot == 1)
            {
                // Cinematic volumetric beam: soft, translucent optical degradé cone matching user reference
                float beam_dist_falloff = clamp(1.0 - (dist_2d / light.radius), 0.0, 1.0);
                float mie = pow(spot_cone, 1.6) * pow(beam_dist_falloff, 1.4);

                // Floating atmospheric micro-dust texture
                vec2 dust_uv = TexCoords * 180.0;
                float dust = fract(sin(dot(floor(dust_uv), vec2(12.9898, 78.233))) * 43758.5453);
                float haze = 0.90 + 0.20 * dust;

                // Translucent volumetric shaft (no blinding white wash, elegant soft degradé)
                total_volumetric += light.color * (mie * 0.095 * haze) * shadow;
            }
            else
            {
                total_volumetric += light.color * light.intensity * atten * shadow * 0.035;
            }
        }

        if (shadow <= 0.001 || spot_factor <= 0.001) continue;

        // 3D Direction to light (isotropic in physical screen space)
        vec3 light_dir = normalize(vec3(dir_to_light_aspect, light.position.z));
        float diff = max(dot(normal, light_dir), 0.0);

        // Specular (Blinn-Phong) with material roughness & metallic response
        vec3 view_dir = vec3(0.0, 0.0, 1.0);
        vec3 half_dir = normalize(light_dir + view_dir);
        float spec_exp = mix(128.0, 8.0, roughness);
        float spec = pow(max(dot(normal, half_dir), 0.0), spec_exp);
        vec3 spec_tint = mix(vec3(1.0), albedo.rgb, metallic);
        if (water > 0.1) spec_tint = vec3(1.0);

        vec3 light_contrib = light.color * light.intensity * atten * shadow * spot_factor * u_light_intensity;
        total_diffuse += light_contrib * diff * (1.0 - metallic * 0.45) * (1.0 - water * 0.35);
        total_specular += light_contrib * spec * spec_tint * (0.25 + metallic * 0.75 + water * 1.5);
    }

    vec3 final_color = albedo.rgb * total_diffuse + total_specular + emission + total_volumetric;

    // Filmic Tone Mapping (rich deep blacks, high dynamic contrast like modern PS5 engines)
    vec3 mapped = final_color / (final_color + vec3(0.55)) * 1.12;
    FragColor = vec4(mapped, albedo.a);
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
uniform sampler2D u_normal;
uniform sampler2D u_occlusion;
uniform float u_bloom_intensity;
uniform int u_bloom_enabled;
uniform int u_reflections_enabled;
uniform float u_time;

const int MAX_UI_RECTS = 16;
uniform int u_num_ui_rects;
uniform vec4 u_ui_rects[MAX_UI_RECTS];

bool is_in_ui(vec2 uv)
{
    for (int i = 0; i < u_num_ui_rects; i++)
    {
        if (uv.x >= u_ui_rects[i].x && uv.x <= u_ui_rects[i].z &&
            uv.y >= u_ui_rects[i].y && uv.y <= u_ui_rects[i].w)
        {
            return true;
        }
    }
    return false;
}

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

    bool is_ui = is_in_ui(TexCoords);
    if (is_ui)
    {
        // UI sits at top Z-index: crisp, unshaded, unaffected by world post-processing
        vec3 ui_col = base.rgb;
        if (u_bloom_enabled == 1 && TexCoords.y > 0.70)
        {
            vec3 bloom = texture(u_bloom, TexCoords).rgb;
            ui_col += bloom * (u_bloom_intensity * 0.5);
        }
        FragColor = vec4(ui_col, base.a);
        return;
    }

    // Screen-Space Planar Floor & Water Reflections (Characters, Monsters, Lasers & Muzzle Flash)
    if (u_reflections_enabled == 1)
    {
        vec4 norm_data = texture(u_normal, TexCoords);
        vec3 norm = normalize(norm_data.rgb * 2.0 - 1.0);
        float roughness = norm_data.a;
        vec4 occ_mat = texture(u_occlusion, TexCoords);
        float metallic = occ_mat.g;
        float water = occ_mat.b;
        vec4 albedo_col = texture(u_albedo, TexCoords);

        float reflect_factor = max(metallic * 0.70, water * 0.95);

        if (reflect_factor > 0.08)
        {
            // Fresnel approximation for planar reflection
            float fresnel = mix(0.25, 0.95, pow(clamp(1.0 - abs(norm.y), 0.0, 1.0), 3.0));

            // Animated water rippling or brushed metal micro-distortion
            vec2 ripple = vec2(0.0);
            if (water > 0.1)
            {
                ripple.x = (sin(TexCoords.x * 65.0 + u_time * 4.0) + cos(TexCoords.y * 45.0 + u_time * 3.0)) * 0.0035 * water;
                ripple.y = (cos(TexCoords.x * 55.0 - u_time * 3.5) + sin(TexCoords.y * 35.0 + u_time * 2.5)) * 0.0020 * water;
            }
            else if (metallic > 0.3)
            {
                ripple.x = sin(TexCoords.x * 240.0) * 0.0006 * roughness;
            }

            // Find surface contact edge (search upward a short distance to locate floor top)
            float y_surface = TexCoords.y;
            for (int i = 1; i <= 8; i++)
            {
                float test_y = TexCoords.y - float(i) * 0.004;
                vec4 test_mat = texture(u_occlusion, vec2(TexCoords.x, test_y));
                if (test_mat.g < 0.20 && test_mat.b < 0.08)
                {
                    y_surface = test_y + 0.002;
                    break;
                }
            }

            float depth = TexCoords.y - y_surface;
            if (depth >= 0.0 && depth < 0.28)
            {
                float refl_y = y_surface - depth + ripple.y;
                float refl_x = TexCoords.x + ripple.x;

                if (refl_y >= 0.0 && refl_y <= 1.0 && refl_x >= 0.0 && refl_x <= 1.0)
                {
                    // Roughness blur across reflection
                    float blur = roughness * 0.006;
                    vec3 refl_obj = texture(u_lit_scene, vec2(refl_x, refl_y)).rgb * 0.50
                                  + texture(u_lit_scene, vec2(refl_x - blur, refl_y)).rgb * 0.25
                                  + texture(u_lit_scene, vec2(refl_x + blur, refl_y)).rgb * 0.25;

                    // Falloff with vertical distance from floor
                    float dist_falloff = clamp(1.0 - (depth / 0.28), 0.0, 1.0);
                    dist_falloff = dist_falloff * dist_falloff;

                    // Color tint: metals reflect with albedo hue, water reflects neutrally with caustics
                    vec3 refl_tint = mix(vec3(0.95, 0.98, 1.0), albedo_col.rgb * 1.3, metallic);
                    if (water > 0.1)
                    {
                        float caustic = 0.88 + 0.24 * sin(TexCoords.x * 80.0 + TexCoords.y * 80.0 + u_time * 5.0);
                        refl_obj *= caustic;
                    }

                    float blend_weight = reflect_factor * fresnel * dist_falloff;
                    color = mix(color, color + refl_obj * refl_tint * 0.85, clamp(blend_weight, 0.0, 0.85));
                }
            }
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
