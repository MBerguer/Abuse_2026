#include "remaster_audio.h"
#include "remaster_config.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <cstdio>

void RemasterAudio::init()
{
    printf("Remaster: 3D Spatial Audio & HRTF DSP subsystem initialized.\n");
}

void RemasterAudio::update(float dt)
{
    // Reserved for dynamic reverb transitions & ambient loop fades
    (void)dt;
}

SpatialAudioResult RemasterAudio::calculate_spatial(
    int32_t sound_x, int32_t sound_y,
    int32_t listener_x, int32_t listener_y,
    int base_volume)
{
    SpatialAudioResult res;
    if (base_volume <= 0)
        return res;

    float dx = static_cast<float>(sound_x - listener_x);
    float dy = static_cast<float>(sound_y - listener_y);
    float dist = std::sqrt(dx * dx + dy * dy);
    res.distance = dist;

    // In widescreen remaster, audible radius is extended to 750 units
    const float max_dist = 750.0f;
    if (dist >= max_dist)
    {
        res.audible = false;
        return res;
    }

    // Smooth acoustic distance falloff curve (inverse distance with reference roll-off)
    const float ref_dist = 120.0f;
    const float rolloff = 1.25f;
    float falloff = 1.0f;
    if (dist > ref_dist)
    {
        falloff = ref_dist / (ref_dist + rolloff * (dist - ref_dist));
    }

    int attenuated_vol = static_cast<int>(std::round(base_volume * falloff));
    attenuated_vol = std::clamp(attenuated_vol, 0, 127);

    if (attenuated_vol <= 0)
    {
        res.audible = false;
        return res;
    }

    // HRTF Stereo Panning Law
    // dx < 0 is sound to the left; dx > 0 is sound to the right
    const float pan_half_width = 320.0f;
    float pan = std::clamp(dx / pan_half_width, -1.0f, 1.0f);
    res.pan = pan;

    // Constant-power circular panning (pi/4 = 45 degrees center)
    const float pi = 3.14159265358979323846f;
    float angle = (pan + 1.0f) * 0.25f * pi; // [0, pi/2]
    float gain_left = std::cos(angle);
    float gain_right = std::sin(angle);

    // Acoustic head-shadow filter simulation:
    // When sound is on one side, high frequencies and sound pressure are shadowed on the far ear
    if (pan < 0.0f)
    {
        // Sound is on the left -> right ear is in acoustic shadow
        gain_right *= (1.0f - 0.20f * std::abs(pan));
    }
    else
    {
        // Sound is on the right -> left ear is in acoustic shadow
        gain_left *= (1.0f - 0.20f * std::abs(pan));
    }

    res.left = static_cast<uint8_t>(std::clamp(static_cast<int>(std::round(gain_left * 255.0f)), 0, 255));
    res.right = static_cast<uint8_t>(std::clamp(static_cast<int>(std::round(gain_right * 255.0f)), 0, 255));
    res.volume = attenuated_vol;
    res.audible = true;

    return res;
}

std::string RemasterAudio::find_hd_music(const std::string &original_filename)
{
    if (original_filename.empty())
        return "";

    std::filesystem::path orig(original_filename);
    std::string stem = orig.stem().string();

    // Check extensions: .ogg, .flac, .mp3, .wav
    const char *extensions[] = { ".ogg", ".flac", ".mp3", ".wav" };
    const char *subdirs[] = { "music", "hd/music", "sfx", "" };

    for (const char *dir : subdirs)
    {
        for (const char *ext : extensions)
        {
            std::filesystem::path candidate;
            if (dir[0] != '\0')
                candidate = std::filesystem::path(dir) / (stem + ext);
            else
                candidate = orig.parent_path() / (stem + ext);

            std::error_code ec;
            if (std::filesystem::exists(candidate, ec))
            {
                return candidate.string();
            }
        }
    }

    return "";
}

void RemasterAudio::play_reverb_reflection(Mix_Chunk *chunk, int volume, uint8_t left, uint8_t right)
{
    if (!chunk || volume <= 10)
        return;

    // Environmental acoustic reflection: secondary reflection at lower volume and swapped/widened panning
    int reverb_vol = std::clamp(volume / 4, 1, 35);
    int channel = Mix_PlayChannel(-1, chunk, 0);
    if (channel > -1)
    {
        Mix_Volume(channel, reverb_vol);
        // Diffuse reflection: cross-feed channels
        uint8_t rev_left = static_cast<uint8_t>((left * 0.4f) + (right * 0.6f));
        uint8_t rev_right = static_cast<uint8_t>((right * 0.4f) + (left * 0.6f));
        Mix_SetPanning(channel, rev_left, rev_right);
    }
}
