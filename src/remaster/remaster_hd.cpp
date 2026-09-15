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
#include <set>

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
    m_grid_buffer.resize(64 * 64 * 4, 0);
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

    // Create 64x64 Tile Grid Texture (GL_RGBA16I)
    glGenTextures(1, &m_tile_grid_tex);
    glBindTexture(GL_TEXTURE_2D, m_tile_grid_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16I, 64, 64, 0, GL_RGBA_INTEGER, GL_SHORT, nullptr);
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
    m_tile_w = static_cast<float>(ftile_w > 0 ? ftile_w : (f_wid > 0 ? f_wid : 16));
    m_tile_h = static_cast<float>(ftile_h > 0 ? ftile_h : (f_hi > 0 ? f_hi : 16));

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

    if (m_grid_buffer.size() < static_cast<size_t>(m_grid_w * m_grid_h * 4))
        m_grid_buffer.resize(m_grid_w * m_grid_h * 4);

    for (int gy = 0; gy < m_grid_h; gy++)
    {
        int map_y = y1 + gy;
        uint16_t *line_fg = (map_y >= 0 && map_y < lev->foreground_height()) ? lev->get_fgline(map_y) : nullptr;
        uint16_t *line_bg = (map_y >= 0 && map_y < lev->background_height()) ? lev->get_bgline(map_y) : nullptr;

        for (int gx = 0; gx < m_grid_w; gx++)
        {
            int map_x = x1 + gx;
            int tile_id_fg = 0;
            int tile_id_bg = 0;
            int sem_type = 0;
            int floor_y = 0;

            if (line_fg && map_x >= 0 && map_x < lev->foreground_width())
            {
                uint16_t raw_code = *(line_fg + map_x);
                tile_id_fg = fgvalue(raw_code);
            }
            if (line_bg && map_x >= 0 && map_x < lev->background_width())
            {
                uint16_t raw_code = *(line_bg + map_x);
                tile_id_bg = bgvalue(raw_code);
            }

            // Semantic Classification:
            // 0 = Deep inaccessible background / outer abyss (AZUL)
            // 1 = Room interior alcove wall / perforated grille mesh (ROJO)
            // 2 = Solid vertical pillar / structural column (VERDE)
            // 3 = Walkable flat floor / catwalk platform (VERDE)
            // 4 = Walkable ramp slope down-right (VERDE)
            // 5 = Walkable ramp slope down-left (VERDE)
            // 6 = Ceiling wedge / under-stair slope (VERDE)
            // 7 = Solid subterranean foundation / continuous wall (VERDE)
            if (tile_id_fg == 10 || tile_id_fg == 11 || tile_id_fg == 12)
            {
                sem_type = 2; // Vertical framing pillar & column base
            }
            else if (tile_id_fg == 388 || tile_id_fg == 394 || tile_id_fg == 412)
            {
                sem_type = 1; // Interior room perforated mesh wall (ROJO)
            }
            else if (tile_id_fg == 23 || tile_id_fg == 88 || tile_id_fg == 89)
            {
                sem_type = 7; // Under-ramp structural truss / foundation
            }
            else if (the_game && tile_id_fg > 0)
            {
                foretile *f = the_game->get_fg(tile_id_fg);
                if (f && f->points && f->points->tot > 0)
                {
                    int tot = f->points->tot;
                    uint8_t *pts = f->points->data;

                    if (tot == 4)
                    {
                        // 4-point polygon: either a walkable floor ramp or a ceiling wedge
                        // Walkable floor ramps have solid bottom: both (0,14) and (29,14)
                        bool has_bot_left  = false; // (0, 14)
                        bool has_bot_right = false; // (29, 14)
                        bool has_top_left  = false; // (0, 0)
                        bool has_top_right = false; // (29, 0)
                        for (int i = 0; i < tot; i++)
                        {
                            int px = pts[i*2];
                            int py = pts[i*2+1];
                            if (px == 0  && py >= 13) has_bot_left  = true;
                            if (px >= 28 && py >= 13) has_bot_right = true;
                            if (px == 0  && py <= 1)  has_top_left  = true;
                            if (px >= 28 && py <= 1)  has_top_right = true;
                        }

                        if (has_bot_left && has_bot_right)
                        {
                            // Walkable Floor Ramp!
                            if (has_top_left)
                            {
                                sem_type = 4; // Ramp down-right (e.g. tile 18, 20, 166)
                                floor_y = 0;
                            }
                            else if (has_top_right)
                            {
                                sem_type = 5; // Ramp down-left (e.g. tile 19, 22, 164)
                                floor_y = 0;
                            }
                            else
                            {
                                sem_type = 7; // Solid foundation
                            }
                        }
                        else if (has_top_left && has_top_right)
                        {
                            // Ceiling Wedge! (e.g. tiles 45, 46, 63, 64, 163, 165)
                            // Solid ceiling girder above slope, air under stairs below slope.
                            sem_type = 6;
                            if (has_bot_left)
                                floor_y = 1; // Slope from (0, 14) to (29, 0) (tile 63)
                            else
                                floor_y = 2; // Slope from (0, 0) to (29, 14) (tile 64)
                        }
                        else
                        {
                            sem_type = 7; // Solid subterranean fill
                        }
                    }
                    else if (tot == 5)
                    {
                        // 5-point polygon: check if top surface forms a flat floor
                        // All Abuse floor tiles (14, 15, 26, 47, 82, 83, 85, etc.) have (0,2) to (29,2)
                        bool has_floor_surf = false;
                        int surf_y = 2;
                        for (int i = 0; i < tot - 1; i++)
                        {
                            int y1 = pts[i*2+1];
                            int y2 = pts[(i+1)*2+1];
                            if (y1 == y2 && y1 >= 1 && y1 <= 5)
                            {
                                has_floor_surf = true;
                                surf_y = y1;
                                break;
                            }
                        }

                        if (has_floor_surf || (f->ylevel > 0 && f->ylevel < 15))
                        {
                            sem_type = 3; // Walkable flat floor platform!
                            floor_y = has_floor_surf ? surf_y : f->ylevel;
                        }
                        else
                        {
                            sem_type = 7; // Solid subterranean foundation block (e.g. tile 28, 58)
                        }
                    }
                    else
                    {
                        if (f->ylevel > 0 && f->ylevel < 15)
                        {
                            sem_type = 3;
                            floor_y = f->ylevel;
                        }
                        else
                        {
                            sem_type = 7; // Solid subterranean foundation
                        }
                    }
                }
                else
                {
                    sem_type = 1; // Middleground room wall mesh
                }
            }
            else
            {
                sem_type = 0; // Deep background / abyss (AZUL)
            }

            size_t idx = (gy * m_grid_w + gx) * 4;
            m_grid_buffer[idx + 0] = static_cast<int16_t>(tile_id_fg);
            m_grid_buffer[idx + 1] = static_cast<int16_t>(tile_id_bg);
            m_grid_buffer[idx + 2] = static_cast<int16_t>(sem_type);
            m_grid_buffer[idx + 3] = static_cast<int16_t>(floor_y);
        }
    }

    // Upload to OpenGL tile grid texture

    glBindTexture(GL_TEXTURE_2D, m_tile_grid_tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_grid_w, m_grid_h, GL_RGBA_INTEGER, GL_SHORT, m_grid_buffer.data());
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
