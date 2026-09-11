#include "remaster_config.h"
#include <fstream>
#include <sstream>
#include <iostream>

extern char const *get_save_filename_prefix();

void RemasterConfig::load()
{
    std::string path = std::string(get_save_filename_prefix()) + "remaster.cfg";
    std::ifstream file(path);
    if (file.is_open())
    {
        std::string line;
        while (std::getline(file, line))
        {
            if (line.empty() || line[0] == ';' || line[0] == '#')
                continue;
            std::istringstream iss(line);
            std::string key;
            if (std::getline(iss, key, '='))
            {
                std::string val;
                if (std::getline(iss, val))
                {
                    // trim
                    key.erase(0, key.find_first_not_of(" \t\r\n"));
                    key.erase(key.find_last_not_of(" \t\r\n") + 1);
                    val.erase(0, val.find_first_not_of(" \t\r\n"));
                    val.erase(val.find_last_not_of(" \t\r\n") + 1);

                    if (key == "enabled") enabled = (val == "1" || val == "true");
                    else if (key == "raytracing") raytracing = (val == "1" || val == "true");
                    else if (key == "soft_shadows") soft_shadows = (val == "1" || val == "true");
                    else if (key == "normal_mapping") normal_mapping = (val == "1" || val == "true");
                    else if (key == "bloom") bloom = (val == "1" || val == "true");
                    else if (key == "volumetric_fog") volumetric_fog = (val == "1" || val == "true");
                    else if (key == "reflections") reflections = (val == "1" || val == "true");
                    else if (key == "high_fps") high_fps = (val == "1" || val == "true");
                    else if (key == "spatial_audio") spatial_audio = (val == "1" || val == "true");
                    else if (key == "widescreen") widescreen = (val == "1" || val == "true");
                    else if (key == "crt_filter") crt_filter = (val == "1" || val == "true");
                    else if (key == "shadow_quality") shadow_quality = std::stoi(val);
                    else if (key == "light_intensity") light_intensity = std::stof(val);
                    else if (key == "ambient_intensity") ambient_intensity = std::stof(val);
                    else if (key == "bloom_intensity") bloom_intensity = std::stof(val);
                    else if (key == "normal_strength") normal_strength = std::stof(val);
                }
            }
        }
    }

    if (getenv("ABUSE_NO_REMASTER"))
        enabled = false;
    if (getenv("ABUSE_NO_RT"))
        raytracing = false;
}

void RemasterConfig::save()
{
    std::string path = std::string(get_save_filename_prefix()) + "remaster.cfg";
    std::ofstream file(path);
    if (!file.is_open())
        return;

    file << "; Abuse 2026 Remaster Configuration\n";
    file << "enabled = " << (enabled ? 1 : 0) << "\n";
    file << "raytracing = " << (raytracing ? 1 : 0) << "\n";
    file << "soft_shadows = " << (soft_shadows ? 1 : 0) << "\n";
    file << "normal_mapping = " << (normal_mapping ? 1 : 0) << "\n";
    file << "bloom = " << (bloom ? 1 : 0) << "\n";
    file << "volumetric_fog = " << (volumetric_fog ? 1 : 0) << "\n";
    file << "reflections = " << (reflections ? 1 : 0) << "\n";
    file << "high_fps = " << (high_fps ? 1 : 0) << "\n";
    file << "spatial_audio = " << (spatial_audio ? 1 : 0) << "\n";
    file << "widescreen = " << (widescreen ? 1 : 0) << "\n";
    file << "crt_filter = " << (crt_filter ? 1 : 0) << "\n";
    file << "shadow_quality = " << shadow_quality << "\n";
    file << "light_intensity = " << light_intensity << "\n";
    file << "ambient_intensity = " << ambient_intensity << "\n";
    file << "bloom_intensity = " << bloom_intensity << "\n";
    file << "normal_strength = " << normal_strength << "\n";
}
