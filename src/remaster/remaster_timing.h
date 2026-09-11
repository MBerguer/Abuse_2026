#ifndef REMASTER_TIMING_H
#define REMASTER_TIMING_H

class RemasterTiming
{
public:
    static RemasterTiming &get()
    {
        static RemasterTiming instance;
        return instance;
    }

    void update_timing(unsigned int current_time, unsigned int last_physics_tick, unsigned int physics_step_interval);

    float get_interpolation_alpha() const { return m_alpha; }
    void set_interpolation_alpha(float a) { m_alpha = a; }

    int interpolate(int prev_val, int curr_val) const;

private:
    float m_alpha = 0.0f;
};

#endif // REMASTER_TIMING_H
