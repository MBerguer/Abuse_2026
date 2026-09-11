#ifndef REMASTER_ASSETS_H
#define REMASTER_ASSETS_H

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

struct HDTexture
{
    int width = 0;
    int height = 0;
    std::vector<uint32_t> pixels; // RGBA 32-bit
    std::vector<uint32_t> normal_map; // RGBA normal vectors
    std::vector<uint32_t> emission_map; // RGBA emission glow
};

class RemasterAssets
{
public:
    static RemasterAssets &get()
    {
        static RemasterAssets instance;
        return instance;
    }

    // Normal map generator using 3x3 Scharr filter on luminance
    static void generate_normal_map(const uint32_t *src_rgba, int width, int height,
                                    std::vector<uint32_t> &out_normals, float strength = 1.2f);

    // Emission map generator (extracts glowing elements)
    static void generate_emission_map(const uint32_t *src_rgba, int width, int height,
                                      std::vector<uint32_t> &out_emission);

    // Multi-plane Parallax computation
    static void get_parallax_camera(int layer_index, int cam_x, int cam_y,
                                    int &out_cam_x, int &out_cam_y);

    // Checks if HD replacement exists on disk
    bool has_hd_replacement(const std::string &asset_name) const;

private:
    std::unordered_map<std::string, HDTexture> m_texture_cache;
};

#endif // REMASTER_ASSETS_H
