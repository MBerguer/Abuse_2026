#ifndef REMASTER_HD_H
#define REMASTER_HD_H

#include <vector>
#include <cstdint>
#include <string>

#ifdef __APPLE__
#ifndef GL_SILENCE_DEPRECATION
#define GL_SILENCE_DEPRECATION
#endif
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif

struct ObjectRect
{
    float u1, v1, u2, v2;
    int otype;
};

class RemasterHD
{
public:
    static RemasterHD &get()
    {
        static RemasterHD instance;
        return instance;
    }

    bool init();
    void cleanup();

    // Updates visible tile grid from level data each frame
    void update_frame_data(int cam_x, int cam_y, int view_w, int view_h,
                           int ftile_w, int ftile_h,
                           int fg_w, int fg_h);

    void set_object_rects(const std::vector<ObjectRect> &rects);

    void bind_pbr_textures(int albedo_unit, int normal_unit, int orm_unit, int emission_unit, int grid_unit);

    bool is_ready() const { return m_ready; }
    int get_loaded_tile_count() const { return m_loaded_tiles; }
    bool has_tile(int tile_id) const;

    float get_cam_x() const { return m_cam_x; }
    float get_cam_y() const { return m_cam_y; }
    float get_view_w() const { return m_view_w; }
    float get_view_h() const { return m_view_h; }
    float get_tile_w() const { return m_tile_w; }
    float get_tile_h() const { return m_tile_h; }
    int get_grid_ox() const { return m_grid_ox; }
    int get_grid_oy() const { return m_grid_oy; }
    int get_grid_w() const { return m_grid_w; }
    int get_grid_h() const { return m_grid_h; }

    const std::vector<ObjectRect> &get_object_rects() const { return m_object_rects; }
    GLuint get_tile_grid_tex() const { return m_tile_grid_tex; }

private:
    RemasterHD();
    ~RemasterHD();

    bool load_binary_pack(const std::string &path);

    bool m_ready;
    int m_loaded_tiles;

    // OpenGL 2D Texture Arrays (512 slices each of 128x128 RGBA8)
    GLuint m_albedo_array;
    GLuint m_normal_array;
    GLuint m_orm_array;
    GLuint m_emission_array;

    // Tile Grid 2D Integer Texture (64x64 GL_R16I)
    GLuint m_tile_grid_tex;

    uint8_t m_tile_presence[512];

    float m_cam_x, m_cam_y;
    float m_view_w, m_view_h;
    float m_tile_w, m_tile_h;
    int m_grid_ox, m_grid_oy;
    int m_grid_w, m_grid_h;

    std::vector<int16_t> m_grid_buffer;
    std::vector<ObjectRect> m_object_rects;
};

#endif // REMASTER_HD_H
