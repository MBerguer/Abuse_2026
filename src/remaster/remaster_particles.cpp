#include "common.h"
#include "remaster_particles.h"
#include "remaster_gl.h"
#include "remaster_config.h"
#include "level.h"
#include "game.h"
#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <iostream>

extern level *current_level;

static inline int remap_pt_x(int val, int tw)
{
    return (val == 0 ? -1 : (val == tw - 1 ? tw + 1 : val));
}

static inline int remap_pt_y(int val, int th)
{
    return (val == 0 ? -1 : (val == th - 1 ? th + 1 : val));
}

static bool is_point_solid(float wx, float wy)
{
    if (!current_level || !the_game) return false;
    int tw = the_game->ftile_width();
    int th = the_game->ftile_height();
    if (tw <= 0) tw = 16;
    if (th <= 0) th = 16;

    int tx = (int)wx / tw;
    int ty = (int)wy / th;
    if (tx < 0 || ty < 0 || tx >= current_level->foreground_width() || ty >= current_level->foreground_height())
        return true;

    uint16_t *line = current_level->get_fgline(ty);
    if (!line) return false;
    int block = line[tx] & 0x7FFF;
    if (block <= BLACK) return false;

    foretile *f = the_game->get_fg(block);
    if (!f || !f->points || f->points->tot < 2)
        return false;

    int total = f->points->tot;
    uint8_t *bdat = f->points->data;
    int xo = tx * tw;
    int yo = ty * th;

    int crossings = 0;
    for (int j = 0; j < total - 1; j++)
    {
        int xp1 = xo + remap_pt_x(bdat[0], tw); bdat++;
        int yp1 = yo + remap_pt_y(bdat[0], th); bdat++;
        int xp2 = xo + remap_pt_x(bdat[0], tw);
        int yp2 = yo + remap_pt_y(bdat[1], th);

        if (((yp1 <= wy) && (yp2 > wy)) || ((yp2 <= wy) && (yp1 > wy)))
        {
            float vt = (wy - (float)yp1) / (float)(yp2 - yp1);
            if (wx < (float)xp1 + vt * (float)(xp2 - xp1))
                crossings++;
        }
    }
    return (crossings & 1) != 0;
}

static float frand01()
{
    return (float)std::rand() / (float)RAND_MAX;
}

static float frand_range(float min_v, float max_v)
{
    return min_v + frand01() * (max_v - min_v);
}

bool RemasterParticles::query_surface_normal(float x, float y, float &out_nx, float &out_ny, float search_dist)
{
    if (!current_level || !the_game)
        return false;

    int tw = the_game->ftile_width();
    int th = the_game->ftile_height();
    if (tw <= 0) tw = 16;
    if (th <= 0) th = 16;

    int min_bx = std::max(0, (int)((x - search_dist) / tw));
    int max_bx = std::min((int)current_level->foreground_width() - 1, (int)((x + search_dist) / tw));
    int min_by = std::max(0, (int)((y - search_dist) / th));
    int max_by = std::min((int)current_level->foreground_height() - 1, (int)((y + search_dist) / th));

    float best_dist_sq = search_dist * search_dist;
    float best_nx = 0.0f, best_ny = -1.0f;
    bool found = false;

    for (int by = min_by; by <= max_by; by++)
    {
        for (int bx = min_bx; bx <= max_bx; bx++)
        {
            int block = the_game->GetMapFg(ivec2(bx, by));
            if (block <= BLACK)
                continue;

            foretile *f = the_game->get_fg(block);
            if (!f || !f->points || f->points->tot < 2)
                continue;

            int total = f->points->tot;
            uint8_t *bdat = f->points->data;
            uint8_t *ins = f->points->inside;
            int xo = bx * tw;
            int yo = by * th;

            for (int j = 0; j < total - 1; j++, ins++)
            {
                int xp1 = xo + remap_pt_x(bdat[0], tw); bdat++;
                int yp1 = yo + remap_pt_y(bdat[0], th); bdat++;
                int xp2 = xo + remap_pt_x(bdat[0], tw);
                int yp2 = yo + remap_pt_y(bdat[1], th);

                float seg_dx = (float)(xp2 - xp1);
                float seg_dy = (float)(yp2 - yp1);
                float seg_len_sq = seg_dx * seg_dx + seg_dy * seg_dy;
                if (seg_len_sq < 0.001f)
                    continue;

                float t = ((x - xp1) * seg_dx + (y - yp1) * seg_dy) / seg_len_sq;
                t = std::clamp(t, 0.0f, 1.0f);
                float qx = (float)xp1 + t * seg_dx;
                float qy = (float)yp1 + t * seg_dy;

                float diff_x = x - qx;
                float diff_y = y - qy;
                float d_sq = diff_x * diff_x + diff_y * diff_y;

                if (d_sq < best_dist_sq)
                {
                    best_dist_sq = d_sq;
                    float d = std::sqrt(d_sq);
                    if (d > 0.05f)
                    {
                        best_nx = diff_x / d;
                        best_ny = diff_y / d;
                    }
                    else
                    {
                        float nx = -seg_dy;
                        float ny = seg_dx;
                        if (*ins == 0) { nx = -nx; ny = -ny; }
                        float nlen = std::hypot(nx, ny);
                        if (nlen > 0.001f)
                        {
                            best_nx = nx / nlen;
                            best_ny = ny / nlen;
                        }
                    }
                    found = true;
                }
            }
        }
    }

    if (found)
    {
        out_nx = best_nx;
        out_ny = best_ny;
        return true;
    }
    return false;
}

RemasterParticles::RemasterParticles()
    : m_last_explo_tick(0),
      m_last_explo_x(0.0f),
      m_last_explo_y(0.0f),
      m_prog(0),
      m_vao_smoke(0),
      m_vbo_smoke(0),
      m_vao_additive(0),
      m_vbo_additive(0)
{
    m_particles.reserve(MAX_PARTICLES);
    m_light_bursts.reserve(64);
}

RemasterParticles::~RemasterParticles()
{
    shutdown();
}

bool RemasterParticles::compile_shader()
{
    const char *vs_src = R"(#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec4 aColor;
layout (location = 2) in vec2 aTexCoords;
layout (location = 3) in int aType;

out vec4 vColor;
out vec2 vTexCoords;
flat out int vType;

uniform vec2 u_cam_pos;
uniform vec2 u_view_size;
uniform float u_flip_y;

void main()
{
    vColor = aColor;
    vTexCoords = aTexCoords;
    vType = aType;

    vec2 screen = (aPos - u_cam_pos) / u_view_size;
    float ndc_x = screen.x * 2.0 - 1.0;
    float ndc_y = (u_flip_y > 0.5) ? (screen.y * 2.0 - 1.0) : (1.0 - screen.y * 2.0);
    gl_Position = vec4(ndc_x, ndc_y, 0.0, 1.0);
}
)";

    const char *fs_src = R"(#version 330 core
in vec4 vColor;
in vec2 vTexCoords;
flat in int vType;

out vec4 FragColor;

void main()
{
    if (vType == 0)
    {
        // Solid/tapered spark ribbon
        FragColor = vColor;
    }
    else if (vType == 1)
    {
        // Soft radial billboard (smoke puff, fireball)
        float d = length(vTexCoords - vec2(0.5)) * 2.0;
        if (d > 1.0) discard;
        float alpha = smoothstep(1.0, 0.0, d);
        FragColor = vec4(vColor.rgb, vColor.a * alpha);
    }
    else if (vType == 2)
    {
        // Shockwave hollow ring: sharp inner & outer radius with feather
        float d = length(vTexCoords - vec2(0.5)) * 2.0;
        float ring = smoothstep(0.08, 0.0, abs(d - 0.90));
        if (ring <= 0.005) discard;
        FragColor = vec4(vColor.rgb * ring, vColor.a * ring);
    }
    else
    {
        FragColor = vColor;
    }
}
)";

    if (!RemasterGL::compile_shader(m_prog, vs_src, fs_src))
    {
        std::cerr << "[RemasterParticles] Failed to compile particle shader\n";
        return false;
    }
    return true;
}

bool RemasterParticles::init()
{
    if (m_prog != 0)
        return true;

    if (!compile_shader())
        return false;

    // 1. VAO & VBO for alpha-blended smoke
    glGenVertexArrays(1, &m_vao_smoke);
    glGenBuffers(1, &m_vbo_smoke);
    glBindVertexArray(m_vao_smoke);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo_smoke);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(ParticleVertex), (void *)offsetof(ParticleVertex, x));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(ParticleVertex), (void *)offsetof(ParticleVertex, r));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(ParticleVertex), (void *)offsetof(ParticleVertex, u));
    glEnableVertexAttribArray(3);
    glVertexAttribIPointer(3, 1, GL_INT, sizeof(ParticleVertex), (void *)offsetof(ParticleVertex, type));

    // 2. VAO & VBO for additive particles (sparks, fireballs, shockwaves, debris)
    glGenVertexArrays(1, &m_vao_additive);
    glGenBuffers(1, &m_vbo_additive);
    glBindVertexArray(m_vao_additive);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo_additive);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(ParticleVertex), (void *)offsetof(ParticleVertex, x));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(ParticleVertex), (void *)offsetof(ParticleVertex, r));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(ParticleVertex), (void *)offsetof(ParticleVertex, u));
    glEnableVertexAttribArray(3);
    glVertexAttribIPointer(3, 1, GL_INT, sizeof(ParticleVertex), (void *)offsetof(ParticleVertex, type));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    return true;
}

void RemasterParticles::shutdown()
{
    if (m_vbo_smoke) { glDeleteBuffers(1, &m_vbo_smoke); m_vbo_smoke = 0; }
    if (m_vao_smoke) { glDeleteVertexArrays(1, &m_vao_smoke); m_vao_smoke = 0; }
    if (m_vbo_additive) { glDeleteBuffers(1, &m_vbo_additive); m_vbo_additive = 0; }
    if (m_vao_additive) { glDeleteVertexArrays(1, &m_vao_additive); m_vao_additive = 0; }
    if (m_prog) { glDeleteProgram(m_prog); m_prog = 0; }

    clear();
}

void RemasterParticles::clear()
{
    m_particles.clear();
    m_light_bursts.clear();
}

void RemasterParticles::add_spark(float x, float y, float vx, float vy, float life, int palette)
{
    if (m_particles.size() >= MAX_PARTICLES)
        return;

    Particle p{};
    p.x = x;
    p.y = y;
    p.prev_x = x - vx * 0.45f;
    p.prev_y = y - vy * 0.45f;
    p.vx = vx;
    p.vy = vy;
    p.life = life;
    p.max_life = life;
    p.type = PARTICLE_SPARK;
    p.palette = palette;
    p.bounce_count = 0;
    p.size = 1.0f;
    p.start_size = 1.0f;
    p.end_size = 1.0f;
    p.r = 1.0f; p.g = 1.0f; p.b = 1.0f; p.a = 1.0f;

    m_particles.push_back(p);
}

void RemasterParticles::add_smoke(float x, float y, float vx, float vy, float start_sz, float end_sz, float life, float r, float g, float b, float a)
{
    if (m_particles.size() >= MAX_PARTICLES)
        return;

    Particle p{};
    p.x = x;
    p.y = y;
    p.prev_x = x;
    p.prev_y = y;
    p.vx = vx;
    p.vy = vy;
    p.life = life;
    p.max_life = life;
    p.type = PARTICLE_SMOKE;
    p.palette = 0;
    p.bounce_count = 0;
    p.size = start_sz;
    p.start_size = start_sz;
    p.end_size = end_sz;
    p.r = r; p.g = g; p.b = b; p.a = a;

    m_particles.push_back(p);
}

void RemasterParticles::add_shockwave(float x, float y, float max_radius, float life, float r, float g, float b)
{
    if (m_particles.size() >= MAX_PARTICLES)
        return;

    Particle p{};
    p.x = x;
    p.y = y;
    p.prev_x = x;
    p.prev_y = y;
    p.vx = 0.0f;
    p.vy = 0.0f;
    p.life = life;
    p.max_life = life;
    p.type = PARTICLE_SHOCKWAVE;
    p.palette = 0;
    p.bounce_count = 0;
    p.size = 4.0f;
    p.start_size = 4.0f;
    p.end_size = max_radius;
    p.r = r; p.g = g; p.b = b; p.a = 1.0f;

    m_particles.push_back(p);
}

void RemasterParticles::add_debris(float x, float y, float vx, float vy, float life)
{
    if (m_particles.size() >= MAX_PARTICLES)
        return;

    Particle p{};
    p.x = x;
    p.y = y;
    p.prev_x = x - vx * 0.45f;
    p.prev_y = y - vy * 0.45f;
    p.vx = vx;
    p.vy = vy;
    p.life = life;
    p.max_life = life;
    p.type = PARTICLE_DEBRIS;
    p.palette = 0;
    p.bounce_count = 0;
    p.size = frand_range(2.5f, 4.5f);
    p.start_size = p.size;
    p.end_size = p.size;
    p.r = 1.0f; p.g = 0.70f; p.b = 0.20f; p.a = 1.0f;

    m_particles.push_back(p);
}

void RemasterParticles::add_fire_burst(float x, float y, float vx, float vy, float sz, float life, float r, float g, float b)
{
    if (m_particles.size() >= MAX_PARTICLES)
        return;

    Particle p{};
    p.x = x;
    p.y = y;
    p.prev_x = x;
    p.prev_y = y;
    p.vx = vx;
    p.vy = vy;
    p.life = life;
    p.max_life = life;
    p.type = PARTICLE_FIRE_BURST;
    p.palette = 0;
    p.bounce_count = 0;
    p.size = sz * 0.4f;
    p.start_size = sz * 0.4f;
    p.end_size = sz;
    p.r = r; p.g = g; p.b = b; p.a = 1.0f;

    m_particles.push_back(p);
}

void RemasterParticles::add_light_burst(float x, float y, float r, float g, float b, float radius, float intensity, float life)
{
    if (m_light_bursts.size() >= 32)
        return;

    LightBurst lb{};
    lb.x = x;
    lb.y = y;
    lb.r = r;
    lb.g = g;
    lb.b = b;
    lb.radius = radius;
    lb.intensity = intensity;
    lb.life = life;
    lb.max_life = life;

    m_light_bursts.push_back(lb);
}

void RemasterParticles::spawn_bullet_impact(float x, float y, float angle_deg, int palette, float nx, float ny)
{
    if (nx == 0.0f && ny == 0.0f)
    {
        query_surface_normal(x, y, nx, ny, 24.0f);
    }
    if (nx == 0.0f && ny == 0.0f)
    {
        float base_ang = (angle_deg + 180.0f) * 3.14159265f / 180.0f;
        nx = std::cos(base_ang);
        ny = -std::sin(base_ang);
    }
    float nlen = std::hypot(nx, ny);
    if (nlen > 0.001f) { nx /= nlen; ny /= nlen; }

    // Offset spawn origin slightly along the normal away from the surface
    float sx = x + nx * 2.5f;
    float sy = y + ny * 2.5f;

    float norm_ang = std::atan2(ny, nx);
    float tx = -ny, ty = nx;

    // Incoming projectile direction
    float in_rad = angle_deg * 3.14159265f / 180.0f;
    float in_dx = std::cos(in_rad);
    float in_dy = -std::sin(in_rad);

    // Reflection vector
    float dot = in_dx * nx + in_dy * ny;
    float rx = in_dx - 2.0f * dot * nx;
    float ry = in_dy - 2.0f * dot * ny;
    float rlen = std::hypot(rx, ry);
    if (rlen > 0.001f) { rx /= rlen; ry /= rlen; } else { rx = nx; ry = ny; }
    float refl_ang = std::atan2(ry, rx);

    // 1. Ricochet sparks: strictly outward hemisphere (never penetrating into the surface)
    int num_sparks = 20 + (std::rand() % 8);
    for (int i = 0; i < num_sparks; i++)
    {
        // Blend reflection direction (70%) and normal (30%) + angular spread
        float spark_ang = refl_ang * 0.70f + norm_ang * 0.30f + frand_range(-0.75f, 0.75f);
        float diff = spark_ang - norm_ang;
        while (diff > 3.14159265f) diff -= 6.2831853f;
        while (diff < -3.14159265f) diff += 6.2831853f;
        diff = std::clamp(diff, -1.35f, 1.35f); // Keep in outward hemisphere (at least ~15 deg away from surface)
        spark_ang = norm_ang + diff;

        float speed = frand_range(12.0f, 28.0f);
        float vx = std::cos(spark_ang) * speed;
        float vy = std::sin(spark_ang) * speed;
        float life = frand_range(7.0f, 14.0f); // Lively duration so sparks arc and bounce
        add_spark(sx, sy, vx, vy, life, palette);
    }

    // 2. Physical falling debris / ricochet embers (the effect the user missed!)
    int num_debris = 4 + (std::rand() % 4);
    for (int i = 0; i < num_debris; i++)
    {
        float debris_ang = norm_ang + frand_range(-1.15f, 1.15f);
        float speed = frand_range(7.0f, 16.0f);
        float vx = std::cos(debris_ang) * speed;
        float vy = std::sin(debris_ang) * speed;
        float life = frand_range(12.0f, 22.0f); // ~0.8 to 1.5 seconds: long enough to arc, hit ground, and bounce
        add_debris(sx, sy, vx, vy, life);
    }

    // 3. Impact Fireball Flash: erupts outward away from surface
    add_fire_burst(sx, sy,
                   nx * frand_range(3.0f, 5.5f) + tx * frand_range(-1.5f, 1.5f),
                   ny * frand_range(3.0f, 5.5f) + ty * frand_range(-1.5f, 1.5f),
                   12.0f, 2.5f,
                   (palette == 1 ? 0.40f : (palette == 2 ? 1.0f : 1.0f)),
                   (palette == 1 ? 0.90f : (palette == 2 ? 0.30f : 0.82f)),
                   (palette == 1 ? 1.0f : (palette == 2 ? 0.15f : 0.25f)));

    // 4. Delicate dynamic raytraced light burst (positioned in front of surface)
    if (palette == 1) // Plasma cyan
        add_light_burst(sx + nx * 5.0f, sy + ny * 5.0f, 0.20f, 0.80f, 0.98f, 48.0f, 1.35f, 1.5f);
    else if (palette == 2) // Red laser
        add_light_burst(sx + nx * 5.0f, sy + ny * 5.0f, 0.95f, 0.22f, 0.16f, 42.0f, 1.25f, 1.5f);
    else // Standard bullet amber-gold
        add_light_burst(sx + nx * 5.0f, sy + ny * 5.0f, 0.98f, 0.85f, 0.45f, 46.0f, 1.30f, 1.5f);

    // 5. Impact smoke wisps: billowing outward along normal
    for (int i = 0; i < 3; i++)
    {
        float vx = nx * frand_range(1.0f, 2.2f) + tx * frand_range(-0.8f, 0.8f);
        float vy = ny * frand_range(1.0f, 2.2f) - frand_range(0.5f, 1.5f);
        float life = frand_range(5.0f, 8.0f);
        add_smoke(sx, sy, vx, vy, 2.0f, 7.0f, life, 0.35f, 0.35f, 0.38f, 0.28f);
    }
}

void RemasterParticles::spawn_flesh_impact(float x, float y, float angle_deg)
{
    float base_angle_rad = (angle_deg + 180.0f) * 3.14159265f / 180.0f;

    // Blood droplets and flesh splatters
    int num_splatters = 16 + (std::rand() % 6);
    for (int i = 0; i < num_splatters; i++)
    {
        float spread = frand_range(-1.1f, 1.1f);
        float ang = base_angle_rad + spread;
        float speed = frand_range(5.0f, 14.0f);
        float vx = std::cos(ang) * speed;
        float vy = -std::sin(ang) * speed;
        float life = frand_range(4.0f, 8.0f);
        add_spark(x, y, vx, vy, life, 3); // palette 3 = flesh/blood
    }

    // Dark crimson mist puff
    add_smoke(x, y, 0.0f, -0.4f, 3.0f, 8.0f, 5.0f, 0.45f, 0.06f, 0.06f, 0.35f);
}

void RemasterParticles::spawn_explosion(float x, float y, int type, float nx, float ny)
{
    uint32_t current_tick = current_level ? current_level->tick_counter() : 0;
    bool duplicate = (current_tick == m_last_explo_tick &&
                      std::abs(x - m_last_explo_x) < 18.0f &&
                      std::abs(y - m_last_explo_y) < 18.0f);

    m_last_explo_tick = current_tick;
    m_last_explo_x = x;
    m_last_explo_y = y;

    bool on_surface = false;
    if (nx != 0.0f || ny != 0.0f)
    {
        on_surface = true;
    }
    else
    {
        on_surface = query_surface_normal(x, y, nx, ny, 28.0f);
    }

    float norm_ang = 0.0f;
    float tx = 0.0f, ty = 0.0f;
    float sx = x, sy = y;
    if (on_surface)
    {
        float nlen = std::hypot(nx, ny);
        if (nlen > 0.001f) { nx /= nlen; ny /= nlen; }
        norm_ang = std::atan2(ny, nx);
        tx = -ny; ty = nx;
        sx = x + nx * 3.5f;
        sy = y + ny * 3.5f;
    }

    if (!duplicate)
    {
        // 1. Expanding Shockwave ring: if on surface, shift outward along normal
        float shock_x = on_surface ? (sx + nx * 8.0f) : x;
        float shock_y = on_surface ? (sy + ny * 8.0f) : y;
        if (type == 1) // Energy / DFRIS
        {
            add_shockwave(shock_x, shock_y, 80.0f, 2.0f, 0.5f, 0.90f, 1.0f);
            add_light_burst(shock_x + (on_surface ? nx * 6.0f : 0.0f),
                            shock_y + (on_surface ? ny * 6.0f : 0.0f),
                            0.45f, 0.88f, 1.0f, 140.0f, 2.2f, 2.0f);
        }
        else // Fiery Grenade / Rocket
        {
            add_shockwave(shock_x, shock_y, 92.0f, 2.0f, 1.15f, 0.85f, 0.35f);
            add_light_burst(shock_x + (on_surface ? nx * 6.0f : 0.0f),
                            shock_y + (on_surface ? ny * 6.0f : 0.0f),
                            1.0f, 0.70f, 0.20f, 150.0f, 2.4f, 2.0f);
        }
    }

    // 2. Fiery / Energy Core Fireballs: erupting outward along normal
    int num_cores = duplicate ? 4 : 10;
    for (int i = 0; i < num_cores; i++)
    {
        float vx, vy;
        if (on_surface)
        {
            vx = nx * frand_range(5.0f, 12.0f) + tx * frand_range(-6.5f, 6.5f);
            vy = ny * frand_range(5.0f, 12.0f) + ty * frand_range(-6.5f, 6.5f);
        }
        else
        {
            vx = frand_range(-8.0f, 8.0f);
            vy = frand_range(-8.0f, 4.5f);
        }
        float sz = frand_range(16.0f, 32.0f);
        float life = frand_range(2.0f, 3.5f);
        if (type == 1)
            add_fire_burst(sx, sy, vx, vy, sz, life, 0.50f, 0.90f, 1.0f);
        else
            add_fire_burst(sx, sy, vx, vy, sz, life, 1.0f, 0.70f, 0.20f);
    }

    // 3. High-Velocity Sparks: outward hemisphere when on surface, 360 when airborne
    int num_sparks = duplicate ? 20 : (44 + (std::rand() % 14));
    for (int i = 0; i < num_sparks; i++)
    {
        float ang;
        if (on_surface)
            ang = norm_ang + frand_range(-1.35f, 1.35f);
        else
            ang = frand_range(0.0f, 6.2831853f);
        float speed = frand_range(14.0f, 34.0f);
        float vx = std::cos(ang) * speed;
        float vy = std::sin(ang) * speed;
        float life = frand_range(8.0f, 16.0f); // Longer lively duration for sparks
        add_spark(sx, sy, vx, vy, life, type == 1 ? 1 : 0);
    }

    // 4. Burning Debris / Shrapnel chunks (physical gravity and bouncing - user requested!)
    if (!duplicate)
    {
        int num_debris = 14 + (std::rand() % 8);
        for (int i = 0; i < num_debris; i++)
        {
            float vx, vy;
            if (on_surface)
            {
                float ang = norm_ang + frand_range(-1.20f, 1.20f);
                float speed = frand_range(10.0f, 26.0f);
                vx = std::cos(ang) * speed;
                vy = std::sin(ang) * speed;
            }
            else
            {
                vx = frand_range(-16.0f, 16.0f);
                vy = frand_range(-22.0f, -9.0f);
            }
            float life = frand_range(14.0f, 24.0f); // ~1.0 to 1.6 seconds: visible arc and bounce
            add_debris(sx, sy, vx, vy, life);
        }
    }

    // 5. Volumetric Smoke Puffs: billowing along normal + upward thermal rise
    int num_smoke = duplicate ? 4 : (10 + (std::rand() % 4));
    for (int i = 0; i < num_smoke; i++)
    {
        float vx, vy;
        if (on_surface)
        {
            vx = nx * frand_range(2.5f, 5.0f) + tx * frand_range(-3.0f, 3.0f);
            vy = ny * frand_range(2.5f, 5.0f) - frand_range(1.5f, 4.0f);
        }
        else
        {
            vx = frand_range(-3.5f, 3.5f);
            vy = frand_range(-4.5f, -1.2f);
        }
        float start_sz = frand_range(5.0f, 10.0f);
        float end_sz = frand_range(20.0f, 32.0f);
        float life = frand_range(8.0f, 14.0f);
        float r = frand_range(0.35f, 0.42f);
        float g = frand_range(0.28f, 0.35f);
        float b = frand_range(0.22f, 0.28f);
        add_smoke(sx + frand_range(-4.0f, 4.0f), sy + frand_range(-4.0f, 4.0f),
                  vx, vy, start_sz, end_sz, life, r, g, b, 0.25f);
    }
}

void RemasterParticles::spawn_small_explosion(float x, float y, float nx, float ny)
{
    bool on_surface = false;
    if (nx != 0.0f || ny != 0.0f)
    {
        on_surface = true;
    }
    else
    {
        on_surface = query_surface_normal(x, y, nx, ny, 24.0f);
    }

    float norm_ang = 0.0f;
    float tx = 0.0f, ty = 0.0f;
    float sx = x, sy = y;
    if (on_surface)
    {
        float nlen = std::hypot(nx, ny);
        if (nlen > 0.001f) { nx /= nlen; ny /= nlen; }
        norm_ang = std::atan2(ny, nx);
        tx = -ny; ty = nx;
        sx = x + nx * 2.5f;
        sy = y + ny * 2.5f;
    }

    // Small shockwave
    float shock_x = on_surface ? (sx + nx * 5.0f) : x;
    float shock_y = on_surface ? (sy + ny * 5.0f) : y;
    add_shockwave(shock_x, shock_y, 45.0f, 2.0f, 1.15f, 0.82f, 0.30f);

    // Sparks
    int num_sparks = 20 + (std::rand() % 8);
    for (int i = 0; i < num_sparks; i++)
    {
        float ang;
        if (on_surface)
            ang = norm_ang + frand_range(-1.35f, 1.35f);
        else
            ang = frand_range(0.0f, 6.2831853f);
        float speed = frand_range(10.0f, 24.0f);
        float vx = std::cos(ang) * speed;
        float vy = std::sin(ang) * speed;
        float life = frand_range(6.0f, 12.0f);
        add_spark(sx, sy, vx, vy, life, 0);
    }

    // 6-10 Physical bouncing debris chunks
    int num_debris = 6 + (std::rand() % 5);
    for (int i = 0; i < num_debris; i++)
    {
        float ang = on_surface ? (norm_ang + frand_range(-1.15f, 1.15f)) : frand_range(0.0f, 6.2831853f);
        float speed = frand_range(8.0f, 18.0f);
        float vx = std::cos(ang) * speed;
        float vy = std::sin(ang) * speed;
        float life = frand_range(12.0f, 20.0f);
        add_debris(sx, sy, vx, vy, life);
    }

    // Fireball center
    for (int i = 0; i < 4; i++)
    {
        float vx, vy;
        if (on_surface)
        {
            vx = nx * frand_range(3.5f, 7.5f) + tx * frand_range(-4.0f, 4.0f);
            vy = ny * frand_range(3.5f, 7.5f) + ty * frand_range(-4.0f, 4.0f);
        }
        else
        {
            vx = frand_range(-4.5f, 4.5f);
            vy = frand_range(-4.5f, 2.5f);
        }
        add_fire_burst(sx, sy, vx, vy, 16.0f, 2.0f, 1.0f, 0.65f, 0.18f);
    }

    // Smoke puffs
    for (int i = 0; i < 3; i++)
    {
        float vx = on_surface ? (nx * frand_range(1.5f, 3.0f) + tx * frand_range(-1.5f, 1.5f)) : frand_range(-2.2f, 2.2f);
        float vy = on_surface ? (ny * frand_range(1.5f, 3.0f) - frand_range(1.0f, 2.5f)) : frand_range(-3.0f, -0.8f);
        add_smoke(sx, sy, vx, vy, 4.0f, 14.0f, 6.0f, 0.35f, 0.32f, 0.30f, 0.22f);
    }

    // Dynamic light burst
    add_light_burst(sx + (on_surface ? nx * 8.0f : 0.0f),
                    sy + (on_surface ? ny * 8.0f : 0.0f),
                    0.98f, 0.72f, 0.22f, 90.0f, 1.8f, 2.0f);
}

void RemasterParticles::spawn_smoke_trail(float x, float y, float vx, float vy)
{
    float life = frand_range(5.0f, 8.0f); // Reduced by 75% (was 18-30)
    add_smoke(x, y, vx * 0.25f, vy * 0.25f - 0.4f, 3.5f, 11.0f, life, 0.32f, 0.32f, 0.34f, 0.25f);
}

void RemasterParticles::tick()
{
    // 1. Update particles
    for (size_t i = 0; i < m_particles.size(); )
    {
        Particle &p = m_particles[i];
        p.life -= 1.0f;
        if (p.life <= 0.0f)
        {
            m_particles[i] = m_particles.back();
            m_particles.pop_back();
            continue;
        }

        float t = p.life / p.max_life; // 1.0 down to 0.0

        if (p.type == PARTICLE_SPARK)
        {
            // Physics: snappy gravity + air drag
            p.vy += 0.65f;
            p.vx *= 0.98f;
            p.vy *= 0.98f;

            float next_x = p.x + p.vx;
            float next_y = p.y + p.vy;

            if (p.bounce_count < 4 && is_point_solid(next_x, next_y))
            {
                float bnx = 0.0f, bny = 0.0f;
                if (query_surface_normal(p.x, p.y, bnx, bny, 16.0f))
                {
                    float v_dot_n = p.vx * bnx + p.vy * bny;
                    if (v_dot_n < 0.0f)
                    {
                        p.vx = (p.vx - 1.55f * v_dot_n * bnx) * 0.55f;
                        p.vy = (p.vy - 1.55f * v_dot_n * bny) * 0.55f;
                        p.x += bnx * 1.5f;
                        p.y += bny * 1.5f;
                    }
                }
                else
                {
                    bool hit_x = is_point_solid(next_x, p.y);
                    bool hit_y = is_point_solid(p.x, next_y);
                    if (hit_x)
                    {
                        p.vx = -p.vx * 0.50f;
                        p.vy *= 0.85f;
                    }
                    if (hit_y)
                    {
                        p.vy = -p.vy * 0.45f;
                        p.vx *= 0.85f;
                    }
                    if (!hit_x && !hit_y)
                    {
                        p.vx = -p.vx * 0.45f;
                        p.vy = -p.vy * 0.45f;
                    }
                }
                p.bounce_count++;
            }
            else
            {
                p.prev_x = p.x;
                p.prev_y = p.y;
                p.x = next_x;
                p.y = next_y;
            }

            // Thermal cooling color curves
            if (p.palette == 0) // Bullet/laser gold
            {
                if (t > 0.65f)
                {
                    p.r = 1.0f; p.g = 0.96f; p.b = 0.82f; p.a = 1.0f;
                }
                else if (t > 0.35f)
                {
                    p.r = 1.0f; p.g = 0.72f; p.b = 0.12f; p.a = 0.95f;
                }
                else if (t > 0.12f)
                {
                    p.r = 1.0f; p.g = 0.38f; p.b = 0.02f; p.a = 0.85f;
                }
                else
                {
                    p.r = 0.75f; p.g = 0.10f; p.b = 0.0f; p.a = t / 0.12f;
                }
            }
            else if (p.palette == 1) // Plasma cyan
            {
                if (t > 0.65f)
                {
                    p.r = 0.85f; p.g = 1.0f; p.b = 1.0f; p.a = 1.0f;
                }
                else if (t > 0.35f)
                {
                    p.r = 0.15f; p.g = 0.85f; p.b = 1.0f; p.a = 0.95f;
                }
                else
                {
                    p.r = 0.05f; p.g = 0.45f; p.b = 0.90f; p.a = t / 0.35f;
                }
            }
            else if (p.palette == 2) // Red laser
            {
                if (t > 0.65f)
                {
                    p.r = 1.0f; p.g = 0.80f; p.b = 0.70f; p.a = 1.0f;
                }
                else if (t > 0.30f)
                {
                    p.r = 1.0f; p.g = 0.20f; p.b = 0.10f; p.a = 0.95f;
                }
                else
                {
                    p.r = 0.65f; p.g = 0.05f; p.b = 0.02f; p.a = t / 0.30f;
                }
            }
            else // Flesh / blood
            {
                p.r = 0.65f; p.g = 0.05f; p.b = 0.05f; p.a = t;
            }
        }
        else if (p.type == PARTICLE_DEBRIS)
        {
            // Tangible physical gravity so chunks fall at brisk, realistic speed
            p.vy += 0.70f;
            p.vx *= 0.985f;
            p.vy *= 0.985f;

            float next_x = p.x + p.vx;
            float next_y = p.y + p.vy;

            if (p.bounce_count < 5 && is_point_solid(next_x, next_y))
            {
                float bnx = 0.0f, bny = 0.0f;
                if (query_surface_normal(p.x, p.y, bnx, bny, 16.0f))
                {
                    float v_dot_n = p.vx * bnx + p.vy * bny;
                    if (v_dot_n < 0.0f)
                    {
                        p.vx = (p.vx - 1.45f * v_dot_n * bnx) * 0.50f;
                        p.vy = (p.vy - 1.45f * v_dot_n * bny) * 0.50f;
                        p.x += bnx * 2.0f;
                        p.y += bny * 2.0f;
                    }
                }
                else
                {
                    bool hit_x = is_point_solid(next_x, p.y);
                    bool hit_y = is_point_solid(p.x, next_y);
                    if (hit_x) p.vx = -p.vx * 0.50f;
                    if (hit_y) p.vy = -p.vy * 0.45f;
                    if (!hit_x && !hit_y) { p.vx = -p.vx * 0.45f; p.vy = -p.vy * 0.45f; }
                }
                p.bounce_count++;
            }
            else
            {
                p.prev_x = p.x;
                p.prev_y = p.y;
                p.x = next_x;
                p.y = next_y;
            }

            p.r = 1.0f;
            p.g = 0.30f + 0.55f * t;
            p.b = 0.12f * t * t;
            p.a = std::min(1.0f, t * 1.3f);
        }
        else if (p.type == PARTICLE_SMOKE)
        {
            p.x += p.vx;
            p.y += p.vy;
            p.vy -= 0.15f; // rapid upward thermal draft
            p.vx *= 0.93f;

            float progress = 1.0f - t;
            p.size = p.start_size + (p.end_size - p.start_size) * std::sqrt(progress);
            p.a = p.start_size * 0.035f + 0.25f * (t * t);
        }
        else if (p.type == PARTICLE_SHOCKWAVE)
        {
            float progress = 1.0f - t;
            p.size = p.start_size + (p.end_size - p.start_size) * std::pow(progress, 0.25f);
            p.a = std::pow(t, 1.30f);
        }
        else if (p.type == PARTICLE_FIRE_BURST)
        {
            p.x += p.vx;
            p.y += p.vy;
            p.vx *= 0.78f;
            p.vy *= 0.78f;

            float progress = 1.0f - t;
            p.size = p.start_size + (p.end_size - p.start_size) * progress;
            p.a = t;
        }

        i++;
    }

    // 2. Update dynamic light bursts
    for (size_t i = 0; i < m_light_bursts.size(); )
    {
        LightBurst &lb = m_light_bursts[i];
        lb.life -= 1.0f;
        if (lb.life <= 0.0f)
        {
            m_light_bursts[i] = m_light_bursts.back();
            m_light_bursts.pop_back();
            continue;
        }
        float t = lb.life / lb.max_life;
        lb.intensity = lb.intensity * (t / (t + 0.1f));
        i++;
    }
}

void RemasterParticles::render_all(float cam_x, float cam_y, float view_w, float view_h, int fbo_w, int fbo_h)
{
    if (m_particles.empty() || m_prog == 0)
        return;

    m_smoke_verts.clear();
    m_additive_verts.clear();

    for (const auto &p : m_particles)
    {
        if (p.type == PARTICLE_SMOKE)
        {
            float hs = p.size;
            ParticleVertex v0{p.x - hs, p.y - hs, p.r, p.g, p.b, p.a, 0.0f, 0.0f, 1};
            ParticleVertex v1{p.x + hs, p.y - hs, p.r, p.g, p.b, p.a, 1.0f, 0.0f, 1};
            ParticleVertex v2{p.x + hs, p.y + hs, p.r, p.g, p.b, p.a, 1.0f, 1.0f, 1};

            ParticleVertex v3{p.x - hs, p.y - hs, p.r, p.g, p.b, p.a, 0.0f, 0.0f, 1};
            ParticleVertex v4{p.x + hs, p.y + hs, p.r, p.g, p.b, p.a, 1.0f, 1.0f, 1};
            ParticleVertex v5{p.x - hs, p.y + hs, p.r, p.g, p.b, p.a, 0.0f, 1.0f, 1};

            m_smoke_verts.push_back(v0);
            m_smoke_verts.push_back(v1);
            m_smoke_verts.push_back(v2);
            m_smoke_verts.push_back(v3);
            m_smoke_verts.push_back(v4);
            m_smoke_verts.push_back(v5);
        }
        else if (p.type == PARTICLE_SPARK)
        {
            float dx = p.x - p.prev_x;
            float dy = p.y - p.prev_y;
            float len = std::hypot(dx, dy);

            float nx = 0.0f, ny = 1.0f;
            if (len > 0.001f)
            {
                nx = -dy / len;
                ny = dx / len;
            }

            float head_x = p.x;
            float head_y = p.y;
            float tail_x = (len > 0.4f) ? (p.x - dx * 1.5f) : (p.x - 0.8f);
            float tail_y = (len > 0.4f) ? (p.y - dy * 1.5f) : (p.y - 0.8f);

            float hw = 0.95f;
            float tail_hw = 0.30f;

            ParticleVertex v0{tail_x + nx * tail_hw, tail_y + ny * tail_hw, p.r, p.g, p.b, p.a * 0.15f, 0.0f, 0.0f, 0};
            ParticleVertex v1{tail_x - nx * tail_hw, tail_y - ny * tail_hw, p.r, p.g, p.b, p.a * 0.15f, 0.0f, 0.0f, 0};
            ParticleVertex v2{head_x + nx * hw,      head_y + ny * hw,      p.r, p.g, p.b, p.a,         1.0f, 0.0f, 0};

            ParticleVertex v3{tail_x - nx * tail_hw, tail_y - ny * tail_hw, p.r, p.g, p.b, p.a * 0.15f, 0.0f, 0.0f, 0};
            ParticleVertex v4{head_x + nx * hw,      head_y + ny * hw,      p.r, p.g, p.b, p.a,         1.0f, 0.0f, 0};
            ParticleVertex v5{head_x - nx * hw,      head_y - ny * hw,      p.r, p.g, p.b, p.a,         1.0f, 0.0f, 0};

            m_additive_verts.push_back(v0);
            m_additive_verts.push_back(v1);
            m_additive_verts.push_back(v2);
            m_additive_verts.push_back(v3);
            m_additive_verts.push_back(v4);
            m_additive_verts.push_back(v5);
        }
        else if (p.type == PARTICLE_DEBRIS)
        {
            float hs = p.size * 0.5f;
            ParticleVertex v0{p.x - hs, p.y - hs, p.r, p.g, p.b, p.a, 0.0f, 0.0f, 1};
            ParticleVertex v1{p.x + hs, p.y - hs, p.r, p.g, p.b, p.a, 1.0f, 0.0f, 1};
            ParticleVertex v2{p.x + hs, p.y + hs, p.r, p.g, p.b, p.a, 1.0f, 1.0f, 1};

            ParticleVertex v3{p.x - hs, p.y - hs, p.r, p.g, p.b, p.a, 0.0f, 0.0f, 1};
            ParticleVertex v4{p.x + hs, p.y + hs, p.r, p.g, p.b, p.a, 1.0f, 1.0f, 1};
            ParticleVertex v5{p.x - hs, p.y + hs, p.r, p.g, p.b, p.a, 0.0f, 1.0f, 1};

            m_additive_verts.push_back(v0);
            m_additive_verts.push_back(v1);
            m_additive_verts.push_back(v2);
            m_additive_verts.push_back(v3);
            m_additive_verts.push_back(v4);
            m_additive_verts.push_back(v5);
        }
        else if (p.type == PARTICLE_SHOCKWAVE)
        {
            float hs = p.size;
            ParticleVertex v0{p.x - hs, p.y - hs, p.r, p.g, p.b, p.a, 0.0f, 0.0f, 2};
            ParticleVertex v1{p.x + hs, p.y - hs, p.r, p.g, p.b, p.a, 1.0f, 0.0f, 2};
            ParticleVertex v2{p.x + hs, p.y + hs, p.r, p.g, p.b, p.a, 1.0f, 1.0f, 2};

            ParticleVertex v3{p.x - hs, p.y - hs, p.r, p.g, p.b, p.a, 0.0f, 0.0f, 2};
            ParticleVertex v4{p.x + hs, p.y + hs, p.r, p.g, p.b, p.a, 1.0f, 1.0f, 2};
            ParticleVertex v5{p.x - hs, p.y + hs, p.r, p.g, p.b, p.a, 0.0f, 1.0f, 2};

            m_additive_verts.push_back(v0);
            m_additive_verts.push_back(v1);
            m_additive_verts.push_back(v2);
            m_additive_verts.push_back(v3);
            m_additive_verts.push_back(v4);
            m_additive_verts.push_back(v5);
        }
        else if (p.type == PARTICLE_FIRE_BURST)
        {
            float hs = p.size;
            ParticleVertex v0{p.x - hs, p.y - hs, p.r, p.g, p.b, p.a, 0.0f, 0.0f, 1};
            ParticleVertex v1{p.x + hs, p.y - hs, p.r, p.g, p.b, p.a, 1.0f, 0.0f, 1};
            ParticleVertex v2{p.x + hs, p.y + hs, p.r, p.g, p.b, p.a, 1.0f, 1.0f, 1};

            ParticleVertex v3{p.x - hs, p.y - hs, p.r, p.g, p.b, p.a, 0.0f, 0.0f, 1};
            ParticleVertex v4{p.x + hs, p.y + hs, p.r, p.g, p.b, p.a, 1.0f, 1.0f, 1};
            ParticleVertex v5{p.x - hs, p.y + hs, p.r, p.g, p.b, p.a, 0.0f, 1.0f, 1};

            m_additive_verts.push_back(v0);
            m_additive_verts.push_back(v1);
            m_additive_verts.push_back(v2);
            m_additive_verts.push_back(v3);
            m_additive_verts.push_back(v4);
            m_additive_verts.push_back(v5);
        }
    }

    glViewport(0, 0, fbo_w, fbo_h);
    glUseProgram(m_prog);
    glUniform2f(glGetUniformLocation(m_prog, "u_cam_pos"), cam_x, cam_y);
    glUniform2f(glGetUniformLocation(m_prog, "u_view_size"), view_w, view_h);
    glUniform1f(glGetUniformLocation(m_prog, "u_flip_y"), 1.0f);

    glEnable(GL_BLEND);

    // 1. Alpha-blended pass: volumetric smoke puffs
    if (!m_smoke_verts.empty())
    {
        glBindVertexArray(m_vao_smoke);
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo_smoke);
        glBufferData(GL_ARRAY_BUFFER, m_smoke_verts.size() * sizeof(ParticleVertex), m_smoke_verts.data(), GL_STREAM_DRAW);

        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)m_smoke_verts.size());
    }

    // 2. Additive pass: glowing sparks, fireballs, debris, and shockwaves
    if (!m_additive_verts.empty())
    {
        glBindVertexArray(m_vao_additive);
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo_additive);
        glBufferData(GL_ARRAY_BUFFER, m_additive_verts.size() * sizeof(ParticleVertex), m_additive_verts.data(), GL_STREAM_DRAW);

        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)m_additive_verts.size());
    }

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDisable(GL_BLEND);
}

void RemasterParticles::render_emissive(float cam_x, float cam_y, float view_w, float view_h, int fbo_w, int fbo_h)
{
    if (m_particles.empty() || m_prog == 0)
        return;

    m_emissive_verts.clear();

    for (const auto &p : m_particles)
    {
        if (p.type == PARTICLE_SPARK)
        {
            float dx = p.x - p.prev_x;
            float dy = p.y - p.prev_y;
            float len = std::hypot(dx, dy);

            float nx = 0.0f, ny = 1.0f;
            if (len > 0.001f)
            {
                nx = -dy / len;
                ny = dx / len;
            }

            float head_x = p.x;
            float head_y = p.y;
            float tail_x = (len > 0.4f) ? (p.x - dx * 1.5f) : (p.x - 0.8f);
            float tail_y = (len > 0.4f) ? (p.y - dy * 1.5f) : (p.y - 0.8f);

            float hw = 1.05f;
            float tail_hw = 0.35f;
            float boost = 1.25f;

            ParticleVertex v0{tail_x + nx * tail_hw, tail_y + ny * tail_hw, p.r * boost, p.g * boost, p.b * boost, p.a * 0.20f, 0.0f, 0.0f, 0};
            ParticleVertex v1{tail_x - nx * tail_hw, tail_y - ny * tail_hw, p.r * boost, p.g * boost, p.b * boost, p.a * 0.20f, 0.0f, 0.0f, 0};
            ParticleVertex v2{head_x + nx * hw,      head_y + ny * hw,      p.r * boost, p.g * boost, p.b * boost, p.a,          1.0f, 0.0f, 0};

            ParticleVertex v3{tail_x - nx * tail_hw, tail_y - ny * tail_hw, p.r * boost, p.g * boost, p.b * boost, p.a * 0.20f, 0.0f, 0.0f, 0};
            ParticleVertex v4{head_x + nx * hw,      head_y + ny * hw,      p.r * boost, p.g * boost, p.b * boost, p.a,          1.0f, 0.0f, 0};
            ParticleVertex v5{head_x - nx * hw,      head_y - ny * hw,      p.r * boost, p.g * boost, p.b * boost, p.a,          1.0f, 0.0f, 0};

            m_emissive_verts.push_back(v0);
            m_emissive_verts.push_back(v1);
            m_emissive_verts.push_back(v2);
            m_emissive_verts.push_back(v3);
            m_emissive_verts.push_back(v4);
            m_emissive_verts.push_back(v5);
        }
        else if (p.type == PARTICLE_SHOCKWAVE || p.type == PARTICLE_FIRE_BURST)
        {
            float hs = p.size;
            float boost = (p.type == PARTICLE_SHOCKWAVE) ? 1.30f : 1.45f;
            int vtype = (p.type == PARTICLE_SHOCKWAVE) ? 2 : 1;

            ParticleVertex v0{p.x - hs, p.y - hs, p.r * boost, p.g * boost, p.b * boost, p.a, 0.0f, 0.0f, vtype};
            ParticleVertex v1{p.x + hs, p.y - hs, p.r * boost, p.g * boost, p.b * boost, p.a, 1.0f, 0.0f, vtype};
            ParticleVertex v2{p.x + hs, p.y + hs, p.r * boost, p.g * boost, p.b * boost, p.a, 1.0f, 1.0f, vtype};

            ParticleVertex v3{p.x - hs, p.y - hs, p.r * boost, p.g * boost, p.b * boost, p.a, 0.0f, 0.0f, vtype};
            ParticleVertex v4{p.x + hs, p.y + hs, p.r * boost, p.g * boost, p.b * boost, p.a, 1.0f, 1.0f, vtype};
            ParticleVertex v5{p.x - hs, p.y + hs, p.r * boost, p.g * boost, p.b * boost, p.a, 0.0f, 1.0f, vtype};

            m_emissive_verts.push_back(v0);
            m_emissive_verts.push_back(v1);
            m_emissive_verts.push_back(v2);
            m_emissive_verts.push_back(v3);
            m_emissive_verts.push_back(v4);
            m_emissive_verts.push_back(v5);
        }
    }

    if (m_emissive_verts.empty())
        return;

    glViewport(0, 0, fbo_w, fbo_h);
    glUseProgram(m_prog);
    glUniform2f(glGetUniformLocation(m_prog, "u_cam_pos"), cam_x, cam_y);
    glUniform2f(glGetUniformLocation(m_prog, "u_view_size"), view_w, view_h);
    glUniform1f(glGetUniformLocation(m_prog, "u_flip_y"), 1.0f);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);

    glBindVertexArray(m_vao_additive);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo_additive);
    glBufferData(GL_ARRAY_BUFFER, m_emissive_verts.size() * sizeof(ParticleVertex), m_emissive_verts.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)m_emissive_verts.size());

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDisable(GL_BLEND);
}
