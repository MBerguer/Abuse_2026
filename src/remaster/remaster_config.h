#ifndef REMASTER_CONFIG_H
#define REMASTER_CONFIG_H

#include <string>

enum RenderMode {
    RENDER_MODE_CLASSIC_1995 = 0,
    RENDER_MODE_CLASSIC_RT   = 1,
    RENDER_MODE_HD_PBR_RT    = 2
};

struct RemasterConfig
{
    int render_mode = RENDER_MODE_HD_PBR_RT; // 0: Classic 1995, 1: Classic+RT, 2: HD PBR (PS5)
    bool enabled = true;             // Master RT/pipeline toggle
    bool hd_textures = true;         // Next-Gen 4-Channel HD PBR Textures
    bool raytracing = true;          // 2D raymarched shadows
    bool soft_shadows = true;        // Penumbra filtering
    bool normal_mapping = true;      // Relieve/normal map lighting
    bool bloom = true;               // HDR bloom & emission glow
    bool volumetric_fog = true;      // 2D light shafts / atmospheric scattering
    bool reflections = true;         // Screen-space floor/water reflections
    bool high_fps = true;            // Fixed-step simulation decouple to 120/144Hz+
    bool spatial_audio = true;       // 3D HRTF positional sound & distance attenuation
    bool widescreen = true;          // Widescreen FOV adaptation
    bool crt_filter = false;         // Retro CRT curvature & scanline filter

    int shadow_quality = 2;          // 0: Off, 1: Fast (16 steps), 2: High (32 steps)
    float light_intensity = 0.88f;   // Dynamic light multiplier (calibrated for delicate, cinematic lighting)
    float ambient_intensity = 0.42f; // Ambient light level (calibrated high-contrast sci-fi base)
    float bloom_intensity = 0.38f;   // Bloom strength (subtle high-end glow, non-blinding)
    float normal_strength = 1.0f;    // Normal map depth strength

    // On-screen notification
    std::string notification_text;
    float notification_timer = 0.0f; // in seconds
    bool show_hud_overlay = false;   // F12 toggle
    bool show_pos_debug = false;     // F10 toggle: player position & tile info overlay (OFF by default)

    static RemasterConfig &get()
    {
        static RemasterConfig instance;
        return instance;
    }

    void show_notification(const std::string &text, float duration = 2.5f)
    {
        notification_text = text;
        notification_timer = duration;
    }

    void apply_render_mode()
    {
        if (render_mode == RENDER_MODE_CLASSIC_1995)
        {
            enabled = false;
            hd_textures = false;
            show_notification("RENDER MODE: ORIGINAL 1995 RETRO (VGA 320x200 / 70Hz)");
        }
        else if (render_mode == RENDER_MODE_CLASSIC_RT)
        {
            enabled = true;
            hd_textures = false;
            show_notification("RENDER MODE: CLASSIC 16px RETRO + 2D RAY TRACING & SSR");
        }
        else
        {
            enabled = true;
            hd_textures = true;
            show_notification("RENDER MODE: NEXT-GEN xBR HD REMASTER (1920x1200 / 4K SHARP)");
        }
    }

    void cycle_render_mode()
    {
        render_mode = (render_mode + 1) % 3;
        apply_render_mode();
    }

    void toggle_hd()
    {
        if (render_mode == RENDER_MODE_HD_PBR_RT)
            render_mode = RENDER_MODE_CLASSIC_RT;
        else
            render_mode = RENDER_MODE_HD_PBR_RT;
        apply_render_mode();
    }

    void toggle_remaster()
    {
        cycle_render_mode();
    }

    void toggle_hud()
    {
        show_hud_overlay = !show_hud_overlay;
    }

    void toggle_pos_debug()
    {
        show_pos_debug = !show_pos_debug;
        show_notification(show_pos_debug ? "POS DEBUG OVERLAY: ON" : "POS DEBUG OVERLAY: OFF", 1.5f);
    }

    void load();
    void save();
};

#endif // REMASTER_CONFIG_H
