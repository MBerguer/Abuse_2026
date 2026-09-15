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

    GLuint m_texture = 0;
    GLuint m_prog = 0;
    int m_canvas_w = 960;
    int m_canvas_h = 540;
    std::vector<uint32_t> m_pixels;
    bool m_initialized = false;
};

#endif // REMASTER_HUD_H
