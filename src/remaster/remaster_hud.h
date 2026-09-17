#ifndef REMASTER_HUD_H
#define REMASTER_HUD_H

#include <cstdint>
#include <vector>
#include <string>

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#include <GL/gl3.h>
#endif

class RemasterHUD
{
public:
    static RemasterHUD &get()
    {
        static RemasterHUD instance;
        return instance;
    }

    bool init();
    void render(int window_w, int window_h, int vp_x = 0, int vp_y = 0, int vp_w = 0, int vp_h = 0, int src_w = 0, int src_h = 0);
    void cleanup();

private:
    RemasterHUD() = default;
    ~RemasterHUD() = default;

    void update_canvas(int w, int h);
    void draw_notification(float alpha);
    void draw_dashboard();
    void draw_pos_debug();
    void draw_cursor(int dst_x0, int dst_y0, int dst_w, int dst_h, void *im_ptr, void *pal_ptr);

    // High-Resolution Status Bar and 7-Segment Displays
    void draw_hud_statusbar(int window_w, int window_h, int vp_x, int vp_y, int vp_w, int vp_h, int src_w, int src_h);
    void draw_seven_segment_digit(uint32_t *buf, int bw, int bh, int x, int y, int digit, int seg_w, int seg_h, int thickness, uint32_t on_color, uint32_t off_color, bool glow = true);
    void draw_seven_segment_number(uint32_t *buf, int bw, int bh, int x, int y, int number, int num_digits, int seg_w, int seg_h, int thickness, uint32_t on_color, uint32_t off_color, bool glow = true, bool pad_zeroes = true);

    // High-Resolution Main Menu
    void draw_main_menu(int window_w, int window_h, int vp_x, int vp_y, int vp_w, int vp_h, int src_w, int src_h);

    struct HDTexture {
        int width = 0;
        int height = 0;
        std::vector<uint32_t> pixels;
    };
    bool load_texture_rgba(const std::string &rel_path, HDTexture &out_tex);
    void load_all_assets();
    void draw_texture(uint32_t *buf, int bw, int bh, int dst_x, int dst_y, int dst_w, int dst_h, const HDTexture &img, float brightness = 1.0f);

    HDTexture m_hd_weapons[8];
    HDTexture m_hud_bay[8];
    HDTexture m_hud_bay_plate;
    HDTexture m_hud_bay_empty;
    HDTexture m_hud_top_grate;
    bool m_assets_loaded = false;

    GLuint m_texture = 0;
    GLuint m_prog = 0;
    int m_canvas_w = 960;
    int m_canvas_h = 540;
    std::vector<uint32_t> m_pixels;
    bool m_initialized = false;
};

#endif // REMASTER_HUD_H
