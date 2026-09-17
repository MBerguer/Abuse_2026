#include "remaster_hud.h"
#include "remaster_config.h"
#include "remaster_lighting.h"
#include "remaster_gl.h"
#include "remaster_hd.h"
#include "remaster_shaders.h"
#include "common.h"
#include "image.h"
#include "palette.h"
#include "jwindow.h"
#include "event.h"
#include "view.h"
#include "level.h"
#include "game.h"
#include "loader2.h"
#include "sbar.h"
#include "cache.h"
#include "file_utils.h"
#include "netcfg.h"
#include "net/sock.h"
#include "sdlport/setup.h"
#include "loadgame.h"
#include "clisp.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>

extern unsigned char fnt6x13[192 * 104];
extern WindowManager *wm;
extern palette *pal;
extern int xres, yres;
extern Settings settings;
extern net_protocol *prot;

namespace
{
inline uint32_t make_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    return (static_cast<uint32_t>(a) << 24) |
           (static_cast<uint32_t>(b) << 16) |
           (static_cast<uint32_t>(g) << 8)  |
           (static_cast<uint32_t>(r));
}

inline uint32_t modulate_alpha(uint32_t color, float alpha_mult)
{
    uint8_t a = static_cast<uint8_t>(((color >> 24) & 0xFF) * std::clamp(alpha_mult, 0.0f, 1.0f));
    return (color & 0x00FFFFFF) | (static_cast<uint32_t>(a) << 24);
}

void draw_char_scaled(uint32_t *buffer, int buf_w, int buf_h, int x, int y, unsigned char c, uint32_t color, int scale)
{
    int col = c % 32;
    int row = c / 32;
    int src_x = col * 6;
    int src_y = row * 13;

    for (int dy = 0; dy < 13; dy++)
    {
        for (int dx = 0; dx < 6; dx++)
        {
            int font_idx = (src_y + dy) * 192 + (src_x + dx);
            if (fnt6x13[font_idx] != 0)
            {
                for (int sy = 0; sy < scale; sy++)
                {
                    for (int sx = 0; sx < scale; sx++)
                    {
                        int px = x + dx * scale + sx;
                        int py = y + dy * scale + sy;
                        if (px >= 0 && px < buf_w && py >= 0 && py < buf_h)
                        {
                            buffer[py * buf_w + px] = color;
                        }
                    }
                }
            }
        }
    }
}

void draw_string(uint32_t *buffer, int buf_w, int buf_h, int x, int y, const char *str, uint32_t color, int scale = 1, bool shadow = true)
{
    if (!str) return;

    if (shadow)
    {
        uint32_t shadow_col = make_rgba(0, 0, 0, static_cast<uint8_t>(((color >> 24) & 0xFF) * 0.85f));
        const char *p = str;
        int cur_x = x + scale;
        int cur_y = y + scale;
        while (*p)
        {
            draw_char_scaled(buffer, buf_w, buf_h, cur_x, cur_y, static_cast<unsigned char>(*p), shadow_col, scale);
            cur_x += 6 * scale + scale;
            p++;
        }
    }

    const char *p = str;
    int cur_x = x;
    while (*p)
    {
        draw_char_scaled(buffer, buf_w, buf_h, cur_x, y, static_cast<unsigned char>(*p), color, scale);
        cur_x += 6 * scale + scale;
        p++;
    }
}

void fill_rect(uint32_t *buffer, int buf_w, int buf_h, int x, int y, int w, int h, uint32_t color)
{
    uint8_t a = (color >> 24) & 0xFF;
    if (a == 0) return;
    uint8_t r = color & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = (color >> 16) & 0xFF;

    for (int j = std::max(0, y); j < std::min(buf_h, y + h); j++)
    {
        for (int i = std::max(0, x); i < std::min(buf_w, x + w); i++)
        {
            if (a == 255)
            {
                buffer[j * buf_w + i] = color;
            }
            else
            {
                uint32_t dst = buffer[j * buf_w + i];
                uint8_t da = (dst >> 24) & 0xFF;
                uint8_t dr = dst & 0xFF;
                uint8_t dg = (dst >> 8) & 0xFF;
                uint8_t db = (dst >> 16) & 0xFF;

                float alpha = a / 255.0f;
                uint8_t out_r = static_cast<uint8_t>(r * alpha + dr * (1.0f - alpha));
                uint8_t out_g = static_cast<uint8_t>(g * alpha + dg * (1.0f - alpha));
                uint8_t out_b = static_cast<uint8_t>(b * alpha + db * (1.0f - alpha));
                uint8_t out_a = std::min(255, a + da);

                buffer[j * buf_w + i] = make_rgba(out_r, out_g, out_b, out_a);
            }
        }
    }
}

void draw_frame(uint32_t *buffer, int buf_w, int buf_h, int x, int y, int w, int h, uint32_t bg_color, uint32_t border_color)
{
    fill_rect(buffer, buf_w, buf_h, x, y, w, h, bg_color);

    // 1px border
    for (int i = x; i < x + w; i++)
    {
        if (i >= 0 && i < buf_w)
        {
            if (y >= 0 && y < buf_h) buffer[y * buf_w + i] = border_color;
            if (y + h - 1 >= 0 && y + h - 1 < buf_h) buffer[(y + h - 1) * buf_w + i] = border_color;
        }
    }
    for (int j = y; j < y + h; j++)
    {
        if (j >= 0 && j < buf_h)
        {
            if (x >= 0 && x < buf_w) buffer[j * buf_w + x] = border_color;
            if (x + w - 1 >= 0 && x + w - 1 < buf_w) buffer[j * buf_w + (x + w - 1)] = border_color;
        }
    }

    // High-tech corner notches
    const int notch_len = 8;
    for (int n = 0; n < notch_len; n++)
    {
        // Top-left
        if (x + n < buf_w && y + 1 < buf_h) buffer[(y + 1) * buf_w + (x + n)] = border_color;
        if (x + 1 < buf_w && y + n < buf_h) buffer[(y + n) * buf_w + (x + 1)] = border_color;
        // Top-right
        if (x + w - 1 - n >= 0 && y + 1 < buf_h) buffer[(y + 1) * buf_w + (x + w - 1 - n)] = border_color;
        if (x + w - 2 >= 0 && y + n < buf_h) buffer[(y + n) * buf_w + (x + w - 2)] = border_color;
        // Bottom-left
        if (x + n < buf_w && y + h - 2 >= 0) buffer[(y + h - 2) * buf_w + (x + n)] = border_color;
        if (x + 1 < buf_w && y + h - 1 - n >= 0) buffer[(y + h - 1 - n) * buf_w + (x + 1)] = border_color;
        // Bottom-right
        if (x + w - 1 - n >= 0 && y + h - 2 >= 0) buffer[(y + h - 2) * buf_w + (x + w - 1 - n)] = border_color;
        if (x + w - 2 >= 0 && y + h - 1 - n >= 0) buffer[(y + h - 1 - n) * buf_w + (x + w - 2)] = border_color;
    }
}
} // namespace

bool RemasterHUD::init()
{
    if (m_initialized)
        return true;

    if (!RemasterGL::compile_shader(m_prog, RemasterShaders::quad_vs, RemasterShaders::hud_overlay_fs))
    {
        printf("RemasterHUD: Failed to compile HUD shaders.\n");
        return false;
    }

    m_canvas_w = 960;
    m_canvas_h = 540;
    m_pixels.assign(m_canvas_w * m_canvas_h, 0);

    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_canvas_w, m_canvas_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    m_initialized = true;
    printf("RemasterHUD: Subsystem initialized successfully.\n");
    return true;
}

void RemasterHUD::update_canvas(int w, int h)
{
    if (w <= 0 || h <= 0) return;
    if (m_canvas_w == w && m_canvas_h == h && !m_pixels.empty())
        return;

    m_canvas_w = w;
    m_canvas_h = h;
    m_pixels.assign(m_canvas_w * m_canvas_h, 0);

    if (m_texture)
    {
        glBindTexture(GL_TEXTURE_2D, m_texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_canvas_w, m_canvas_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }
}

void RemasterHUD::cleanup()
{
    if (m_texture)
    {
        glDeleteTextures(1, &m_texture);
        m_texture = 0;
    }
    if (m_prog)
    {
        glDeleteProgram(m_prog);
        m_prog = 0;
    }
    m_pixels.clear();
    for (int i = 0; i < 8; i++)
    {
        m_hd_weapons[i].pixels.clear();
        m_hd_weapons[i].width = 0;
        m_hd_weapons[i].height = 0;
    }
    m_assets_loaded = false;
    m_initialized = false;
}

void RemasterHUD::draw_notification(float alpha)
{
    auto &cfg = RemasterConfig::get();
    if (cfg.notification_text.empty())
        return;

    int text_len = static_cast<int>(cfg.notification_text.length());
    int char_w = 7; // 6px + 1px spacing
    int text_w = text_len * char_w;
    int box_w = text_w + 40;
    int box_h = 30;
    int box_x = (m_canvas_w - box_w) / 2;
    int box_y = 20;

    uint32_t bg = modulate_alpha(make_rgba(6, 12, 22, 220), alpha);
    uint32_t border = modulate_alpha(make_rgba(0, 225, 255, 240), alpha);
    uint32_t text_col = modulate_alpha(make_rgba(230, 250, 255, 255), alpha);

    draw_frame(m_pixels.data(), m_canvas_w, m_canvas_h, box_x, box_y, box_w, box_h, bg, border);
    draw_string(m_pixels.data(), m_canvas_w, m_canvas_h, box_x + 20, box_y + 8, cfg.notification_text.c_str(), text_col, 1, true);
}

void RemasterHUD::draw_dashboard()
{
    auto &cfg = RemasterConfig::get();
    int panel_w = 640;
    int panel_h = 380;
    int panel_x = (m_canvas_w - panel_w) / 2;
    int panel_y = (m_canvas_h - panel_h) / 2;

    uint32_t bg = make_rgba(8, 14, 26, 235);
    uint32_t border = make_rgba(0, 210, 255, 255);
    uint32_t hdr_bar = make_rgba(0, 60, 110, 200);
    uint32_t text_white = make_rgba(240, 245, 255, 255);
    uint32_t text_cyan = make_rgba(0, 230, 255, 255);
    uint32_t text_green = make_rgba(50, 255, 120, 255);
    uint32_t text_dim = make_rgba(130, 150, 170, 255);
    uint32_t badge_on = make_rgba(0, 255, 150, 255);
    uint32_t badge_off = make_rgba(180, 70, 70, 255);

    draw_frame(m_pixels.data(), m_canvas_w, m_canvas_h, panel_x, panel_y, panel_w, panel_h, bg, border);
    fill_rect(m_pixels.data(), m_canvas_w, m_canvas_h, panel_x + 1, panel_y + 1, panel_w - 2, 28, hdr_bar);

    draw_string(m_pixels.data(), m_canvas_w, m_canvas_h, panel_x + 16, panel_y + 8, "ABUSE 2026 : NEXT-GEN REMASTER DASHBOARD", text_cyan, 1, true);
    draw_string(m_pixels.data(), m_canvas_w, m_canvas_h, panel_x + panel_w - 120, panel_y + 8, "[F12] CLOSE", text_dim, 1, false);

    int cur_y = panel_y + 40;
    draw_string(m_pixels.data(), m_canvas_w, m_canvas_h, panel_x + 20, cur_y, "SYSTEM & HARDWARE PIPELINE", text_cyan, 1, false);
    cur_y += 18;
    draw_string(m_pixels.data(), m_canvas_w, m_canvas_h, panel_x + 24, cur_y, "* Render Subsystem:  OpenGL 3.3 Core Profile (Metal Accelerated)", text_dim, 1, false);
    cur_y += 16;
    draw_string(m_pixels.data(), m_canvas_w, m_canvas_h, panel_x + 24, cur_y, "* Simulation:        Fixed-timestep 60Hz with 144Hz+ Sub-frame Lerp", text_dim, 1, false);
    cur_y += 22;

    draw_string(m_pixels.data(), m_canvas_w, m_canvas_h, panel_x + 20, cur_y, "REAL-TIME REMASTER FEATURES MATRIX", text_cyan, 1, false);
    cur_y += 18;

    auto draw_row = [&](const char *label, bool active, const char *extra_info) {
        draw_string(m_pixels.data(), m_canvas_w, m_canvas_h, panel_x + 24, cur_y, label, text_white, 1, false);
        if (active)
        {
            draw_string(m_pixels.data(), m_canvas_w, m_canvas_h, panel_x + 280, cur_y, "[ ACTIVE ]", badge_on, 1, false);
            if (extra_info)
                draw_string(m_pixels.data(), m_canvas_w, m_canvas_h, panel_x + 370, cur_y, extra_info, text_dim, 1, false);
        }
        else
        {
            draw_string(m_pixels.data(), m_canvas_w, m_canvas_h, panel_x + 280, cur_y, "[ OFF ]", badge_off, 1, false);
        }
        cur_y += 17;
    };

    const char *mode_str = "NEXT-GEN xBR HD (1920x1200 / 4K)";
    if (cfg.render_mode == RENDER_MODE_CLASSIC_1995) mode_str = "ORIGINAL 1995 RETRO (VGA)";
    else if (cfg.render_mode == RENDER_MODE_CLASSIC_RT) mode_str = "CLASSIC 16px RETRO + 2D RT";

    draw_row("[F11] Render Mode:", cfg.enabled, mode_str);

    draw_row("[F10] Next-Gen xBR HD:", cfg.hd_textures, "Edge-Directed 6x Subpixel Filter");

    draw_row("2D Ray Tracing & Shadows:", cfg.raytracing, "32 Steps Soft Penumbra");
    draw_row("Volumetric Atmospheric Fog:", cfg.volumetric_fog, "Light Shaft Scattering");
    draw_row("Procedural Normal Mapping:", cfg.normal_mapping, "Sobel 3D Relief");
    draw_row("HDR Bloom & Emission:", cfg.bloom, "Ping-Pong Gaussian Blur");
    draw_row("Screen-Space Reflections:", cfg.reflections, "SSR Water & Wet Floors");
    draw_row("3D Spatial Audio & HRTF:", cfg.spatial_audio, "Acoustic Attenuation");
    draw_row("Widescreen Aspect Correction:", cfg.widescreen, "Letterbox / Dynamic FOV");

    char lights_str[64];
    snprintf(lights_str, sizeof(lights_str), "%d tracked", (int)RemasterLighting::get().get_lights().size());
    draw_row("Dynamic Light Sources:", true, lights_str);
    draw_row("[F8] Gamma / Brightness:", true, "Interactive Calibration Dialog");

    // Footer
    cur_y = panel_y + panel_h - 32;
    fill_rect(m_pixels.data(), m_canvas_w, m_canvas_h, panel_x + 1, cur_y, panel_w - 2, 31, make_rgba(4, 8, 16, 220));
    draw_string(m_pixels.data(), m_canvas_w, m_canvas_h, panel_x + 16, cur_y + 9, "CONTROLS: [F8] Pos Debug  |  [F10] HD PBR  |  [F11] Cycle Modes  |  [F12] Close", text_cyan, 1, false);
}

void RemasterHUD::draw_pos_debug()
{
    view *v = player_list;
    if (!v && the_game) v = the_game->first_view;
    if (!v || !v->m_focus)
        return;

    int px = v->m_focus->x;
    int py = v->m_focus->y;
    int tw = f_wid > 0 ? f_wid : 16;
    int th = f_hi > 0 ? f_hi : 16;
    int tx = (px >= 0) ? (px / tw) : -1;
    int ty = (py >= 0) ? (py / th) : -1;
    int cx = v->xoff();
    int cy = v->yoff();

    uint16_t fg = 0;
    uint16_t bg = 0;
    if (current_level && tx >= 0 && ty >= 0)
    {
        fg = current_level->GetFg(ivec2(tx, ty));
        bg = current_level->GetBg(ivec2(tx, ty));
    }

    char line1[128];
    char line2[128];
    snprintf(line1, sizeof(line1), "PLAYER POS:  X=%-5d  Y=%-5d  [F8: Toggle]", px, py);
    snprintf(line2, sizeof(line2), "TILE: [%-3d, %-3d]  FG: %-3d  BG: %-3d", tx, ty, fg, bg);

    int box_w = 340;
    int box_h = 42;
    int box_x = 16;
    int box_y = 16;

    uint32_t bg_col = make_rgba(8, 14, 24, 225);
    uint32_t border_col = make_rgba(0, 220, 255, 240);
    uint32_t text_col1 = make_rgba(255, 255, 255, 255);
    uint32_t text_col2 = make_rgba(0, 230, 255, 255);

    draw_frame(m_pixels.data(), m_canvas_w, m_canvas_h, box_x, box_y, box_w, box_h, bg_col, border_col);
    draw_string(m_pixels.data(), m_canvas_w, m_canvas_h, box_x + 12, box_y + 7, line1, text_col1, 1, true);
    draw_string(m_pixels.data(), m_canvas_w, m_canvas_h, box_x + 12, box_y + 23, line2, text_col2, 1, true);
}

void RemasterHUD::draw_cursor(int dst_x0, int dst_y0, int dst_w, int dst_h, void *im_ptr, void *pal_ptr)
{
    image *im = static_cast<image *>(im_ptr);
    palette *p = static_cast<palette *>(pal_ptr);
    if (!im || !p || dst_w <= 0 || dst_h <= 0) return;

    int im_w = im->Size().x;
    int im_h = im->Size().y;
    if (im_w <= 0 || im_h <= 0) return;

    for (int dy = 0; dy < dst_h; dy++)
    {
        int sy = dy * im_h / dst_h;
        int py = dst_y0 + dy;
        if (py < 0 || py >= m_canvas_h) continue;

        const uint8_t *src_row = im->scan_line(sy);
        for (int dx = 0; dx < dst_w; dx++)
        {
            int sx = dx * im_w / dst_w;
            int px = dst_x0 + dx;
            if (px < 0 || px >= m_canvas_w) continue;

            uint8_t c = src_row[sx];
            if (c == 0) continue; // transparent pixel

            uint8_t r = p->red(c);
            uint8_t g = p->green(c);
            uint8_t b = p->blue(c);

            m_pixels[py * m_canvas_w + px] = make_rgba(r, g, b, 255);
        }
    }
}

bool RemasterHUD::load_texture_rgba(const std::string &rel_path, HDTexture &out_tex)
{
    std::vector<std::string> candidates;
    char *pfx = get_filename_prefix();
    if (pfx && pfx[0])
    {
        candidates.push_back(std::string(pfx) + "/" + rel_path);
        candidates.push_back(std::string(pfx) + "/hd/hud/" + rel_path);
    }
    char *spfx = get_save_filename_prefix();
    if (spfx && spfx[0])
    {
        candidates.push_back(std::string(spfx) + "/data/" + rel_path);
        candidates.push_back(std::string(spfx) + "/data/hd/hud/" + rel_path);
    }
    candidates.push_back(rel_path);
    candidates.push_back("data/" + rel_path);
    candidates.push_back("data/hd/hud/" + rel_path);
    candidates.push_back("abuse.app/Contents/Resources/data/" + rel_path);
    candidates.push_back("abuse.app/Contents/Resources/data/hd/hud/" + rel_path);
    candidates.push_back("../Resources/data/hd/hud/" + rel_path);
    candidates.push_back("build/src/abuse.app/Contents/Resources/data/hd/hud/" + rel_path);

    for (const auto &path : candidates)
    {
        FILE *f = fopen(path.c_str(), "rb");
        if (f)
        {
            uint32_t w = 0, h = 0;
            if (fread(&w, 4, 1, f) == 1 && fread(&h, 4, 1, f) == 1 &&
                w > 0 && h > 0 && w <= 4096 && h <= 4096)
            {
                out_tex.width = static_cast<int>(w);
                out_tex.height = static_cast<int>(h);
                out_tex.pixels.resize(w * h);
                size_t read_bytes = fread(out_tex.pixels.data(), 4, w * h, f);
                fclose(f);
                if (read_bytes == static_cast<size_t>(w * h))
                {
                    printf("[RemasterHUD] Loaded HD texture %s (%dx%d) from %s\n", rel_path.c_str(), w, h, path.c_str());
                    return true;
                }
            }
            else
            {
                fclose(f);
            }
        }
    }
    return false;
}

void RemasterHUD::load_all_assets()
{
    if (m_assets_loaded) return;
    load_texture_rgba("hud_bay_empty.rgba", m_hud_bay_empty);
    load_texture_rgba("hud_top_grate.rgba", m_hud_top_grate);
    load_texture_rgba("weapon_01.rgba", m_hd_weapons[0]);
    load_texture_rgba("weapon_02.rgba", m_hd_weapons[1]);
    m_assets_loaded = true;
}

void RemasterHUD::draw_seven_segment_digit(uint32_t *buf, int bw, int bh, int x, int y, int digit, int seg_w, int seg_h, int thickness, uint32_t on_color, uint32_t off_color, bool glow)
{
    static const uint8_t s_seg_masks[10] = {
        0x3F, // 0: a, b, c, d, e, f
        0x06, // 1: b, c
        0x5B, // 2: a, b, d, e, g
        0x4F, // 3: a, b, c, d, g
        0x66, // 4: b, c, f, g
        0x6D, // 5: a, c, d, f, g
        0x7D, // 6: a, c, d, e, f, g
        0x07, // 7: a, b, c
        0x7F, // 8: a, b, c, d, e, f, g
        0x6F  // 9: a, b, c, d, f, g
    };

    uint8_t mask = 0;
    if (digit >= 0 && digit <= 9)
        mask = s_seg_masks[digit];

    int t = std::max(2, thickness);
    seg_w = std::max(t * 3 + 2, seg_w);
    seg_h = std::max(t * 5 + 4, seg_h);

    int mid_y = y + (seg_h - t) / 2;
    int seg_len_v = mid_y - (y + t);
    int seg_len_h = seg_w - 2 * t;

    struct SegInfo {
        char type; // 'h' or 'v'
        int sx, sy, slen, st;
    };

    SegInfo segs[7] = {
        { 'h', x + t, y, seg_len_h, t },                         // 0: a (top)
        { 'v', x + seg_w - t, y + t, seg_len_v, t },             // 1: b (top-right)
        { 'v', x + seg_w - t, mid_y + t, seg_len_v, t },         // 2: c (bottom-right)
        { 'h', x + t, y + seg_h - t, seg_len_h, t },            // 3: d (bottom)
        { 'v', x, mid_y + t, seg_len_v, t },                     // 4: e (bottom-left)
        { 'v', x, y + t, seg_len_v, t },                         // 5: f (top-left)
        { 'h', x + t, mid_y, seg_len_h, t }                      // 6: g (middle)
    };

    auto draw_horiz_miter = [&](int hx, int hy, int len, int th, uint32_t col) {
        int half = th / 2;
        for (int dy = 0; dy < th; dy++) {
            int py = hy + dy;
            if (py < 0 || py >= bh) continue;
            int inset = half - std::abs(dy - half);
            int x1 = hx + inset;
            int x2 = hx + len - inset;
            for (int px = x1; px < x2; px++) {
                if (px >= 0 && px < bw)
                    buf[py * bw + px] = col;
            }
        }
    };

    auto draw_vert_miter = [&](int vx, int vy, int len, int th, uint32_t col) {
        int half = th / 2;
        for (int dx = 0; dx < th; dx++) {
            int px = vx + dx;
            if (px < 0 || px >= bw) continue;
            int inset = half - std::abs(dx - half);
            int y1 = vy + inset;
            int y2 = vy + len - inset;
            for (int py = y1; py < y2; py++) {
                if (py >= 0 && py < bh)
                    buf[py * bw + px] = col;
            }
        }
    };

    for (int s = 0; s < 7; s++)
    {
        bool is_lit = (mask & (1 << s)) != 0;
        const auto &seg = segs[s];

        if (is_lit)
        {
            if (glow)
            {
                uint32_t glow_col = modulate_alpha(on_color, 0.35f);
                if (seg.type == 'h')
                    draw_horiz_miter(seg.sx - 1, seg.sy - 1, seg.slen + 2, seg.st + 2, glow_col);
                else
                    draw_vert_miter(seg.sx - 1, seg.sy - 1, seg.slen + 2, seg.st + 2, glow_col);
            }

            if (seg.type == 'h')
            {
                draw_horiz_miter(seg.sx, seg.sy, seg.slen, seg.st, on_color);
                uint32_t core = make_rgba(230, 255, 235, 220);
                draw_horiz_miter(seg.sx + 2, seg.sy + seg.st / 2, seg.slen - 4, 1, core);
            }
            else
            {
                draw_vert_miter(seg.sx, seg.sy, seg.slen, seg.st, on_color);
                uint32_t core = make_rgba(230, 255, 235, 220);
                draw_vert_miter(seg.sx + seg.st / 2, seg.sy + 2, seg.slen - 4, 1, core);
            }
        }
        else
        {
            if (seg.type == 'h')
                draw_horiz_miter(seg.sx, seg.sy, seg.slen, seg.st, off_color);
            else
                draw_vert_miter(seg.sx, seg.sy, seg.slen, seg.st, off_color);
        }
    }
}

void RemasterHUD::draw_seven_segment_number(uint32_t *buf, int bw, int bh, int x, int y, int number, int num_digits, int seg_w, int seg_h, int thickness, uint32_t on_color, uint32_t off_color, bool glow, bool pad_zeroes)
{
    int digit_gap = std::max(2, seg_w / 4);
    int d_val = number;

    int digits[8];
    for (int i = num_digits - 1; i >= 0; i--)
    {
        if (d_val >= 0)
        {
            digits[i] = d_val % 10;
            d_val /= 10;
        }
        else
        {
            digits[i] = -1; // unlit
        }
    }

    if (!pad_zeroes && number >= 0)
    {
        for (int i = 0; i < num_digits - 1; i++)
        {
            if (digits[i] == 0)
                digits[i] = -1;
            else
                break;
        }
    }

    int cur_x = x;
    for (int i = 0; i < num_digits; i++)
    {
        draw_seven_segment_digit(buf, bw, bh, cur_x, y, digits[i], seg_w, seg_h, thickness, on_color, off_color, glow);
        cur_x += seg_w + digit_gap;
    }
}

void RemasterHUD::draw_texture(uint32_t *buf, int bw, int bh, int dst_x, int dst_y, int dst_w, int dst_h, const HDTexture &img, float brightness)
{
    if (img.width <= 0 || img.height <= 0 || img.pixels.empty() || dst_w <= 0 || dst_h <= 0)
        return;

    for (int dy = 0; dy < dst_h; dy++)
    {
        int py = dst_y + dy;
        if (py < 0 || py >= bh) continue;

        int sy = (dy * img.height) / dst_h;
        if (sy >= img.height) sy = img.height - 1;

        for (int dx = 0; dx < dst_w; dx++)
        {
            int px = dst_x + dx;
            if (px < 0 || px >= bw) continue;

            int sx = (dx * img.width) / dst_w;
            if (sx >= img.width) sx = img.width - 1;

            uint32_t src = img.pixels[sy * img.width + sx];
            uint8_t sa = (src >> 24) & 0xFF;
            if (sa == 0) continue;

            uint8_t sr = static_cast<uint8_t>(std::clamp((src & 0xFF) * brightness, 0.0f, 255.0f));
            uint8_t sg = static_cast<uint8_t>(std::clamp(((src >> 8) & 0xFF) * brightness, 0.0f, 255.0f));
            uint8_t sb = static_cast<uint8_t>(std::clamp(((src >> 16) & 0xFF) * brightness, 0.0f, 255.0f));

            if (sa == 255)
            {
                buf[py * bw + px] = make_rgba(sr, sg, sb, 255);
            }
            else
            {
                uint32_t dst = buf[py * bw + px];
                uint8_t da = (dst >> 24) & 0xFF;
                uint8_t dr = dst & 0xFF;
                uint8_t dg = (dst >> 8) & 0xFF;
                uint8_t db = (dst >> 16) & 0xFF;

                float a = sa / 255.0f;
                uint8_t out_r = static_cast<uint8_t>(sr * a + dr * (1.0f - a));
                uint8_t out_g = static_cast<uint8_t>(sg * a + dg * (1.0f - a));
                uint8_t out_b = static_cast<uint8_t>(sb * a + db * (1.0f - a));
                uint8_t out_a = std::min(255, sa + da);

                buf[py * bw + px] = make_rgba(out_r, out_g, out_b, out_a);
            }
        }
    }
}

void RemasterHUD::draw_hud_statusbar(int window_w, int window_h, int vp_x, int vp_y, int vp_w, int vp_h, int src_w, int src_h)
{
    view *v = player_list;
    if (!v && the_game) v = the_game->first_view;
    if (!v) v = sbar.get_view();
    if (!v) return;

    // For test frame verification, ensure weapon 1 (Grenade Launcher) is active and has ammo
    if (getenv("ABUSE_DUMP_FRAME"))
    {
        if (!v->has_weapon(1))
        {
            v->give_weapon(1);
            v->add_ammo(1, 50);
        }
        if (v->weapon_total(0) == 0)
        {
            v->add_ammo(0, 100);
        }
    }

    int sx1 = 0, sy1 = 0, sx2 = 0, sy2 = 0;
    if (!sbar.get_area(sx1, sy1, sx2, sy2))
    {
        sx1 = 0;
        sx2 = src_w > 0 ? src_w : 320;
        sy1 = src_h > 0 ? (src_h - 32) : 168;
        sy2 = src_h > 0 ? src_h : 200;
    }

    if (vp_w <= 0) vp_w = window_w;
    if (vp_h <= 0) vp_h = window_h;
    if (src_w <= 0) src_w = xres > 0 ? xres : 320;
    if (src_h <= 0) src_h = yres > 0 ? yres : 200;

    float scale_x = (float)vp_w / (float)src_w;
    float scale_y = (float)vp_h / (float)src_h;

    int bar_x = vp_x + (int)std::round(sx1 * scale_x);
    int bar_y = vp_y + (int)std::round(sy1 * scale_y);
    int bar_w = (int)std::round((sx2 - sx1) * scale_x);
    int bar_h = (int)std::round((sy2 - sy1) * scale_y);

    bar_x = std::clamp(bar_x, 0, m_canvas_w - 1);
    bar_w = std::min(bar_w, m_canvas_w - bar_x);
    bar_y = std::clamp(bar_y, 0, m_canvas_h - 1);
    bar_h = std::min(bar_h, m_canvas_h - bar_y);

    if (bar_w <= 20 || bar_h <= 10) return;

    // 1. Top Industrial Ventilation Grate (from user reference y=0..160)
    int top_grate_h = std::max(6, (int)(bar_h * 0.11f));
    if (m_hud_top_grate.width > 0)
    {
        int gw = (int)(m_hud_top_grate.width * ((float)top_grate_h / (float)m_hud_top_grate.height));
        if (gw <= 0) gw = 64;
        for (int gx = bar_x; gx < bar_x + bar_w; gx += gw)
        {
            int draw_gw = std::min(gw, bar_x + bar_w - gx);
            draw_texture(m_pixels.data(), m_canvas_w, m_canvas_h, gx, bar_y, draw_gw, top_grate_h, m_hud_top_grate, 0.90f);
        }
    }
    else
    {
        fill_rect(m_pixels.data(), m_canvas_w, m_canvas_h, bar_x, bar_y, bar_w, top_grate_h, make_rgba(12, 16, 22, 255));
    }

    int chassis_y = bar_y + top_grate_h;
    int chassis_h = bar_h - top_grate_h;

    // Dark brushed titanium backing across entire status bar
    for (int y = chassis_y; y < chassis_y + chassis_h; y++)
    {
        if (y < 0 || y >= m_canvas_h) continue;
        float ty = (float)(y - chassis_y) / (float)chassis_h;
        int base_r = (int)(28.0f - ty * 14.0f);
        int base_g = (int)(32.0f - ty * 16.0f);
        int base_b = (int)(38.0f - ty * 18.0f);
        for (int x = bar_x; x < bar_x + bar_w; x++)
        {
            int grain = (((x * 67 + y * 13) ^ (x * 19)) & 0x07) - 3;
            m_pixels[y * m_canvas_w + x] = make_rgba(std::clamp(base_r + grain, 0, 255),
                                                     std::clamp(base_g + grain, 0, 255),
                                                     std::clamp(base_b + grain, 0, 255), 255);
        }
    }

    // 2. Health Station (Left Wing)
    int health_x0 = bar_x;
    int health_x1 = bar_x + (int)std::round(47.0f * scale_x);
    int health_w = health_x1 - health_x0;
    int health_cx = health_x0 + health_w / 2;
    int health_cy = chassis_y + chassis_h / 2;
    int r_outer = (int)(chassis_h * 0.44f);
    int r_inner = r_outer - 7;

    // Beveled circular steel collar
    for (int dy = -r_outer; dy <= r_outer; dy++)
    {
        int py = health_cy + dy;
        if (py < 0 || py >= m_canvas_h) continue;
        for (int dx = -r_outer; dx <= r_outer; dx++)
        {
            int px = health_cx + dx;
            if (px < 0 || px >= m_canvas_w) continue;
            float dist = std::sqrt((float)(dx * dx + dy * dy));
            if (dist <= r_outer)
            {
                if (dist > r_inner)
                {
                    float angle = std::atan2((float)dy, (float)dx);
                    float light = 0.5f - 0.45f * std::cos(angle + 0.785f);
                    uint8_t mr = static_cast<uint8_t>(std::clamp(90.0f + light * 90.0f, 35.0f, 220.0f));
                    uint8_t mg = static_cast<uint8_t>(std::clamp(105.0f + light * 95.0f, 40.0f, 230.0f));
                    uint8_t mb = static_cast<uint8_t>(std::clamp(120.0f + light * 100.0f, 50.0f, 240.0f));
                    m_pixels[py * m_canvas_w + px] = make_rgba(mr, mg, mb, 255);
                }
                else
                {
                    m_pixels[py * m_canvas_w + px] = make_rgba(4, 16, 10, 255);
                }
            }
        }
    }

    // 4 hex bolts at 45, 135, 225, 315 deg
    for (int a_idx = 0; a_idx < 4; a_idx++)
    {
        float ang = 0.785398f + a_idx * 1.570796f;
        int sx = health_cx + (int)(std::cos(ang) * (r_outer - 3));
        int sy = health_cy + (int)(std::sin(ang) * (r_outer - 3));
        fill_rect(m_pixels.data(), m_canvas_w, m_canvas_h, sx - 1, sy - 1, 3, 3, make_rgba(185, 205, 225, 255));
        if (sx >= 0 && sx < m_canvas_w && sy >= 0 && sy < m_canvas_h)
            m_pixels[sy * m_canvas_w + sx] = make_rgba(30, 40, 50, 255);
    }

    // Health digital value
    int hp_val = (v && v->m_focus) ? v->m_focus->hp() : 0;
    hp_val = std::clamp(hp_val, 0, 999);

    uint32_t hp_on_col = make_rgba(65, 255, 110, 255); // cyber-green
    if (hp_val <= 25)
        hp_on_col = make_rgba(255, 45, 45, 255); // red alert
    else if (hp_val <= 50)
        hp_on_col = make_rgba(255, 195, 40, 255); // amber alert

    int h_seg_w = std::max(12, (int)(chassis_h * 0.13f));
    int h_seg_h = std::max(22, (int)(chassis_h * 0.26f));
    int h_thick = std::max(3, h_seg_w / 4);
    int h_gap = 3;
    int total_hp_w = h_seg_w * 3 + h_gap * 2;
    int hp_digits_x = health_cx - total_hp_w / 2;
    int hp_digits_y = health_cy - h_seg_h / 2 - (int)(chassis_h * 0.05f);

    draw_seven_segment_number(m_pixels.data(), m_canvas_w, m_canvas_h, hp_digits_x, hp_digits_y,
                              hp_val, 3, h_seg_w, h_seg_h, h_thick, hp_on_col, make_rgba(14, 40, 20, 80), true, false);

    // Heart icon + "HEALTH" label below digits
    int label_y = hp_digits_y + h_seg_h + 5;
    int heart_x = health_cx - 26;
    int heart_y = label_y + 2;
    uint32_t heart_col = (hp_val <= 25) ? make_rgba(255, 50, 50, 255) : make_rgba(255, 75, 105, 255);
    fill_rect(m_pixels.data(), m_canvas_w, m_canvas_h, heart_x + 1, heart_y, 2, 2, heart_col);
    fill_rect(m_pixels.data(), m_canvas_w, m_canvas_h, heart_x + 4, heart_y, 2, 2, heart_col);
    fill_rect(m_pixels.data(), m_canvas_w, m_canvas_h, heart_x, heart_y + 1, 7, 3, heart_col);
    fill_rect(m_pixels.data(), m_canvas_w, m_canvas_h, heart_x + 1, heart_y + 4, 5, 1, heart_col);
    fill_rect(m_pixels.data(), m_canvas_w, m_canvas_h, heart_x + 2, heart_y + 5, 3, 1, heart_col);
    fill_rect(m_pixels.data(), m_canvas_w, m_canvas_h, heart_x + 3, heart_y + 6, 1, 1, heart_col);

    draw_string(m_pixels.data(), m_canvas_w, m_canvas_h, heart_x + 10, label_y, "HEALTH", make_rgba(170, 215, 235, 240), 1, false);

    // 3. Modular Weapon Bays (8 modular slots matching statbar.cpp mouse coordinates exactly)
    int cur_selected = v->current_weapon;
    int mouse_hover = sbar.get_icon_in_selection();

    for (int i = 0; i < 8; i++)
    {
        int bx = bar_x + (int)std::round((47.0f + i * 33.0f) * scale_x);
        int next_bx = bar_x + (int)std::round((47.0f + (i + 1) * 33.0f) * scale_x);
        int bw = next_bx - bx;
        if (bw <= 8) continue;

        bool is_current = (cur_selected == i);
        bool is_owned = v->has_weapon(i);
        bool is_hovered = (mouse_hover == i);

        // Draw 3D metallic modular bay chassis from reference image!
        if (m_hud_bay_empty.width > 0)
        {
            float bay_bright = is_current ? 1.05f : (is_hovered ? 1.15f : 0.95f);
            draw_texture(m_pixels.data(), m_canvas_w, m_canvas_h, bx, chassis_y, bw, chassis_h, m_hud_bay_empty, bay_bright);
        }

        // Weapon sprite inside the holster tray
        if (is_owned)
        {
            int tray_x = bx + (int)(bw * 0.15f);
            int tray_y = chassis_y + (int)(chassis_h * 0.26f);
            int tray_w = (int)(bw * 0.70f);
            int tray_h = (int)(chassis_h * 0.42f);

            float brightness = is_current ? 1.0f : 0.90f;
            if (i < 8 && m_hd_weapons[i].width > 0)
            {
                // Soft drop shadow offset +2, +2
                draw_texture(m_pixels.data(), m_canvas_w, m_canvas_h, tray_x + 2, tray_y + 2, tray_w, tray_h, m_hd_weapons[i], 0.12f);
                // HD weapon sprite
                draw_texture(m_pixels.data(), m_canvas_w, m_canvas_h, tray_x, tray_y, tray_w, tray_h, m_hd_weapons[i], brightness);
            }
            else
            {
                int icon_id = is_current ? sbar.get_bweap(i) : sbar.get_dweap(i);
                if (icon_id >= 0 && pal)
                {
                    image *im = cache.img(icon_id);
                    if (im && im->Size().x > 0 && im->Size().y > 0)
                    {
                        int im_w = im->Size().x;
                        int im_h = im->Size().y;
                        float sc = std::min((float)(tray_w - 4) / (float)im_w, (float)(tray_h - 4) / (float)im_h);
                        int fw = std::max(1, (int)(im_w * sc));
                        int fh = std::max(1, (int)(im_h * sc));
                        int ox = tray_x + (tray_w - fw) / 2;
                        int oy = tray_y + (tray_h - fh) / 2;
                        for (int dy = 0; dy < fh; dy++)
                        {
                            int py = oy + dy;
                            if (py < 0 || py >= m_canvas_h) continue;
                            int sy = (dy * im_h) / fh;
                            const uint8_t *src_row = im->scan_line(sy);
                            for (int dx = 0; dx < fw; dx++)
                            {
                                int px = ox + dx;
                                if (px < 0 || px >= m_canvas_w) continue;
                                int sx = (dx * im_w) / fw;
                                uint8_t c = src_row[sx];
                                if (c == 0) continue;
                                uint8_t r = (uint8_t)(pal->red(c) * brightness);
                                uint8_t g = (uint8_t)(pal->green(c) * brightness);
                                uint8_t b = (uint8_t)(pal->blue(c) * brightness);
                                m_pixels[py * m_canvas_w + px] = make_rgba(r, g, b, 255);
                            }
                        }
                    }
                }
            }
        }

        // Selection highlight frame
        if (is_hovered)
        {
            uint32_t hov_col = make_rgba(255, 215, 60, 240);
            draw_frame(m_pixels.data(), m_canvas_w, m_canvas_h, bx + 1, chassis_y + 1, bw - 2, chassis_h - 2, 0, hov_col);
        }
        else if (is_current)
        {
            uint32_t act_col = make_rgba(0, 230, 255, 230);
            draw_frame(m_pixels.data(), m_canvas_w, m_canvas_h, bx + 1, chassis_y + 1, bw - 2, chassis_h - 2, 0, act_col);
        }

        // Ammo Display inside the trapezoidal window
        int ammo_w = (int)(bw * 0.58f);
        int ammo_h = (int)(chassis_h * 0.20f);
        int ammo_x = bx + (int)(bw * 0.18f);
        int ammo_y = chassis_y + (int)(chassis_h * 0.77f);

        // Slot number indicator on left side
        char slot_num[4];
        snprintf(slot_num, sizeof(slot_num), "%d", i + 1);
        draw_string(m_pixels.data(), m_canvas_w, m_canvas_h, ammo_x + 2, ammo_y + (ammo_h - 12) / 2, slot_num, make_rgba(140, 180, 205, 220), 1, false);

        // 7-segment Ammo counter
        int ammo_val = is_owned ? v->weapon_total(i) : -1;
        if (ammo_val > 999) ammo_val = 999;

        int a_seg_w = std::max(8, (int)(ammo_h * 0.36f));
        int a_seg_h = std::max(14, (int)(ammo_h * 0.72f));
        int a_thick = std::max(2, a_seg_w / 4);
        int a_gap = 2;
        int total_ammo_w = a_seg_w * 3 + a_gap * 2;
        int ammo_digits_x = ammo_x + ammo_w - total_ammo_w - 4;
        int ammo_digits_y = ammo_y + (ammo_h - a_seg_h) / 2;

        uint32_t a_on_col = is_current ? make_rgba(65, 255, 110, 255) : make_rgba(40, 220, 90, 240);
        uint32_t a_off_col = make_rgba(10, 32, 18, 55);

        draw_seven_segment_number(m_pixels.data(), m_canvas_w, m_canvas_h, ammo_digits_x, ammo_digits_y,
                                  ammo_val, 3, a_seg_w, a_seg_h, a_thick, a_on_col, a_off_col, is_owned, true);
    }

    // Right end cap / chassis terminator
    int bays_end_x = bar_x + (int)std::round((47.0f + 8 * 33.0f) * scale_x);
    if (bays_end_x < bar_x + bar_w && m_hud_bay_empty.width > 0)
    {
        int cap_w = bar_x + bar_w - bays_end_x;
        draw_texture(m_pixels.data(), m_canvas_w, m_canvas_h, bays_end_x, chassis_y, cap_w, chassis_h, m_hud_bay_empty, 0.85f);
    }
}

void RemasterHUD::draw_main_menu(int window_w, int window_h, int vp_x, int vp_y, int vp_w, int vp_h, int src_w, int src_h)
{
    if (vp_w <= 0) vp_w = window_w;
    if (vp_h <= 0) vp_h = window_h;
    if (src_w <= 0) src_w = xres > 0 ? xres : 320;
    if (src_h <= 0) src_h = yres > 0 ? yres : 200;

    float scale_x = (float)vp_w / (float)src_w;
    float scale_y = (float)vp_h / (float)src_h;

    ivec2 mpos = wm ? wm->GetMousePos() : ivec2(0, 0);
    float win_mx = (float)vp_x + (float)mpos.x * scale_x;
    float win_my = (float)vp_y + (float)mpos.y * scale_y;

    int dock_w = std::min(380, (int)(window_w * 0.30f));
    dock_w = std::max(280, dock_w);
    int dock_x = window_w - dock_w;
    int dock_h = window_h;

    // Brushed titanium dock background with anisotropic micro-grain
    for (int y = 0; y < dock_h; y++)
    {
        float ty = (float)y / (float)dock_h;
        int base_r = (int)(20.0f + ty * 6.0f);
        int base_g = (int)(26.0f + ty * 7.0f);
        int base_b = (int)(34.0f + ty * 8.0f);

        for (int x = dock_x; x < window_w; x++)
        {
            int grain = (((x * 67 + y * 13) ^ (x * 19)) & 0x07) - 3;
            uint8_t r = static_cast<uint8_t>(std::clamp(base_r + grain, 0, 255));
            uint8_t g = static_cast<uint8_t>(std::clamp(base_g + grain, 0, 255));
            uint8_t b = static_cast<uint8_t>(std::clamp(base_b + grain, 0, 255));
            m_pixels[y * m_canvas_w + x] = make_rgba(r, g, b, 245);
        }
    }

    // Glowing vertical cyan neon strip along the left edge of the dock
    for (int y = 0; y < dock_h; y++)
    {
        for (int hx = -3; hx <= 3; hx++)
        {
            int px = dock_x + hx;
            if (px < 0 || px >= m_canvas_w) continue;
            uint32_t neon = (hx == 0) ? make_rgba(255, 255, 255, 255)
                                      : (std::abs(hx) == 1 ? make_rgba(100, 235, 255, 200)
                                                           : make_rgba(0, 160, 255, 70));
            uint32_t dst = m_pixels[y * m_canvas_w + px];
            uint8_t a = (neon >> 24) & 0xFF;
            float af = a / 255.0f;
            uint8_t r = (uint8_t)((neon & 0xFF) * af + (dst & 0xFF) * (1.0f - af));
            uint8_t g = (uint8_t)(((neon >> 8) & 0xFF) * af + ((dst >> 8) & 0xFF) * (1.0f - af));
            uint8_t b = (uint8_t)(((neon >> 16) & 0xFF) * af + ((dst >> 16) & 0xFF) * (1.0f - af));
            m_pixels[y * m_canvas_w + px] = make_rgba(r, g, b, 255);
        }
    }

    // Terminal Header
    int head_x = dock_x + 20;
    int head_y = 16;
    draw_string(m_pixels.data(), m_canvas_w, m_canvas_h, head_x, head_y, "ABUSE // 2026", make_rgba(0, 230, 255, 255), 2, true);
    draw_string(m_pixels.data(), m_canvas_w, m_canvas_h, head_x, head_y + 22, "SYSTEM TERMINAL - ACCESS GRANTED", make_rgba(130, 165, 190, 220), 1, false);

    // Divider line below header
    fill_rect(m_pixels.data(), m_canvas_w, m_canvas_h, head_x, head_y + 35, dock_w - 40, 1, make_rgba(0, 200, 255, 140));

    // Determine buttons present (exact same order as menu.cpp)
    struct MenuBtn {
        int id;
        std::string title;
        std::string subtitle;
        uint32_t icon_color;
    };
    std::vector<MenuBtn> buttons;

    if (current_level)
        buttons.push_back({ ID_RETURN, "RESUME MISSION", "RETURN TO ACTIVE COMBAT", make_rgba(0, 255, 200, 255) });
    if (show_load_icon())
        buttons.push_back({ ID_LOAD_PLAYER_GAME, "LOAD GAME", "RESTORE CHECKPOINT", make_rgba(100, 200, 255, 255) });

    buttons.push_back({ ID_START_GAME, "START GAME", "NEW CAMPAIGN OPERATION", make_rgba(65, 255, 110, 255) });

    std::string diff_str = "NORMAL";
    if (DEFINEDP(symbol_value(l_difficulty)))
    {
        if (symbol_value(l_difficulty) == l_extreme) diff_str = "EXTREME";
        else if (symbol_value(l_difficulty) == l_hard) diff_str = "HARD";
        else if (symbol_value(l_difficulty) == l_easy) diff_str = "EASY";
    }
    if (!main_net_cfg || (main_net_cfg->state != net_configuration::SERVER && main_net_cfg->state != net_configuration::CLIENT))
        buttons.push_back({ ID_NULL, "DIFFICULTY: " + diff_str, "COMBAT SIMULATION LEVEL", make_rgba(255, 200, 50, 255) });

    buttons.push_back({ ID_LIGHT_OFF, "DISPLAY GAMMA", "CALIBRATE BRIGHTNESS", make_rgba(255, 175, 40, 255) });
    buttons.push_back({ ID_VOLUME, "AUDIO SETTINGS", "SFX & MUSIC CONTROLS", make_rgba(160, 210, 255, 255) });
    if (prot)
        buttons.push_back({ ID_NETWORKING, "MULTIPLAYER", "LOCAL NETWORK & SERVER", make_rgba(200, 120, 255, 255) });
    buttons.push_back({ ID_QUIT, "QUIT TO DESKTOP", "TERMINATE SIMULATION", make_rgba(255, 75, 75, 255) });

    int button_h = settings.hires ? 39 : 25;
    int total_height = (int)buttons.size() * button_h;
    int orig_y0 = (src_h - total_height) / 2;

    for (size_t i = 0; i < buttons.size(); i++)
    {
        int by_top = vp_y + (int)std::round((orig_y0 + (int)i * button_h) * scale_y);
        int by_bot = vp_y + (int)std::round((orig_y0 + ((int)i + 1) * button_h) * scale_y);
        int bh = by_bot - by_top;

        int card_x = dock_x + 16;
        int card_w = dock_w - 32;
        int card_y = by_top + 2;
        int card_h = bh - 4;

        bool is_hover = (win_my >= by_top && win_my < by_bot && win_mx >= (window_w - dock_w));

        // Button Card Chassis
        uint32_t bg_col = is_hover ? make_rgba(35, 50, 66, 255) : make_rgba(18, 24, 32, 230);
        fill_rect(m_pixels.data(), m_canvas_w, m_canvas_h, card_x, card_y, card_w, card_h, bg_col);

        // Frame
        uint32_t frame_col = is_hover ? make_rgba(0, 240, 255, 255) : make_rgba(65, 85, 105, 180);
        draw_frame(m_pixels.data(), m_canvas_w, m_canvas_h, card_x, card_y, card_w, card_h, 0, frame_col);

        if (is_hover)
        {
            // Specular top highlight sheen
            fill_rect(m_pixels.data(), m_canvas_w, m_canvas_h, card_x + 2, card_y + 1, card_w - 4, 1, make_rgba(255, 255, 255, 140));
            // Left neon accent bar
            fill_rect(m_pixels.data(), m_canvas_w, m_canvas_h, card_x + 1, card_y + 1, 4, card_h - 2, make_rgba(0, 240, 255, 255));
        }

        // Icon indicator bar on left of card
        int icon_box_x = card_x + (is_hover ? 8 : 6);
        int icon_box_y = card_y + (card_h - 18) / 2;
        fill_rect(m_pixels.data(), m_canvas_w, m_canvas_h, icon_box_x, icon_box_y, 4, 18, buttons[i].icon_color);

        // Typography
        uint32_t text_col = is_hover ? make_rgba(255, 255, 255, 255) : make_rgba(215, 230, 245, 240);
        int text_x = icon_box_x + 14;
        int title_y = card_y + (card_h > 36 ? 6 : (card_h - 14) / 2);
        draw_string(m_pixels.data(), m_canvas_w, m_canvas_h, text_x, title_y, buttons[i].title.c_str(), text_col, 1, is_hover);

        if (card_h > 36)
        {
            draw_string(m_pixels.data(), m_canvas_w, m_canvas_h, text_x, title_y + 16, buttons[i].subtitle.c_str(), make_rgba(110, 145, 175, 200), 1, false);
        }
    }
}

void RemasterHUD::render(int window_w, int window_h, int vp_x, int vp_y, int vp_w, int vp_h, int src_w, int src_h)
{
    if (!m_initialized)
        return;

    auto &cfg = RemasterConfig::get();
    load_all_assets();
    update_canvas(window_w, window_h);

    // Update notification timer
    float alpha = 1.0f;
    if (cfg.notification_timer > 0.0f)
    {
        cfg.notification_timer -= 0.01667f;
        if (cfg.notification_timer < 0.5f)
            alpha = std::clamp(cfg.notification_timer / 0.5f, 0.0f, 1.0f);
    }

    view *v = player_list;
    if (!v && the_game) v = the_game->first_view;
    if (!v) v = sbar.get_view();

    bool in_play = (the_game != nullptr &&
                    (the_game->state == RUN_STATE || the_game->state == PAUSE_STATE) &&
                    current_level != nullptr &&
                    v != nullptr);
    bool in_menu = (the_game != nullptr && the_game->state == MENU_STATE);

    bool need_statusbar = (cfg.enabled && in_play);
    bool need_menu = (cfg.enabled && in_menu);
    bool need_notification = (cfg.notification_timer > 0.0f && !cfg.notification_text.empty());
    bool need_dashboard = cfg.show_hud_overlay && in_play;
    bool need_cursor = (cfg.enabled && wm && wm->has_mouse() && wm->GetMouseVisual() != nullptr);
    bool need_pos_debug = cfg.show_pos_debug && in_play && (v != nullptr && v->m_focus != nullptr);

    if (!need_statusbar && !need_menu && !need_notification && !need_dashboard && !need_cursor && !need_pos_debug)
        return;

    // Clear canvas
    std::fill(m_pixels.begin(), m_pixels.end(), 0);

    if (need_statusbar)
        draw_hud_statusbar(window_w, window_h, vp_x, vp_y, vp_w, vp_h, src_w, src_h);

    if (need_menu)
        draw_main_menu(window_w, window_h, vp_x, vp_y, vp_w, vp_h, src_w, src_h);

    if (need_pos_debug)
        draw_pos_debug();

    if (need_dashboard)
        draw_dashboard();

    if (need_notification)
        draw_notification(alpha);

    if (need_cursor)
    {
        image *mv = wm->GetMouseVisual();
        ivec2 mpos = wm->GetMousePos();
        ivec2 mcenter = wm->GetMouseCenter();

        if (vp_w <= 0) vp_w = window_w;
        if (vp_h <= 0) vp_h = window_h;
        if (src_w <= 0) src_w = xres > 0 ? xres : 320;
        if (src_h <= 0) src_h = yres > 0 ? yres : 200;

        float win_x = (float)vp_x + (float)(mpos.x - mcenter.x) * (float)vp_w / (float)src_w;
        float win_y = (float)vp_y + (float)(mpos.y - mcenter.y) * (float)vp_h / (float)src_h;

        int can_x = (int)std::round(win_x * (float)m_canvas_w / (float)window_w);
        int can_y = (int)std::round(win_y * (float)m_canvas_h / (float)window_h);

        int can_w = std::max(1, (int)std::round((float)mv->Size().x * ((float)vp_w / (float)src_w) * ((float)m_canvas_w / (float)window_w)));
        int can_h = std::max(1, (int)std::round((float)mv->Size().y * ((float)vp_h / (float)src_h) * ((float)m_canvas_h / (float)window_h)));

        draw_cursor(can_x, can_y, can_w, can_h, mv, pal);
    }

    // Upload to OpenGL texture
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_canvas_w, m_canvas_h, GL_RGBA, GL_UNSIGNED_BYTE, m_pixels.data());

    // Render over full window viewport
    glViewport(0, 0, window_w, window_h);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(m_prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glUniform1i(glGetUniformLocation(m_prog, "u_hud_texture"), 0);

    RemasterGL::render_quad();

    glDisable(GL_BLEND);
}
