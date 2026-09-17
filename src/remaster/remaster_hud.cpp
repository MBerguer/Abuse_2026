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
    load_texture_rgba("hud_bay_plate.rgba", m_hud_bay_plate);
    load_texture_rgba("hud_bay_empty.rgba", m_hud_bay_empty);
    load_texture_rgba("hud_top_grate.rgba", m_hud_top_grate);
    for (int i = 0; i < 8; i++)
    {
        char bay_fn[32];
        snprintf(bay_fn, sizeof(bay_fn), "hud_bay_%d.rgba", i);
        load_texture_rgba(bay_fn, m_hud_bay[i]);
    }
    load_texture_rgba("weapon_01.rgba", m_hd_weapons[0]);
    load_texture_rgba("weapon_02.rgba", m_hd_weapons[1]);

    load_texture_rgba("title_bg.rgba", m_title_bg);
    load_texture_rgba("menu_btn_start.rgba", m_btn_start);
    load_texture_rgba("menu_btn_diff.rgba", m_btn_diff);
    load_texture_rgba("menu_btn_gamma.rgba", m_btn_gamma);
    load_texture_rgba("menu_btn_volume.rgba", m_btn_volume);
    load_texture_rgba("menu_btn_quit.rgba", m_btn_quit);
    load_texture_rgba("menu_btn_return.rgba", m_btn_return);
    load_texture_rgba("menu_btn_load.rgba", m_btn_load);
    load_texture_rgba("menu_btn_net.rgba", m_btn_net);

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

    // 2. Health Station (Left Wing) - Cybernetic Biometric Core
    int health_x0 = bar_x;
    int health_x1 = bar_x + (int)std::round(47.0f * scale_x);
    int health_w = health_x1 - health_x0;
    int health_cx = health_x0 + health_w / 2;
    int health_cy = chassis_y + chassis_h / 2;
    int r_outer = (int)(chassis_h * 0.45f);
    int r_gauge_outer = r_outer - 5;
    int r_gauge_inner = r_gauge_outer - 5;
    int r_glass = r_gauge_inner - 2;

    int hp_val = (v && v->m_focus) ? v->m_focus->hp() : 0;
    hp_val = std::clamp(hp_val, 0, 999);

    uint32_t hp_on_col = make_rgba(55, 255, 115, 255); // cyber-green
    if (hp_val <= 25)
        hp_on_col = make_rgba(255, 45, 45, 255); // red alert
    else if (hp_val <= 50)
        hp_on_col = make_rgba(255, 195, 35, 255); // amber alert

    int lit_gauge_segs = std::clamp((hp_val * 32 + 99) / 100, 0, 32);

    // Armored Titanium Bezel, 32-Segment Radial LED Arc, Obsidian Core & Glass Dome
    for (int dy = -r_outer; dy <= r_outer; dy++)
    {
        int py = health_cy + dy;
        if (py < 0 || py >= m_canvas_h) continue;
        for (int dx = -r_outer; dx <= r_outer; dx++)
        {
            int px = health_cx + dx;
            if (px < 0 || px >= m_canvas_w) continue;
            float dist = std::sqrt((float)(dx * dx + dy * dy));
            if (dist > r_outer) continue;

            if (dist > r_gauge_outer)
            {
                // Stage 1: Armored Titanium Bezel Collar with directional lighting
                float angle = std::atan2((float)dy, (float)dx);
                float light = 0.5f - 0.48f * std::cos(angle + 0.785f);
                float rim_factor = (dist - r_gauge_outer) / (float)(r_outer - r_gauge_outer);
                float edge_shade = (rim_factor > 0.85f || rim_factor < 0.15f) ? 0.65f : 1.0f;
                uint8_t mr = static_cast<uint8_t>(std::clamp((95.0f + light * 110.0f) * edge_shade, 30.0f, 235.0f));
                uint8_t mg = static_cast<uint8_t>(std::clamp((110.0f + light * 115.0f) * edge_shade, 35.0f, 245.0f));
                uint8_t mb = static_cast<uint8_t>(std::clamp((125.0f + light * 120.0f) * edge_shade, 45.0f, 255.0f));
                m_pixels[py * m_canvas_w + px] = make_rgba(mr, mg, mb, 255);
            }
            else if (dist >= r_gauge_inner)
            {
                // Stage 2: 32-Segment Radial LED Circular Energy Meter
                float angle = std::atan2((float)dy, (float)dx);
                float gauge_ang = angle - 1.570796f; // 0 at bottom (6 o'clock)
                while (gauge_ang < 0.0f) gauge_ang += 6.2831853f;
                while (gauge_ang >= 6.2831853f) gauge_ang -= 6.2831853f;

                float seg_pos = gauge_ang / (6.2831853f / 32.0f);
                int seg_idx = (int)seg_pos;
                float seg_frac = seg_pos - seg_idx;

                if (seg_frac < 0.80f)
                {
                    if (seg_idx < lit_gauge_segs)
                    {
                        float mid_r = (r_gauge_outer + r_gauge_inner) * 0.5f;
                        float core_dist = std::abs(dist - mid_r) / (float)(r_gauge_outer - r_gauge_inner);
                        if (core_dist < 0.25f)
                            m_pixels[py * m_canvas_w + px] = make_rgba(235, 255, 240, 255);
                        else
                            m_pixels[py * m_canvas_w + px] = hp_on_col;
                    }
                    else
                    {
                        m_pixels[py * m_canvas_w + px] = make_rgba(8, 30, 16, 255);
                    }
                }
                else
                {
                    m_pixels[py * m_canvas_w + px] = make_rgba(4, 10, 8, 255);
                }
            }
            else if (dist > r_glass)
            {
                m_pixels[py * m_canvas_w + px] = make_rgba(10, 16, 22, 255);
            }
            else
            {
                // Stage 3: Obsidian Core Lens & Curved Specular Reflection
                float core_ratio = dist / (float)r_glass;
                int bg_r = (int)(6 + core_ratio * 12);
                int bg_g = (int)(10 + core_ratio * 16);
                int bg_b = (int)(14 + core_ratio * 20);

                if (dx < 0 && dy < 0 && dist >= r_glass * 0.40f && dist <= r_glass * 0.85f)
                {
                    float sheen = (1.0f - std::abs(dist - r_glass * 0.65f) / (r_glass * 0.22f));
                    float ang_fade = (-dx / (float)r_glass) * (-dy / (float)r_glass) * 3.2f;
                    float highlight = std::clamp(sheen * ang_fade * 45.0f, 0.0f, 60.0f);
                    bg_r = std::min(255, bg_r + (int)highlight);
                    bg_g = std::min(255, bg_g + (int)(highlight * 1.1f));
                    bg_b = std::min(255, bg_b + (int)(highlight * 1.25f));
                }

                m_pixels[py * m_canvas_w + px] = make_rgba(bg_r, bg_g, bg_b, 255);
            }
        }
    }

    // 8 Countersunk Hex Bolts around bezel
    for (int a_idx = 0; a_idx < 8; a_idx++)
    {
        float ang = a_idx * 0.7853982f;
        int sx = health_cx + (int)std::round(std::cos(ang) * (r_outer - 3));
        int sy = health_cy + (int)std::round(std::sin(ang) * (r_outer - 3));
        fill_rect(m_pixels.data(), m_canvas_w, m_canvas_h, sx - 1, sy - 1, 3, 3, make_rgba(195, 215, 235, 255));
        if (sx >= 0 && sx < m_canvas_w && sy >= 0 && sy < m_canvas_h)
            m_pixels[sy * m_canvas_w + sx] = make_rgba(20, 28, 38, 255);
    }

    // 7-segment digital health value (centered above vital pulse)
    int h_seg_w = std::max(11, (int)(chassis_h * 0.125f));
    int h_seg_h = std::max(20, (int)(chassis_h * 0.24f));
    int h_thick = std::max(3, h_seg_w / 4);
    int h_gap = 3;
    int total_hp_w = h_seg_w * 3 + h_gap * 2;
    int hp_digits_x = health_cx - total_hp_w / 2;
    int hp_digits_y = health_cy - h_seg_h / 2 - (int)(chassis_h * 0.08f);

    draw_seven_segment_number(m_pixels.data(), m_canvas_w, m_canvas_h, hp_digits_x, hp_digits_y,
                              hp_val, 3, h_seg_w, h_seg_h, h_thick, hp_on_col, make_rgba(10, 35, 18, 65), true, false);

    // Dynamic Biometric EKG Heartbeat Pulse Wave beneath digits
    int ekg_y = hp_digits_y + h_seg_h + 4;
    int ekg_w = total_hp_w + 10;
    int ekg_x0 = health_cx - ekg_w / 2;

    const int ekg_pts = 16;
    float ekg_profile[ekg_pts] = {
        0.0f, 0.0f, 0.2f, 0.0f, -0.25f, 1.0f, -0.6f, 0.0f, 0.35f, 0.15f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f
    };
    int prev_ex = ekg_x0;
    int prev_ey = ekg_y;
    for (int p = 0; p < ekg_pts; p++)
    {
        int cur_ex = ekg_x0 + (p * ekg_w) / (ekg_pts - 1);
        int cur_ey = ekg_y - (int)(ekg_profile[p] * 7.0f);
        int steps = std::max(std::abs(cur_ex - prev_ex), std::abs(cur_ey - prev_ey));
        for (int s = 0; s <= steps; s++)
        {
            float t = steps > 0 ? (float)s / (float)steps : 0.0f;
            int lx = (int)(prev_ex + t * (cur_ex - prev_ex));
            int ly = (int)(prev_ey + t * (cur_ey - prev_ey));
            if (lx >= 0 && lx < m_canvas_w && ly >= 0 && ly < m_canvas_h)
            {
                m_pixels[ly * m_canvas_w + lx] = hp_on_col;
                if (ly > 0) m_pixels[(ly - 1) * m_canvas_w + lx] = modulate_alpha(hp_on_col, 0.35f);
                if (ly + 1 < m_canvas_h) m_pixels[(ly + 1) * m_canvas_w + lx] = modulate_alpha(hp_on_col, 0.35f);
            }
        }
        prev_ex = cur_ex;
        prev_ey = cur_ey;
    }

    // High-tech heart icon and "VITALS" label
    int label_y = ekg_y + 8;
    int heart_x = health_cx - 25;
    int heart_y = label_y + 1;
    uint32_t heart_col = (hp_val <= 25) ? make_rgba(255, 45, 45, 255) : make_rgba(255, 70, 105, 255);
    fill_rect(m_pixels.data(), m_canvas_w, m_canvas_h, heart_x + 1, heart_y, 2, 2, heart_col);
    fill_rect(m_pixels.data(), m_canvas_w, m_canvas_h, heart_x + 4, heart_y, 2, 2, heart_col);
    fill_rect(m_pixels.data(), m_canvas_w, m_canvas_h, heart_x, heart_y + 1, 7, 3, heart_col);
    fill_rect(m_pixels.data(), m_canvas_w, m_canvas_h, heart_x + 1, heart_y + 4, 5, 1, heart_col);
    fill_rect(m_pixels.data(), m_canvas_w, m_canvas_h, heart_x + 2, heart_y + 5, 3, 1, heart_col);
    fill_rect(m_pixels.data(), m_canvas_w, m_canvas_h, heart_x + 3, heart_y + 6, 1, 1, heart_col);

    draw_string(m_pixels.data(), m_canvas_w, m_canvas_h, heart_x + 10, label_y, "VITALS", make_rgba(165, 220, 240, 230), 1, false);

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

        // Render modular bay: matching custom molded silhouette for each slot 0..7!
        if (m_hud_bay[i].width > 0)
        {
            float bay_bright = is_current ? 1.05f : (is_hovered ? 1.15f : 0.95f);
            draw_texture(m_pixels.data(), m_canvas_w, m_canvas_h, bx, chassis_y, bw, chassis_h, m_hud_bay[i], bay_bright);
        }
        else if (m_hud_bay_plate.width > 0)
        {
            float bay_bright = is_current ? 1.05f : (is_hovered ? 1.15f : 0.95f);
            draw_texture(m_pixels.data(), m_canvas_w, m_canvas_h, bx, chassis_y, bw, chassis_h, m_hud_bay_plate, bay_bright);
        }
        else if (m_hud_bay_empty.width > 0)
        {
            draw_texture(m_pixels.data(), m_canvas_w, m_canvas_h, bx, chassis_y, bw, chassis_h, m_hud_bay_empty, 0.95f);
        }

        // Tray region inside bay
        int tray_x = bx + (int)(bw * 0.15f);
        int tray_y = chassis_y + (int)(chassis_h * 0.24f);
        int tray_w = (int)(bw * 0.70f);
        int tray_h = (int)(chassis_h * 0.44f);

        // Active weapon selection effects (internal illumination backlight, neon rails, corner bracket clips)
        if (is_current)
        {
            // 1. Internal bay illumination glow (soft electric cyan/blue light pool)
            for (int gy = tray_y - 4; gy < tray_y + tray_h + 4; gy++)
            {
                if (gy < 0 || gy >= m_canvas_h) continue;
                float ty = (float)(gy - tray_y) / (float)tray_h;
                float v_glow = 1.0f - std::abs(ty - 0.45f) * 1.5f;
                if (v_glow <= 0.0f) continue;
                for (int gx = tray_x - 2; gx < tray_x + tray_w + 2; gx++)
                {
                    if (gx < 0 || gx >= m_canvas_w) continue;
                    float tx = (float)(gx - (tray_x + tray_w / 2)) / (float)(tray_w / 2);
                    float h_glow = 1.0f - tx * tx;
                    if (h_glow <= 0.0f) continue;
                    float alpha = v_glow * h_glow * 0.28f;
                    uint32_t dst = m_pixels[gy * m_canvas_w + gx];
                    uint8_t dr = dst & 0xFF, dg = (dst >> 8) & 0xFF, db = (dst >> 16) & 0xFF;
                    uint8_t nr = static_cast<uint8_t>(dr * (1.0f - alpha) + 0 * alpha);
                    uint8_t ng = static_cast<uint8_t>(dg * (1.0f - alpha) + 190 * alpha);
                    uint8_t nb = static_cast<uint8_t>(db * (1.0f - alpha) + 255 * alpha);
                    m_pixels[gy * m_canvas_w + gx] = make_rgba(nr, ng, nb, 255);
                }
            }

            // 2. Top and Bottom Neon LED Guide Rails
            int rail_x0 = bx + 2;
            int rail_x1 = bx + bw - 3;
            int rail_w = rail_x1 - rail_x0;
            fill_rect(m_pixels.data(), m_canvas_w, m_canvas_h, rail_x0, chassis_y + 1, rail_w, 1, make_rgba(0, 180, 255, 140));
            fill_rect(m_pixels.data(), m_canvas_w, m_canvas_h, rail_x0 + 1, chassis_y + 2, rail_w - 2, 1, make_rgba(215, 250, 255, 255));
            fill_rect(m_pixels.data(), m_canvas_w, m_canvas_h, rail_x0, chassis_y + 3, rail_w, 1, make_rgba(0, 180, 255, 140));

            int bot_rail_y = chassis_y + (int)(chassis_h * 0.73f);
            fill_rect(m_pixels.data(), m_canvas_w, m_canvas_h, rail_x0 + 2, bot_rail_y, rail_w - 4, 1, make_rgba(0, 210, 255, 200));

            // 3. Four Sleek Industrial Corner Bracket Clips
            int clip_len = 6;
            uint32_t clip_col = make_rgba(220, 250, 255, 255);
            uint32_t clip_glow = make_rgba(0, 210, 255, 180);
            // Top-left
            fill_rect(m_pixels.data(), m_canvas_w, m_canvas_h, bx + 2, chassis_y + 2, clip_len, 2, clip_col);
            fill_rect(m_pixels.data(), m_canvas_w, m_canvas_h, bx + 2, chassis_y + 2, 2, clip_len, clip_col);
            fill_rect(m_pixels.data(), m_canvas_w, m_canvas_h, bx + 1, chassis_y + 1, clip_len + 2, 1, clip_glow);
            // Top-right
            fill_rect(m_pixels.data(), m_canvas_w, m_canvas_h, bx + bw - 2 - clip_len, chassis_y + 2, clip_len, 2, clip_col);
            fill_rect(m_pixels.data(), m_canvas_w, m_canvas_h, bx + bw - 4, chassis_y + 2, 2, clip_len, clip_col);
            fill_rect(m_pixels.data(), m_canvas_w, m_canvas_h, bx + bw - 2 - clip_len, chassis_y + 1, clip_len + 1, 1, clip_glow);
            // Bottom-left
            fill_rect(m_pixels.data(), m_canvas_w, m_canvas_h, bx + 2, chassis_y + chassis_h - 4, clip_len, 2, clip_col);
            fill_rect(m_pixels.data(), m_canvas_w, m_canvas_h, bx + 2, chassis_y + chassis_h - 2 - clip_len, 2, clip_len, clip_col);
            // Bottom-right
            fill_rect(m_pixels.data(), m_canvas_w, m_canvas_h, bx + bw - 2 - clip_len, chassis_y + chassis_h - 4, clip_len, 2, clip_col);
            fill_rect(m_pixels.data(), m_canvas_w, m_canvas_h, bx + bw - 4, chassis_y + chassis_h - 2 - clip_len, 2, clip_len, clip_col);
        }
        else if (is_hovered)
        {
            int clip_len = 5;
            uint32_t hov_col = make_rgba(255, 215, 60, 240);
            fill_rect(m_pixels.data(), m_canvas_w, m_canvas_h, bx + 2, chassis_y + 2, clip_len, 2, hov_col);
            fill_rect(m_pixels.data(), m_canvas_w, m_canvas_h, bx + 2, chassis_y + 2, 2, clip_len, hov_col);
            fill_rect(m_pixels.data(), m_canvas_w, m_canvas_h, bx + bw - 2 - clip_len, chassis_y + 2, clip_len, 2, hov_col);
            fill_rect(m_pixels.data(), m_canvas_w, m_canvas_h, bx + bw - 4, chassis_y + 2, 2, clip_len, hov_col);
        }

        // Weapon sprite inside the holster tray
        if (is_owned)
        {
            float brightness = is_current ? 1.0f : 0.90f;
            if (i < 8 && m_hd_weapons[i].width > 0)
            {
                // Soft drop shadow offset +2, +2
                draw_texture(m_pixels.data(), m_canvas_w, m_canvas_h, tray_x + 2, tray_y + 2, tray_w, tray_h, m_hd_weapons[i], 0.15f);
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

                        // Pass 1: Drop shadow
                        for (int dy = 0; dy < fh; dy++)
                        {
                            int py = oy + dy + 2;
                            if (py < 0 || py >= m_canvas_h) continue;
                            int sy = (dy * im_h) / fh;
                            const uint8_t *src_row = im->scan_line(sy);
                            for (int dx = 0; dx < fw; dx++)
                            {
                                int px = ox + dx + 2;
                                if (px < 0 || px >= m_canvas_w) continue;
                                int sx = (dx * im_w) / fw;
                                uint8_t c = src_row[sx];
                                if (c == 0) continue;
                                uint32_t dst = m_pixels[py * m_canvas_w + px];
                                uint8_t dr = static_cast<uint8_t>((dst & 0xFF) * 0.4f);
                                uint8_t dg = static_cast<uint8_t>(((dst >> 8) & 0xFF) * 0.4f);
                                uint8_t db = static_cast<uint8_t>(((dst >> 16) & 0xFF) * 0.4f);
                                m_pixels[py * m_canvas_w + px] = make_rgba(dr, dg, db, 255);
                            }
                        }

                        // Pass 2: Weapon sprite pixels
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

        // Ammo Display inside the trapezoidal window
        int ammo_w = (int)(bw * 0.58f);
        int ammo_h = (int)(chassis_h * 0.20f);
        int ammo_x = bx + (int)(bw * 0.18f);
        int ammo_y = chassis_y + (int)(chassis_h * 0.77f);

        // Dark LCD backing to eliminate any baked texture ghosting
        fill_rect(m_pixels.data(), m_canvas_w, m_canvas_h, ammo_x + 1, ammo_y + 1, ammo_w - 2, ammo_h - 2, make_rgba(4, 18, 11, 230));

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

    // Right end cap / chassis terminator (use clean plate without any gun cutouts)
    int bays_end_x = bar_x + (int)std::round((47.0f + 8 * 33.0f) * scale_x);
    if (bays_end_x < bar_x + bar_w)
    {
        int cap_w = bar_x + bar_w - bays_end_x;
        const HDTexture &cap_tex = (m_hud_bay_plate.width > 0) ? m_hud_bay_plate : m_hud_bay_empty;
        if (cap_tex.width > 0)
            draw_texture(m_pixels.data(), m_canvas_w, m_canvas_h, bays_end_x, chassis_y, cap_w, chassis_h, cap_tex, 0.85f);
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

    // 1. Clear full canvas to pure pitch black (0, 0, 0, 255)
    // Seamless letterboxing/pillarboxing for widescreen and ultra-widescreen displays
    std::fill(m_pixels.begin(), m_pixels.end(), make_rgba(0, 0, 0, 255));

    // 2. Render High-Resolution Remastered Title Screen Background (Alien on spotlight + burning ABUSE logo)
    if (m_title_bg.width > 0 && m_title_bg.height > 0)
    {
        // Fit within the active viewport preserving 4:3 / authentic title proportions
        draw_texture(m_pixels.data(), m_canvas_w, m_canvas_h, vp_x, vp_y, vp_w, vp_h, m_title_bg, 1.0f);
    }

    // 3. Determine buttons present (exact same order as menu.cpp)
    struct MenuBtnItem {
        int id;
        std::string label;
        std::string desc;
        const HDTexture *tex;
        int diff_index; // -1 if not difficulty
    };
    std::vector<MenuBtnItem> items;

    if (current_level)
        items.push_back({ ID_RETURN, "RESUME MISSION", "RETURN TO ACTIVE COMBAT", &m_btn_return, -1 });
    if (show_load_icon())
        items.push_back({ ID_LOAD_PLAYER_GAME, "LOAD GAME", "RESTORE CHECKPOINT", &m_btn_load, -1 });

    items.push_back({ ID_START_GAME, "START GAME", "NEW CAMPAIGN OPERATION", &m_btn_start, -1 });

    int cur_diff = 3;
    if (DEFINEDP(symbol_value(l_difficulty)))
    {
        if (symbol_value(l_difficulty) == l_easy) cur_diff = 0;
        else if (symbol_value(l_difficulty) == l_medium) cur_diff = 1;
        else if (symbol_value(l_difficulty) == l_hard) cur_diff = 2;
        else if (symbol_value(l_difficulty) == l_extreme) cur_diff = 3;
    }
    static const char *s_diff_titles[] = { "EASY", "MEDIUM", "HARD", "EXTREME" };
    static const char *s_diff_subtitles[] = {
        "STANDARD COMBAT SIMULATION",
        "ENHANCED AGGRESSION PROTOCOL",
        "SEVERE THREAT LEVEL",
        "MAXIMUM LETHALITY MATRIX"
    };

    if (!main_net_cfg || (main_net_cfg->state != net_configuration::SERVER && main_net_cfg->state != net_configuration::CLIENT))
    {
        items.push_back({ ID_NULL, std::string("DIFFICULTY: ") + s_diff_titles[cur_diff], s_diff_subtitles[cur_diff], &m_btn_diff, cur_diff });
    }

    items.push_back({ ID_LIGHT_OFF, "DISPLAY GAMMA", "CALIBRATE BRIGHTNESS", &m_btn_gamma, -1 });
    items.push_back({ ID_VOLUME, "AUDIO SETTINGS", "SFX & MUSIC CONTROLS", &m_btn_volume, -1 });
    if (prot)
        items.push_back({ ID_NETWORKING, "MULTIPLAYER", "LOCAL NETWORK & SERVER", &m_btn_net, -1 });
    items.push_back({ ID_QUIT, "QUIT TO DESKTOP", "TERMINATE SIMULATION", &m_btn_quit, -1 });

    // 4. Calculate exact button layout on right side matching menu.cpp
    int btn_w_native = settings.hires ? 50 : 32;
    int btn_h_native = settings.hires ? 39 : 25;
    int pad_x_native = settings.hires ? 2 : 1;
    int total_h_native = static_cast<int>(items.size()) * btn_h_native;
    int orig_y0 = (src_h - total_h_native) / 2;
    int orig_x0 = src_w - btn_w_native - pad_x_native;

    int hovered_idx = -1;

    for (size_t i = 0; i < items.size(); i++)
    {
        int by_top = vp_y + (int)std::round((orig_y0 + (int)i * btn_h_native) * scale_y);
        int by_bot = vp_y + (int)std::round((orig_y0 + ((int)i + 1) * btn_h_native) * scale_y);
        int bx_left = vp_x + (int)std::round(orig_x0 * scale_x);
        int bx_right = vp_x + (int)std::round((orig_x0 + btn_w_native) * scale_x);

        int bw = bx_right - bx_left;
        int bh = by_bot - by_top;

        // Subtle 1px padding between plates for crisp bevel definition
        int card_y = by_top + 1;
        int card_h = bh - 2;

        // Hover test in window space (covers button plate, right screen edge, and left tooltip area)
        bool is_hover = (win_mx >= bx_left - (int)(70.0f * scale_x) && win_mx <= (float)(vp_x + vp_w) &&
                         win_my >= by_top && win_my < by_bot);
        const char *force_hover = getenv("ABUSE_FORCE_HOVER_BTN");
        if (force_hover && atoi(force_hover) == (int)i)
            is_hover = true;
        if (is_hover)
            hovered_idx = static_cast<int>(i);

        float brightness = is_hover ? 1.22f : 0.95f;

        // Draw the high-res texture on the button plate
        if (items[i].tex && items[i].tex->width > 0)
        {
            draw_texture(m_pixels.data(), m_canvas_w, m_canvas_h, bx_left, card_y, bw, card_h, *items[i].tex, brightness);
        }

        // Metallic bevel frame
        uint32_t top_col = is_hover ? make_rgba(255, 255, 255, 230) : make_rgba(170, 185, 195, 140);
        uint32_t bot_col = is_hover ? make_rgba(0, 220, 255, 210) : make_rgba(25, 30, 36, 230);

        // Highlight top & left edges
        fill_rect(m_pixels.data(), m_canvas_w, m_canvas_h, bx_left, card_y, bw, 2, top_col);
        fill_rect(m_pixels.data(), m_canvas_w, m_canvas_h, bx_left, card_y, 2, card_h, top_col);

        // Shadow bottom & right edges
        fill_rect(m_pixels.data(), m_canvas_w, m_canvas_h, bx_left, card_y + card_h - 2, bw, 2, bot_col);
        fill_rect(m_pixels.data(), m_canvas_w, m_canvas_h, bx_left + bw - 2, card_y, 2, card_h, bot_col);

        // Cybernetic eye red iris effect for EXTREME difficulty
        if (items[i].diff_index == 3)
        {
            int eye_cx = bx_left + (int)(bw * 0.50f);
            int eye_cy = card_y + (int)(card_h * 0.45f);
            int r_iris = std::max(2, (int)(bw * 0.08f));
            for (int dy = -r_iris; dy <= r_iris; dy++)
            {
                for (int dx = -r_iris; dx <= r_iris; dx++)
                {
                    if (dx * dx + dy * dy <= r_iris * r_iris)
                    {
                        int px = eye_cx + dx;
                        int py = eye_cy + dy;
                        if (px >= 0 && px < m_canvas_w && py >= 0 && py < m_canvas_h)
                        {
                            uint32_t dst = m_pixels[py * m_canvas_w + px];
                            uint8_t dr = dst & 0xFF;
                            uint8_t dg = (dst >> 8) & 0xFF;
                            uint8_t db = (dst >> 16) & 0xFF;
                            m_pixels[py * m_canvas_w + px] = make_rgba(std::min(255, dr + 160),
                                                                      (uint8_t)(dg * 0.25f),
                                                                      (uint8_t)(db * 0.25f), 255);
                        }
                    }
                }
            }
        }
    }

    // 5. Draw sleek tactical tooltip badge to the left of the hovered button
    if (hovered_idx >= 0 && hovered_idx < (int)items.size())
    {
        int by_top = vp_y + (int)std::round((orig_y0 + hovered_idx * btn_h_native) * scale_y);
        int by_bot = vp_y + (int)std::round((orig_y0 + (hovered_idx + 1) * btn_h_native) * scale_y);
        int bx_left = vp_x + (int)std::round(orig_x0 * scale_x);
        int bh = by_bot - by_top;

        int badge_w = 270;
        int badge_h = 42;
        int badge_x = bx_left - badge_w - 14;
        int badge_y = by_top + (bh - badge_h) / 2;

        if (badge_x > 10)
        {
            // Dark translucent glass chassis
            fill_rect(m_pixels.data(), m_canvas_w, m_canvas_h, badge_x, badge_y, badge_w, badge_h, make_rgba(10, 14, 20, 235));
            // Cyan tech border
            draw_frame(m_pixels.data(), m_canvas_w, m_canvas_h, badge_x, badge_y, badge_w, badge_h, 0, make_rgba(0, 220, 255, 220));
            // Neon accent bar on right edge pointing toward the button
            fill_rect(m_pixels.data(), m_canvas_w, m_canvas_h, badge_x + badge_w - 3, badge_y + 1, 3, badge_h - 2, make_rgba(0, 255, 200, 255));

            draw_string(m_pixels.data(), m_canvas_w, m_canvas_h, badge_x + 12, badge_y + 7, items[hovered_idx].label.c_str(), make_rgba(255, 255, 255, 255), 1, true);
            draw_string(m_pixels.data(), m_canvas_w, m_canvas_h, badge_x + 12, badge_y + 23, items[hovered_idx].desc.c_str(), make_rgba(120, 180, 220, 200), 1, false);
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
