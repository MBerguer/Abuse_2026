#ifndef REMASTER_AUDIO_H
#define REMASTER_AUDIO_H

#include <cstdint>
#include <string>
#include <cmath>
#include "SDL_mixer.h"

struct SpatialAudioResult
{
    bool audible = false;
    int volume = 0;       // 0 - 127
    uint8_t left = 128;   // 0 - 255
    uint8_t right = 128;  // 0 - 255
    float distance = 0.0f;
    float pan = 0.0f;     // -1.0 (left) to 1.0 (right)
};

enum class AcousticEnvironment
{
    Corridor,       // Tight, dry sound, small delay
    IndustrialHall, // Metallic slapback, medium decay
    Silo,           // Large resonant vertical shaft
    OpenAir         // Wide dispersion, gentle decay
};

class RemasterAudio
{
public:
    static RemasterAudio &get()
    {
        static RemasterAudio instance;
        return instance;
    }

    void init();
    void update(float dt);

    // Calculate 3D positional HRTF stereo panning and distance attenuation
    SpatialAudioResult calculate_spatial(int32_t sound_x, int32_t sound_y,
                                         int32_t listener_x, int32_t listener_y,
                                         int base_volume);

    // Look for high-definition / modern replacements for soundtrack (.ogg, .flac, .mp3)
    std::string find_hd_music(const std::string &original_filename);

    // Environmental acoustics
    void set_environment(AcousticEnvironment env) { m_env = env; }
    AcousticEnvironment get_environment() const { return m_env; }

    // Subtle acoustic reverb reflection playback
    void play_reverb_reflection(Mix_Chunk *chunk, int volume, uint8_t left, uint8_t right);

private:
    RemasterAudio() = default;
    ~RemasterAudio() = default;

    AcousticEnvironment m_env = AcousticEnvironment::IndustrialHall;
};

#endif // REMASTER_AUDIO_H
