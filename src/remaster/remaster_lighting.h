#ifndef REMASTER_LIGHTING_H
#define REMASTER_LIGHTING_H

#include <vector>

struct GPULight
{
    float pos_x, pos_y, pos_z;
    float col_r, col_g, col_b;
    float radius;
    float intensity;
    int is_spot;
    float dir_x, dir_y;
    float spot_cutoff;
};

class RemasterLighting
{
public:
    static constexpr int MAX_LIGHTS = 32;

    static RemasterLighting &get()
    {
        static RemasterLighting instance;
        return instance;
    }

    void clear();
    void add_point_light(float norm_x, float norm_y, float norm_z,
                         float r, float g, float b,
                         float radius, float intensity);
    void add_spot_light(float norm_x, float norm_y, float norm_z,
                        float r, float g, float b,
                        float radius, float intensity,
                        float dir_x, float dir_y, float cutoff);

    // Gathers lights from the level, player aim cone, and weapon effects
    void update_frame_lights(int camera_x, int camera_y, int view_w, int view_h,
                             int player_screen_x, int player_screen_y,
                             int aim_screen_x, int aim_screen_y,
                             bool player_firing,
                             float aim_dir_x = 0.0f, float aim_dir_y = 0.0f,
                             int muzzle_screen_x = -1, int muzzle_screen_y = -1,
                             bool flashlight_on = true);

    const std::vector<GPULight> &get_lights() const { return m_lights; }

private:
    std::vector<GPULight> m_lights;
};

#endif // REMASTER_LIGHTING_H
