#include "remaster_assets.h"
#include <cmath>
#include <algorithm>
#include <fstream>

void RemasterAssets::generate_normal_map(const uint32_t *src_rgba, int width, int height,
                                         std::vector<uint32_t> &out_normals, float strength)
{
    out_normals.resize(width * height);

    auto get_lum = [&](int px, int py) -> float {
        px = std::clamp(px, 0, width - 1);
        py = std::clamp(py, 0, height - 1);
        uint32_t c = src_rgba[py * width + px];
        uint8_t a = (c >> 24) & 0xFF;
        if (a < 10) return 0.0f;
        uint8_t r = (c >> 16) & 0xFF;
        uint8_t g = (c >> 8) & 0xFF;
        uint8_t b = c & 0xFF;
        return (0.299f * r + 0.587f * g + 0.114f * b) / 255.0f;
    };

    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < width; x++)
        {
            uint32_t c = src_rgba[y * width + x];
            uint8_t a = (c >> 24) & 0xFF;
            if (a < 10)
            {
                out_normals[y * width + x] = 0x008080FF; // Flat neutral normal with 0 alpha
                continue;
            }

            float tl = get_lum(x - 1, y - 1);
            float t  = get_lum(x,     y - 1);
            float tr = get_lum(x + 1, y - 1);
            float l  = get_lum(x - 1, y);
            float r  = get_lum(x + 1, y);
            float bl = get_lum(x - 1, y + 1);
            float b  = get_lum(x,     y + 1);
            float br = get_lum(x + 1, y + 1);

            float dx = (tr + 2.0f * r + br) - (tl + 2.0f * l + bl);
            float dy = (bl + 2.0f * b + br) - (tl + 2.0f * t + tr);

            dx *= strength;
            dy *= strength;

            float len = std::sqrt(dx * dx + dy * dy + 1.0f);
            float nx = -dx / len;
            float ny = -dy / len;
            float nz = 1.0f / len;

            uint8_t nr = static_cast<uint8_t>(std::clamp((nx * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f));
            uint8_t ng = static_cast<uint8_t>(std::clamp((ny * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f));
            uint8_t nb = static_cast<uint8_t>(std::clamp((nz * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f));

            out_normals[y * width + x] = (a << 24) | (nr << 16) | (ng << 8) | nb;
        }
    }
}

void RemasterAssets::generate_emission_map(const uint32_t *src_rgba, int width, int height,
                                          std::vector<uint32_t> &out_emission)
{
    out_emission.resize(width * height);

    for (int i = 0; i < width * height; i++)
    {
        uint32_t c = src_rgba[i];
        uint8_t a = (c >> 24) & 0xFF;
        if (a < 10)
        {
            out_emission[i] = 0;
            continue;
        }

        uint8_t r = (c >> 16) & 0xFF;
        uint8_t g = (c >> 8) & 0xFF;
        uint8_t b = c & 0xFF;

        float lum = (0.2126f * r + 0.7152f * g + 0.0722f * b) / 255.0f;
        bool isLaser = (r > 170 && g < 90 && b < 90);
        bool isPlasma = (g > 170 && r < 100);
        bool isElectric = (b > 170 && g > 130);
        bool isFire = (r > 200 && g > 150 && b < 100);

        if (lum > 0.85f || isLaser || isPlasma || isElectric || isFire)
        {
            out_emission[i] = c;
        }
        else
        {
            out_emission[i] = 0;
        }
    }
}

void RemasterAssets::get_parallax_camera(int layer_index, int cam_x, int cam_y,
                                        int &out_cam_x, int &out_cam_y)
{
    float factor = 1.0f;
    if (layer_index == 0) factor = 0.35f;      // Far background (distant structures)
    else if (layer_index == 1) factor = 0.65f; // Mid background
    else if (layer_index == 2) factor = 1.0f;  // Playfield
    else if (layer_index == 3) factor = 1.25f; // Foreground elements

    out_cam_x = static_cast<int>(cam_x * factor);
    out_cam_y = static_cast<int>(cam_y * factor);
}

bool RemasterAssets::has_hd_replacement(const std::string &asset_name) const
{
    // Checks if HD replacement file exists in data/hd/
    std::string path = "data/hd/" + asset_name + ".png";
    std::ifstream file(path);
    return file.good();
}
