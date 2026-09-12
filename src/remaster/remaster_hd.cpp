#include "remaster_hd.h"
#include "game.h"
#include "level.h"
#include "loader2.h"
#include "file_utils.h"

#include <iostream>
#include <fstream>
#include <cmath>
#include <cstring>
#include <algorithm>

RemasterHD::RemasterHD()
    : m_ready(false),
      m_loaded_tiles(0),
      m_albedo_array(0),
      m_normal_array(0),
      m_orm_array(0),
      m_emission_array(0),
      m_tile_grid_tex(0),
      m_cam_x(0.0f), m_cam_y(0.0f),
      m_view_w(320.0f), m_view_h(200.0f),
      m_tile_w(30.0f), m_tile_h(15.0f),
      m_grid_ox(0), m_grid_oy(0),
      m_grid_w(0), m_grid_h(0)
{
    std::memset(m_tile_presence, 0, sizeof(m_tile_presence));
    m_grid_buffer.resize(64 * 64, 0);
}

RemasterHD::~RemasterHD()
{
    cleanup();
}

void RemasterHD::cleanup()
{
    if (m_albedo_array) { glDeleteTextures(1, &m_albedo_array); m_albedo_array = 0; }
    if (m_normal_array) { glDeleteTextures(1, &m_normal_array); m_normal_array = 0; }
    if (m_orm_array)    { glDeleteTextures(1, &m_orm_array);    m_orm_array = 0; }
    if (m_emission_array) { glDeleteTextures(1, &m_emission_array); m_emission_array = 0; }
    if (m_tile_grid_tex)  { glDeleteTextures(1, &m_tile_grid_tex);  m_tile_grid_tex = 0; }
    m_ready = false;
    m_loaded_tiles = 0;
}

bool RemasterHD::has_tile(int tile_id) const
{
    if (tile_id >= 0 && tile_id < 512)
        return m_tile_presence[tile_id] != 0;
    return false;
}

bool RemasterHD::load_binary_pack(const std::string &path)
{
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return false;

    char sig[8];
    if (fread(sig, 1, 8, f) != 8 || std::memcmp(sig, "ABUSEPBR", 8) != 0)
    {
        fclose(f);
        return false;
    }

    uint32_t version = 0, slice_w = 0, slice_h = 0, num_slices = 0;
    if (fread(&version, 4, 1, f) != 1 ||
        fread(&slice_w, 4, 1, f) != 1 ||
        fread(&slice_h, 4, 1, f) != 1 ||
        fread(&num_slices, 4, 1, f) != 1)
    {
        fclose(f);
        return false;
    }

    if (slice_w != 128 || slice_h != 128 || num_slices > 512)
    {
        fclose(f);
        return false;
    }

    // Allocate OpenGL 2D Texture Arrays (512 slices of 128x128 RGBA8)
    auto create_array = [](GLuint &tex, GLenum format) {
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D_ARRAY, tex);
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, format, 128, 128, 512, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    };

    create_array(m_albedo_array, GL_RGBA8);
    create_array(m_normal_array, GL_RGBA8);
    create_array(m_orm_array, GL_RGBA8);
    create_array(m_emission_array, GL_RGBA8);

    const size_t bytes_per_map = 128 * 128 * 4;
    std::vector<uint8_t> buf(bytes_per_map);

    m_loaded_tiles = 0;
    for (uint32_t tid = 0; tid < num_slices; tid++)
    {
        uint8_t has_pbr = 0;
        if (fread(&has_pbr, 1, 1, f) != 1) break;

        if (has_pbr)
        {
            m_tile_presence[tid] = 1;
            m_loaded_tiles++;

            // 1. Albedo
            if (fread(buf.data(), 1, bytes_per_map, f) == bytes_per_map)
            {
                glBindTexture(GL_TEXTURE_2D_ARRAY, m_albedo_array);
                glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, tid, 128, 128, 1, GL_RGBA, GL_UNSIGNED_BYTE, buf.data());
            }

            // 2. Normal
            if (fread(buf.data(), 1, bytes_per_map, f) == bytes_per_map)
            {
                glBindTexture(GL_TEXTURE_2D_ARRAY, m_normal_array);
                glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, tid, 128, 128, 1, GL_RGBA, GL_UNSIGNED_BYTE, buf.data());
            }

            // 3. ORM
            if (fread(buf.data(), 1, bytes_per_map, f) == bytes_per_map)
            {
                glBindTexture(GL_TEXTURE_2D_ARRAY, m_orm_array);
                glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, tid, 128, 128, 1, GL_RGBA, GL_UNSIGNED_BYTE, buf.data());
            }

            // 4. Emission
            if (fread(buf.data(), 1, bytes_per_map, f) == bytes_per_map)
            {
                glBindTexture(GL_TEXTURE_2D_ARRAY, m_emission_array);
                glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, tid, 128, 128, 1, GL_RGBA, GL_UNSIGNED_BYTE, buf.data());
            }
        }
        else
        {
            m_tile_presence[tid] = 0;
        }
    }

    fclose(f);

    // Generate mipmaps for smooth filtering at 4K / sub-pixel zoom
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_albedo_array);
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_normal_array);
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_orm_array);
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_emission_array);
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);

    // Create 64x64 Tile Grid Texture (GL_R16I)
    glGenTextures(1, &m_tile_grid_tex);
    glBindTexture(GL_TEXTURE_2D, m_tile_grid_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16I, 64, 64, 0, GL_RED_INTEGER, GL_SHORT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

    return true;
}

bool RemasterHD::init()
{
    if (m_ready) return true;

    std::vector<std::string> candidate_paths;

    char *prefix = get_filename_prefix();
    if (prefix && prefix[0])
    {
        candidate_paths.push_back(std::string(prefix) + "/hd/tiles_pbr.bin");
        candidate_paths.push_back(std::string(prefix) + "/tiles_pbr.bin");
    }

    char *save_pfx = get_save_filename_prefix();
    if (save_pfx && save_pfx[0])
    {
        candidate_paths.push_back(std::string(save_pfx) + "/data/hd/tiles_pbr.bin");
        candidate_paths.push_back(std::string(save_pfx) + "/hd/tiles_pbr.bin");
    }

    candidate_paths.push_back("data/hd/tiles_pbr.bin");
    candidate_paths.push_back("abuse.app/Contents/Resources/data/hd/tiles_pbr.bin");
    candidate_paths.push_back("../Resources/data/hd/tiles_pbr.bin");

    for (const auto &path : candidate_paths)
    {
        if (load_binary_pack(path))
        {
            m_ready = true;
            std::cout << "[RemasterHD] Successfully initialized Next-Gen 4-Channel PBR Pipeline ("
                      << m_loaded_tiles << " HD tiles loaded from " << path << ")." << std::endl;
            return true;
        }
    }

    std::cerr << "[RemasterHD] Warning: tiles_pbr.bin not found. HD PBR pipeline disabled." << std::endl;
    return false;
}

void RemasterHD::update_frame_data(int cam_x, int cam_y, int view_w, int view_h,
                                   int ftile_w, int ftile_h,
                                   int fg_w, int fg_h)
{
    m_cam_x = static_cast<float>(cam_x);
    m_cam_y = static_cast<float>(cam_y);
    m_view_w = static_cast<float>(view_w);
    m_view_h = static_cast<float>(view_h);
    m_tile_w = static_cast<float>(ftile_w > 0 ? ftile_w : 30);
    m_tile_h = static_cast<float>(ftile_h > 0 ? ftile_h : 15);

    if (!m_ready || !current_level) return;

    level *lev = current_level;

    // Determine visible grid tile coordinates
    int x1 = static_cast<int>(std::floor(m_cam_x / m_tile_w)) - 1;
    int y1 = static_cast<int>(std::floor(m_cam_y / m_tile_h)) - 1;
    int x2 = static_cast<int>(std::ceil((m_cam_x + m_view_w) / m_tile_w)) + 1;
    int y2 = static_cast<int>(std::ceil((m_cam_y + m_view_h) / m_tile_h)) + 1;

    m_grid_ox = x1;
    m_grid_oy = y1;
    m_grid_w = std::clamp(x2 - x1 + 1, 1, 64);
    m_grid_h = std::clamp(y2 - y1 + 1, 1, 64);

    if (m_grid_buffer.size() < static_cast<size_t>(m_grid_w * m_grid_h))
        m_grid_buffer.resize(m_grid_w * m_grid_h);

    for (int gy = 0; gy < m_grid_h; gy++)
    {
        int map_y = y1 + gy;
        uint16_t *line = (map_y >= 0 && map_y < fg_h) ? lev->get_fgline(map_y) : nullptr;

        for (int gx = 0; gx < m_grid_w; gx++)
        {
            int map_x = x1 + gx;
            int tile_id = 0;

            if (line && map_x >= 0 && map_x < fg_w)
            {
                uint16_t raw_code = *(line + map_x);
                tile_id = fgvalue(raw_code);
            }

            m_grid_buffer[gy * m_grid_w + gx] = static_cast<int16_t>(tile_id);
        }
    }

    // Upload to OpenGL tile grid texture
    glBindTexture(GL_TEXTURE_2D, m_tile_grid_tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_grid_w, m_grid_h, GL_RED_INTEGER, GL_SHORT, m_grid_buffer.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}

void RemasterHD::set_object_rects(const std::vector<ObjectRect> &rects)
{
    m_object_rects = rects;
}

void RemasterHD::bind_pbr_textures(int albedo_unit, int normal_unit, int orm_unit, int emission_unit, int grid_unit)
{
    if (!m_ready) return;

    glActiveTexture(GL_TEXTURE0 + albedo_unit);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_albedo_array);

    glActiveTexture(GL_TEXTURE0 + normal_unit);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_normal_array);

    glActiveTexture(GL_TEXTURE0 + orm_unit);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_orm_array);

    glActiveTexture(GL_TEXTURE0 + emission_unit);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_emission_array);

    glActiveTexture(GL_TEXTURE0 + grid_unit);
    glBindTexture(GL_TEXTURE_2D, m_tile_grid_tex);
}
