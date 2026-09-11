#ifndef REMASTER_CONFIG_H
#define REMASTER_CONFIG_H

#include <string>

struct RemasterConfig
{
    bool enabled = true;             // F11 master toggle
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
    float light_intensity = 1.2f;    // Dynamic light multiplier
    float ambient_intensity = 0.75f;  // Ambient light level
    float bloom_intensity = 0.55f;   // Bloom strength
    float normal_strength = 1.0f;    // Normal map depth strength

    // On-screen notification
    std::string notification_text;
    float notification_timer = 0.0f; // in seconds
    bool show_hud_overlay = false;   // F12 toggle

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

    void toggle_remaster()
    {
        enabled = !enabled;
        show_notification(enabled ? "REMASTER MODE: ACTIVATED (RT, Normals, Bloom, 120Hz+)"
                                  : "CLASSIC 1995 MODE: ACTIVATED (Original Software Renderer)");
    }

    void toggle_hud()
    {
        show_hud_overlay = !show_hud_overlay;
    }

    void load();
    void save();
};

#endif // REMASTER_CONFIG_H
