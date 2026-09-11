#include "remaster_timing.h"
#include <algorithm>
#include <cmath>

void RemasterTiming::update_timing(unsigned int current_time, unsigned int last_physics_tick, unsigned int physics_step_interval)
{
    if (physics_step_interval == 0)
    {
        m_alpha = 1.0f;
        return;
    }

    float elapsed = static_cast<float>(current_time - last_physics_tick);
    m_alpha = std::clamp(elapsed / static_cast<float>(physics_step_interval), 0.0f, 1.0f);
}

int RemasterTiming::interpolate(int prev_val, int curr_val) const
{
    return static_cast<int>(std::round(prev_val + m_alpha * (curr_val - prev_val)));
}
