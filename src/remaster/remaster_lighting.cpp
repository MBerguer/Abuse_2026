#include "remaster_lighting.h"
#include "common.h"
#include "light.h"
#include <cmath>
#include <algorithm>

void RemasterLighting::clear()
{
    m_lights.clear();
}

void RemasterLighting::add_point_light(float norm_x, float norm_y, float norm_z,
                                       float r, float g, float b,
                                       float radius, float intensity)
{
    if (m_lights.size() >= MAX_LIGHTS)
        return;

    GPULight l{};
    l.pos_x = norm_x;
    l.pos_y = norm_y;
    l.pos_z = norm_z;
    l.col_r = r;
    l.col_g = g;
    l.col_b = b;
    l.radius = radius;
    l.intensity = intensity;
    l.is_spot = 0;
    l.dir_x = 0.0f;
    l.dir_y = 0.0f;
    l.spot_cutoff = 0.0f;
    m_lights.push_back(l);
}

void RemasterLighting::add_spot_light(float norm_x, float norm_y, float norm_z,
                                      float r, float g, float b,
                                      float radius, float intensity,
                                      float dir_x, float dir_y, float cutoff)
{
    if (m_lights.size() >= MAX_LIGHTS)
        return;

    GPULight l{};
    l.pos_x = norm_x;
    l.pos_y = norm_y;
    l.pos_z = norm_z;
    l.col_r = r;
    l.col_g = g;
    l.col_b = b;
    l.radius = radius;
    l.intensity = intensity;
    l.is_spot = 1;
    l.dir_x = dir_x;
    l.dir_y = dir_y;
    l.spot_cutoff = cutoff;
    m_lights.push_back(l);
}

void RemasterLighting::update_frame_lights(int camera_x, int camera_y, int view_w, int view_h,
                                           int player_screen_x, int player_screen_y,
                                           int aim_screen_x, int aim_screen_y,
                                           bool player_firing)
{
    clear();

    // 1. Gather static and animated environmental lights from Abuse's level
    for (light_source *s = first_light_source; s; s = s->next)
    {
        if (m_lights.size() >= MAX_LIGHTS - 4) // Reserve slots for player and weapon effects
            break;

        float lx = (float)(s->x - camera_x) / (float)view_w;
        float ly = (float)(s->y - camera_y) / (float)view_h;
        float radius = (float)s->outer_radius / (float)view_w;

        // Culling: check if light touches visible screen
        if (lx + radius < 0.0f || lx - radius > 1.0f ||
            ly + radius < 0.0f || ly - radius > 1.0f)
            continue;

        // Color based on light type in Abuse
        float cr = 1.0f, cg = 0.95f, cb = 0.85f;
        if (s->type == 1) { cr = 0.3f; cg = 1.0f; cb = 0.4f; } // Alien toxic green
        else if (s->type == 2) { cr = 1.0f; cg = 0.3f; cb = 0.2f; } // Alarm red
        else if (s->type == 3) { cr = 0.4f; cg = 0.6f; cb = 1.0f; } // Cyber blue

        add_point_light(lx, ly, 0.06f, cr, cg, cb, std::max(0.15f, radius * 1.5f), 1.3f);
    }

    // 2. Player Tactical Flashlight / Aim Cone
    if (view_w > 0 && view_h > 0)
    {
        float px = (float)player_screen_x / (float)view_w;
        float py = (float)player_screen_y / (float)view_h;
        float ax = (float)aim_screen_x / (float)view_w;
        float ay = (float)aim_screen_y / (float)view_h;

        float dx = ax - px;
        float dy = ay - py;
        float len = std::sqrt(dx * dx + dy * dy);

        if (len > 0.0001f)
        {
            dx /= len;
            dy /= len;

            // Player body ambient aura
            add_point_light(px, py, 0.08f, 0.8f, 0.85f, 1.0f, 0.22f, 0.9f);

            // Directional weapon flashlight cone towards crosshair
            add_spot_light(px, py, 0.05f, 1.0f, 0.98f, 0.92f, 0.75f, 1.8f, dx, dy, 0.72f);

            // Muzzle flash when firing
            if (player_firing)
            {
                add_point_light(px + dx * 0.05f, py + dy * 0.05f, 0.04f, 1.0f, 0.85f, 0.3f, 0.35f, 3.5f);
            }
        }
    }
}
