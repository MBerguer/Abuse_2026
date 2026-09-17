#ifndef REMASTER_PARTICLES_H
#define REMASTER_PARTICLES_H

#include <vector>
#include <cstdint>

#if defined __APPLE__
#   include <OpenGL/gl3.h>
#else
#   include <GL/gl.h>
#endif

enum ParticleType
{
    PARTICLE_SPARK = 0,    // Tapered glowing spark/ember with physics bounce
    PARTICLE_SMOKE,        // Expanding soft billboard smoke puff
    PARTICLE_SHOCKWAVE,    // Expanding blast ring
    PARTICLE_DEBRIS,       // Flying glowing shrapnel chunk
    PARTICLE_FIRE_BURST    // Fiery core puff
};

struct Particle
{
    float x, y;
    float prev_x, prev_y;
    float vx, vy;
    float r, g, b, a;
    float size;
    float start_size;
    float end_size;
    float life;
    float max_life;
    int type;
    int palette;           // 0=bullet/laser gold, 1=plasma cyan, 2=red laser, 3=flesh/blood
    int bounce_count;
};

struct LightBurst
{
    float x, y;            // World coordinates
    float r, g, b;
    float radius;          // Radius in world pixels
    float intensity;
    float life;
    float max_life;
};

struct ParticleVertex
{
    float x, y;            // World pos
    float r, g, b, a;      // Color
    float u, v;            // Quad UV
    int type;              // 0=flat/streak, 1=soft circle, 2=ring
};

class RemasterParticles
{
public:
    static RemasterParticles &get()
    {
        static RemasterParticles instance;
        return instance;
    }

    bool init();
    void shutdown();
    void clear();

    // Geometry query
    bool query_surface_normal(float x, float y, float &nx, float &ny, float search_dist = 28.0f);

    // Spawning API
    void spawn_bullet_impact(float x, float y, float angle_deg, int palette = 0, float nx = 0.0f, float ny = 0.0f);
    void spawn_flesh_impact(float x, float y, float angle_deg);
    void spawn_explosion(float x, float y, int type = 0, float nx = 0.0f, float ny = 0.0f); // 0=fire/standard, 1=energy/cyan
    void spawn_small_explosion(float x, float y, float nx = 0.0f, float ny = 0.0f);
    void spawn_smoke_trail(float x, float y, float vx, float vy);

    // Simulation update (called once per game tick)
    void tick();

    // Rendering into Remaster pipeline
    void render_all(float cam_x, float cam_y, float view_w, float view_h, int fbo_w, int fbo_h);
    void render_emissive(float cam_x, float cam_y, float view_w, float view_h, int fbo_w, int fbo_h);

    // Dynamic point lights for RemasterLighting
    const std::vector<LightBurst> &get_light_bursts() const { return m_light_bursts; }

private:
    RemasterParticles();
    ~RemasterParticles();

    bool compile_shader();
    void add_spark(float x, float y, float vx, float vy, float life, int palette);
    void add_smoke(float x, float y, float vx, float vy, float start_sz, float end_sz, float life, float r, float g, float b, float a);
    void add_shockwave(float x, float y, float max_radius, float life, float r, float g, float b);
    void add_debris(float x, float y, float vx, float vy, float life);
    void add_fire_burst(float x, float y, float vx, float vy, float sz, float life, float r, float g, float b);
    void add_light_burst(float x, float y, float r, float g, float b, float radius, float intensity, float life);

    static constexpr size_t MAX_PARTICLES = 4096;
    std::vector<Particle> m_particles;
    std::vector<LightBurst> m_light_bursts;

    // Cooldown/throttle tracking to prevent duplicate explosions on the same tick
    uint32_t m_last_explo_tick;
    float m_last_explo_x, m_last_explo_y;

    // GPU resources
    GLuint m_prog;
    GLuint m_vao_smoke;
    GLuint m_vbo_smoke;
    GLuint m_vao_additive;
    GLuint m_vbo_additive;

    std::vector<ParticleVertex> m_smoke_verts;
    std::vector<ParticleVertex> m_additive_verts;
    std::vector<ParticleVertex> m_emissive_verts;
};

#endif // REMASTER_PARTICLES_H
