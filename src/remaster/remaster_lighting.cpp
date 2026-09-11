#include "remaster_lighting.h"
#include "common.h"
#include "light.h"
#include "objects.h"
#include "level.h"
#include <cmath>
#include <algorithm>
#include <cstring>

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

        // Natural warm industrial light (no artificial green/red tints)
        float cr = 1.0f, cg = 0.98f, cb = 0.94f;
        add_point_light(lx, ly, 0.06f, cr, cg, cb, std::max(0.12f, radius * 1.2f), 0.65f);
    }

    // 2. Player Subtle Local Aura & Muzzle Flash (no blinding searchlight cone)
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
        }
        else
        {
            dx = 1.0f;
            dy = 0.0f;
        }

        // 2a. Player subtle body presence (dim ambient glow around character)
        add_point_light(px, py, 0.08f, 1.0f, 0.98f, 0.95f, 0.10f, 0.35f);

        // 2b. Tactical Weapon-Mounted Directional Flashlight (follows aim cone)
        float spot_intensity = player_firing ? 2.6f : 1.75f;
        float spot_radius = player_firing ? 0.60f : 0.52f;
        float spot_cutoff = 0.68f; // ~48 degree realistic tactical cone
        add_spot_light(px + dx * 0.025f, py + dy * 0.025f, 0.05f,
                       1.0f, 0.97f, 0.92f,
                       spot_radius, spot_intensity,
                       dx, dy, spot_cutoff);

        // 2c. Dynamic muzzle flash burst when firing
        if (player_firing)
        {
            add_point_light(px + dx * 0.045f, py + dy * 0.045f, 0.04f, 1.0f, 0.90f, 0.45f, 0.28f, 3.2f);
        }
    }

    // 3. Dynamic colored lights from active weapon projectiles, rockets, lasers, and explosions
    extern level *current_level;
    extern char **object_names;
    extern int total_objects;

    if (current_level && view_w > 0 && view_h > 0)
    {
        for (game_object *o = current_level->first_active_object(); o; o = o->next_active)
        {
            if (m_lights.size() >= MAX_LIGHTS)
                break;

            float ox = (float)(o->x - camera_x) / (float)view_w;
            float oy = (float)(o->y - camera_y) / (float)view_h;

            if (ox < -0.1f || ox > 1.1f || oy < -0.1f || oy > 1.1f)
                continue;

            if (o->otype >= 0 && o->otype < total_objects && object_names && object_names[o->otype])
            {
                const char *name = object_names[o->otype];

                // Rockets / Missiles: intense fiery flame and smoke trail
                if (strcasestr(name, "rocket"))
                {
                    add_point_light(ox, oy, 0.05f, 1.0f, 0.55f, 0.12f, 0.22f, 2.8f);
                }
                // Grenades: pulsing green/yellow phosphorescent light
                else if (strcasestr(name, "gren"))
                {
                    add_point_light(ox, oy, 0.05f, 0.35f, 1.0f, 0.20f, 0.18f, 2.2f);
                }
                // Plasma shots: bright electric cyan/blue ray
                else if (strcasestr(name, "plasma"))
                {
                    add_point_light(ox, oy, 0.04f, 0.15f, 0.75f, 1.0f, 0.24f, 3.0f);
                }
                // Lasers and rifle bullets: vibrant red/amber beam light
                else if (strcasestr(name, "laser") || strcasestr(name, "bullet"))
                {
                    add_point_light(ox, oy, 0.04f, 1.0f, 0.20f, 0.12f, 0.16f, 2.4f);
                }
                // Firebombs: intense burning orange-yellow fire
                else if (strcasestr(name, "fire"))
                {
                    add_point_light(ox, oy, 0.05f, 1.0f, 0.45f, 0.05f, 0.25f, 3.2f);
                }
                // Discs and energy blades: violet/magenta glow
                else if (strcasestr(name, "dfris") || strcasestr(name, "lsaber"))
                {
                    add_point_light(ox, oy, 0.05f, 0.85f, 0.25f, 1.0f, 0.20f, 2.6f);
                }
                // Explosions: large expanding warm flash lighting up surrounding architecture
                else if (strcasestr(name, "explo") || strcasestr(name, "exp_"))
                {
                    add_point_light(ox, oy, 0.06f, 1.0f, 0.85f, 0.40f, 0.45f, 4.2f);
                }
            }
        }
    }
}
