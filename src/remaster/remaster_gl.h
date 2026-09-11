#ifndef REMASTER_GL_H
#define REMASTER_GL_H

#ifdef __APPLE__
#ifndef GL_SILENCE_DEPRECATION
#define GL_SILENCE_DEPRECATION
#endif
#include <OpenGL/gl3.h>
#include <OpenGL/gl3ext.h>
#else
#include <GL/gl.h>
#endif

class RemasterGL
{
public:
    static bool init(int screen_w, int screen_h);
    static void shutdown();
    static void resize(int screen_w, int screen_h);

    static void render_frame(const void *pixel_data, int src_w, int src_h, int window_w, int window_h,
                             bool in_gameplay = true,
                             float ui_u1 = 0.0f, float ui_v1 = 1.0f, float ui_u2 = 0.0f, float ui_v2 = 1.0f,
                             int level_ambient = 32);
    static void render_classic(const void *pixel_data, int src_w, int src_h, int window_w, int window_h);
    static void capture_screenshot(const char *filepath, int window_w, int window_h);

    static void render_quad();
    static bool compile_shader(GLuint &program, const char *vs_src, const char *fs_src);

    static bool is_initialized() { return s_initialized; }

private:
    static void init_fbo(int w, int h);

    static bool s_initialized;
    static int s_width;
    static int s_height;

    static GLuint s_quad_vao;
    static GLuint s_quad_vbo;

    static GLuint s_source_texture;

    // Shader programs
    static GLuint s_classic_prog;
    static GLuint s_gbuffer_prog;
    static GLuint s_raytracing_prog;
    static GLuint s_blur_prog;
    static GLuint s_composite_prog;

    // G-Buffer FBO & Textures
    static GLuint s_gbuffer_fbo;
    static GLuint s_g_albedo;
    static GLuint s_g_normal;
    static GLuint s_g_emission;
    static GLuint s_g_occlusion;

    // Lit Scene FBO & Texture
    static GLuint s_lit_fbo;
    static GLuint s_lit_texture;

    // Bloom Ping-Pong FBOs & Textures
    static GLuint s_bloom_fbo[2];
    static GLuint s_bloom_texture[2];

    static float s_time;
};

#endif // REMASTER_GL_H
