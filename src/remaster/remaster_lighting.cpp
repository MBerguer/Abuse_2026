#include "remaster_lighting.h"
#include "common.h"
#include "light.h"
#include "objects.h"
#include "level.h"
#include "remaster_particles.h"
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
                                           bool player_firing,
                                           float aim_dir_x, float aim_dir_y,
                                           int muzzle_screen_x, int muzzle_screen_y,
                                           bool flashlight_on)
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

        float mx = (muzzle_screen_x >= 0) ? (float)muzzle_screen_x / (float)view_w : px;
        float my = (muzzle_screen_y >= 0) ? (float)muzzle_screen_y / (float)view_h : py;

        float dx = aim_dir_x;
        float dy = aim_dir_y;
        if (std::abs(dx) < 0.0001f && std::abs(dy) < 0.0001f)
        {
            float ax = (float)aim_screen_x / (float)view_w;
            float ay = (float)aim_screen_y / (float)view_h;
            dx = ax - px;
            dy = ay - py;
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
        }

        // Enforce outward direction: light must NEVER point backward towards player torso
        float bdx = mx - px;
        float bdy = my - py;
        float blen = std::sqrt(bdx * bdx + bdy * bdy);
        if (blen > 0.001f)
        {
            float b_norm_x = bdx / blen;
            float b_norm_y = bdy / blen;
            float dot = dx * b_norm_x + dy * b_norm_y;
            if (dot < 0.2f)
            {
                dx = b_norm_x;
                dy = b_norm_y;
            }
        }

        // 2a. Player subtle body presence (dim ambient glow around character torso)
        float body_intensity = flashlight_on ? 0.30f : 0.18f;
        add_point_light(px, py, 0.06f, 1.0f, 0.98f, 0.95f, 0.08f, body_intensity);

        if (flashlight_on)
        {
            // 2b. Tactical Weapon-Mounted Directional Flashlight (starts directly at muzzle tip, pointing outward)
            float spot_intensity = player_firing ? 2.8f : 2.4f;
            float spot_radius = player_firing ? 0.90f : 0.85f;
            float spot_cutoff = 0.78f; // ~38.7 degree tactical cone (half-angle), matching user capture
            add_spot_light(mx, my, 0.05f,
                           1.0f, 0.98f, 0.94f,
                           spot_radius, spot_intensity,
                           dx, dy, spot_cutoff);

            // Flashlight lens emitter core: subtle tactical diode right on the weapon muzzle tip
            add_point_light(mx, my, 0.025f,
                            1.0f, 0.98f, 0.95f,
                            0.035f, 1.1f);

            // 2c. Dynamic muzzle flash burst when firing (anchored right at muzzle tip)
            if (player_firing)
            {
                add_point_light(mx, my, 0.04f, 1.0f, 0.90f, 0.45f, 0.28f, 3.2f);
            }
        }
    }

    // 3. Dynamic colored lights from active weapon projectiles, interactive doors, switches, and consoles
    extern level *current_level;
    extern char **object_names;
    extern int total_objects;

    if (current_level && view_w > 0 && view_h > 0)
    {
        float pulse = 0.85f + 0.15f * std::sin((float)current_level->tick_counter() * 0.18f);

        for (game_object *o = current_level->first_active_object(); o; o = o->next_active)
        {
            if (m_lights.size() >= MAX_LIGHTS)
                break;

            float ox = (float)(o->x - camera_x) / (float)view_w;
            float oy = (float)(o->y - camera_y) / (float)view_h;

            if (ox < -0.15f || ox > 1.15f || oy < -0.15f || oy > 1.15f)
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
                // Switches / Buttons (SWITCH, SWITCH_ONCE, SWITCH_DELAY, SWITCH_BALL, SWITCH_MOVER)
                else if (strcasestr(name, "switch"))
                {
                    bool is_on = (o->state == running || o->Aistate != 0);
                    if (is_on)
                    {
                        // Activated: vibrant glowing green indicator light
                        add_point_light(ox, oy - 0.02f, 0.05f, 0.12f, 0.98f, 0.25f, 0.14f, 2.2f);
                    }
                    else
                    {
                        // Standby / Off: subtle amber-red standby LED
                        add_point_light(ox, oy - 0.02f, 0.04f, 0.85f, 0.15f, 0.05f, 0.08f, 1.0f);
                    }
                }
                // Doors / Gates (SWITCH_DOOR, TRAP_DOOR, DOOR)
                else if (strcasestr(name, "door") && !strcasestr(name, "tp_door"))
                {
                    // Standard sliding/trap doors linked to switches:
                    // State 0: closed, State 1: opening, State 2: open, State 3: closing
                    bool is_open_or_moving = (o->state != stopped || o->Aistate != 0);
                    if (is_open_or_moving)
                    {
                        // Open threshold: warm yellowish / golden amber passageway light (amarillita)
                        add_point_light(ox, oy - 0.10f, 0.06f, 1.0f, 0.88f, 0.40f, 0.28f, 2.2f);
                    }
                    else
                    {
                        // Closed: subtle warm amber safety sensor light on frame (no green)
                        add_point_light(ox, oy - 0.20f, 0.04f, 0.90f, 0.55f, 0.10f, 0.08f, 1.0f);
                    }
                }
                // Computer Save Terminals (RESTART_POSITION)
                else if (strcasestr(name, "restart") || strcasestr(name, "console"))
                {
                    bool is_saving = (o->state == running || o->Aistate >= 2);
                    if (is_saving)
                    {
                        // Active save flash: bright digital cyan-white burst
                        add_point_light(ox, oy - 0.06f, 0.06f, 0.40f, 0.90f, 1.0f, 0.28f, 3.2f);
                    }
                    else
                    {
                        // CRT terminal display: cool blue phosphor glow onto floor and nearby wall
                        add_point_light(ox, oy - 0.06f, 0.05f, 0.18f, 0.65f, 1.0f, 0.16f, 1.6f);
                    }
                }
                // Teleporters, Portals, and Exit Beams (TP_DOOR, TELE, TELE2, TELE_BEAM, NEXT_LEVEL, SENSOR_TELEPORT)
                else if (strcasestr(name, "tele") || strcasestr(name, "tp_door") ||
                         strcasestr(name, "next_level") || strcasestr(name, "end_port") ||
                         strcasestr(name, "port"))
                {
                    bool is_active = (o->state == running || o->Aistate != 0);
                    float tp_pulse = 0.85f + 0.15f * std::sin((float)current_level->tick_counter() * 0.40f);
                    float rad = is_active ? 0.46f : 0.32f;
                    float inten = is_active ? 4.2f : 2.6f;

                    // Dazzling pure-white energetic raytracing portal light
                    add_point_light(ox, oy - 0.08f, 0.06f, 0.96f, 0.98f, 1.0f, rad * tp_pulse, inten);
                    // Core electric diamond beam highlight
                    add_point_light(ox, oy - 0.08f, 0.04f, 1.0f, 1.0f, 1.0f, rad * 0.40f, inten * 1.35f);
                }
                // Health Powerup (heart)
                else if (strcasestr(name, "health"))
                {
                    add_point_light(ox, oy, 0.04f, 0.95f, 0.20f, 0.35f, 0.10f * pulse, 1.3f);
                }
                // Ammo and Powerup pickups
                else if (strcasestr(name, "_icon") || strcasestr(name, "power_"))
                {
                    add_point_light(ox, oy, 0.04f, 0.30f, 0.85f, 0.95f, 0.09f, 1.1f);
                }
            }
        }

        // 3a. Dynamic light bursts from RemasterParticles (bullet impacts, explosions, etc.)
        for (const auto &lb : RemasterParticles::get().get_light_bursts())
        {
            if (m_lights.size() >= MAX_LIGHTS)
                break;

            float lx = (float)(lb.x - camera_x) / (float)view_w;
            float ly = (float)(lb.y - camera_y) / (float)view_h;
            float rad = (float)lb.radius / (float)view_w;

            if (lx + rad < 0.0f || lx - rad > 1.0f ||
                ly + rad < 0.0f || ly - rad > 1.0f)
                continue;

            add_point_light(lx, ly, 0.05f, lb.r, lb.g, lb.b, rad, lb.intensity);
        }
    }

    // 4. Atmospheric off-screen lights in wide open spaces (tenue / creepy ambiance)
    if (current_level && view_w > 0 && view_h > 0 &&
        current_level->foreground_width() > 0 && current_level->foreground_height() > 0)
    {
        int fg_w = current_level->foreground_width();
        int fg_h = current_level->foreground_height();

        // Sample 35 points across the viewport to measure openness of the room
        int open_count = 0;
        int total_samples = 0;
        for (int sy = 1; sy <= 5; sy++)
        {
            int ty = (camera_y + (sy * view_h) / 6) / 16;
            if (ty < 0 || ty >= fg_h) continue;
            uint16_t *line = current_level->get_fgline(ty);
            if (!line) continue;
            for (int sx = 1; sx <= 7; sx++)
            {
                int tx = (camera_x + (sx * view_w) / 8) / 16;
                if (tx >= 0 && tx < fg_w)
                {
                    total_samples++;
                    if ((line[tx] & 0x7FFF) == 0) // Air / empty space
                    {
                        open_count++;
                    }
                }
            }
        }

        // Only inject off-screen cavern lights in wide open spaces (> 50% open space)
        if (total_samples > 0 && (open_count * 100 / total_samples) >= 50)
        {
            const int metraje = 300; // Place an atmospheric shaft every ~300 world pixels
            int min_col = (camera_x - 150) / metraje;
            int max_col = (camera_x + view_w + 150) / metraje;
            uint32_t tick = current_level->tick_counter();

            for (int col = min_col; col <= max_col; col++)
            {
                if (m_lights.size() >= MAX_LIGHTS) break;

                int wx = col * metraje + 150;
                float lx = (float)(wx - camera_x) / (float)view_w;

                // Positioned OUTSIDE the screen above the ceiling:
                // Screen Y is negative (-0.22f), never showing the light bulb directly
                float ly = -0.22f;

                // Subtle organic electrical pulse and occasional spooky flicker
                float flicker_phase = (float)tick * 0.08f + (float)col * 2.39f;
                float flicker = 0.85f + 0.15f * std::sin(flicker_phase);
                if (((tick + col * 31) % 89) < 6)
                {
                    flicker *= 0.40f; // Eerie voltage drop / stutter
                }

                // Creepy/Tenue palette:
                // Alternate between cold eerie mercury-vapor cyan/white and dim sodium amber
                float cr, cg, cb;
                if (col % 2 == 0)
                {
                    cr = 0.65f; cg = 0.80f; cb = 0.95f; // Cold industrial vent shaft light
                }
                else
                {
                    cr = 0.85f; cg = 0.60f; cb = 0.28f; // Dim eerie emergency beacon
                }

                // Wide radius projects soft volumetric light down into the dark chamber
                add_point_light(lx, ly, 0.08f, cr, cg, cb, 0.72f, 1.15f * flicker);

                // For deep chasms / pits, add an ominous bottom glow
                if (col % 3 == 1 && m_lights.size() < MAX_LIGHTS)
                {
                    float ly_pit = 1.25f; // Deep below the floor
                    add_point_light(lx, ly_pit, 0.08f, 0.90f, 0.35f, 0.10f, 0.65f, 0.85f * flicker);
                }
            }
        }
    }
}
