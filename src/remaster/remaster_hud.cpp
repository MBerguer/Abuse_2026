#include "remaster_hud.h"
#include "remaster_config.h"
#include "remaster_lighting.h"
#include "remaster_gl.h"
#include "remaster_shaders.h"
#include "common.h"
#include "image.h"
#include "palette.h"
#include "jwindow.h"
#include "event.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>

extern unsigned char fnt6x13[192 * 104];
extern WindowManager *wm;
extern palette *pal;
extern int xres, yres;

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

    draw_row("[F11] Remaster Mode:", cfg.enabled, "Master GPU Pipeline");
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
    draw_string(m_pixels.data(), m_canvas_w, m_canvas_h, panel_x + 16, cur_y + 9, "CONTROLS: [F8] Gamma  |  [F11] Remaster  |  [F12] Close", text_cyan, 1, false);
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

void RemasterHUD::render(int window_w, int window_h, int vp_x, int vp_y, int vp_w, int vp_h, int src_w, int src_h)
{
    if (!m_initialized)
        return;

    auto &cfg = RemasterConfig::get();

    // Update notification timer
    float alpha = 1.0f;
    if (cfg.notification_timer > 0.0f)
    {
        cfg.notification_timer -= 0.01667f;
        if (cfg.notification_timer < 0.5f)
            alpha = std::clamp(cfg.notification_timer / 0.5f, 0.0f, 1.0f);
    }

    bool need_notification = (cfg.notification_timer > 0.0f && !cfg.notification_text.empty());
    bool need_dashboard = cfg.show_hud_overlay;
    bool need_cursor = (cfg.enabled && wm && wm->has_mouse() && wm->GetMouseVisual() != nullptr);

    if (!need_notification && !need_dashboard && !need_cursor)
        return;

    // Clear canvas
    std::fill(m_pixels.begin(), m_pixels.end(), 0);

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
