#include "remaster_gl.h"
#include "remaster_shaders.h"
#include "remaster_config.h"
#include "remaster_lighting.h"
#include "remaster_hud.h"

#include <iostream>
#include <vector>

bool RemasterGL::s_initialized = false;
int RemasterGL::s_width = 320;
int RemasterGL::s_height = 200;

GLuint RemasterGL::s_quad_vao = 0;
GLuint RemasterGL::s_quad_vbo = 0;
GLuint RemasterGL::s_source_texture = 0;

GLuint RemasterGL::s_classic_prog = 0;
GLuint RemasterGL::s_gbuffer_prog = 0;
GLuint RemasterGL::s_raytracing_prog = 0;
GLuint RemasterGL::s_blur_prog = 0;
GLuint RemasterGL::s_composite_prog = 0;

GLuint RemasterGL::s_gbuffer_fbo = 0;
GLuint RemasterGL::s_g_albedo = 0;
GLuint RemasterGL::s_g_normal = 0;
GLuint RemasterGL::s_g_emission = 0;
GLuint RemasterGL::s_g_occlusion = 0;

GLuint RemasterGL::s_lit_fbo = 0;
GLuint RemasterGL::s_lit_texture = 0;

GLuint RemasterGL::s_bloom_fbo[2] = {0, 0};
GLuint RemasterGL::s_bloom_texture[2] = {0, 0};

float RemasterGL::s_time = 0.0f;

bool RemasterGL::compile_shader(GLuint &program, const char *vs_src, const char *fs_src)
{
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vs_src, nullptr);
    glCompileShader(vs);

    GLint success;
    glGetShaderiv(vs, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        char infoLog[512];
        glGetShaderInfoLog(vs, 512, nullptr, infoLog);
        std::cerr << "Vertex Shader Compilation Error: " << infoLog << std::endl;
        return false;
    }

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fs_src, nullptr);
    glCompileShader(fs);

    glGetShaderiv(fs, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        char infoLog[512];
        glGetShaderInfoLog(fs, 512, nullptr, infoLog);
        std::cerr << "Fragment Shader Compilation Error: " << infoLog << std::endl;
        return false;
    }

    program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);

    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success)
    {
        char infoLog[512];
        glGetProgramInfoLog(program, 512, nullptr, infoLog);
        std::cerr << "Shader Program Link Error: " << infoLog << std::endl;
        return false;
    }

    glDeleteShader(vs);
    glDeleteShader(fs);
    return true;
}

void RemasterGL::init_fbo(int w, int h)
{
    s_width = w;
    s_height = h;

    // Delete existing if resizing
    if (s_gbuffer_fbo)
    {
        glDeleteFramebuffers(1, &s_gbuffer_fbo);
        glDeleteTextures(1, &s_g_albedo);
        glDeleteTextures(1, &s_g_normal);
        glDeleteTextures(1, &s_g_emission);
        glDeleteTextures(1, &s_g_occlusion);
        glDeleteFramebuffers(1, &s_lit_fbo);
        glDeleteTextures(1, &s_lit_texture);
        glDeleteFramebuffers(2, s_bloom_fbo);
        glDeleteTextures(2, s_bloom_texture);
    }

    // 1. G-Buffer FBO
    glGenFramebuffers(1, &s_gbuffer_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, s_gbuffer_fbo);

    auto create_tex = [w, h](GLuint &tex, GLenum internalFormat, GLenum format, GLenum type, GLint filter) {
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, w, h, 0, format, type, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    };

    create_tex(s_g_albedo, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR);
    create_tex(s_g_normal, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR);
    create_tex(s_g_emission, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR);
    create_tex(s_g_occlusion, GL_R8, GL_RED, GL_UNSIGNED_BYTE, GL_LINEAR);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_g_albedo, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, s_g_normal, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, s_g_emission, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3, GL_TEXTURE_2D, s_g_occlusion, 0);

    GLenum drawBuffers[4] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3};
    glDrawBuffers(4, drawBuffers);

    // 2. Lit Scene FBO
    glGenFramebuffers(1, &s_lit_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, s_lit_fbo);
    create_tex(s_lit_texture, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_lit_texture, 0);

    // 3. Bloom Ping-Pong FBOs (Downsampled half size for performance and wider radius)
    int bw = std::max(1, w / 2);
    int bh = std::max(1, h / 2);
    glGenFramebuffers(2, s_bloom_fbo);
    for (int i = 0; i < 2; i++)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, s_bloom_fbo[i]);
        create_tex(s_bloom_texture[i], GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_bloom_texture[i], 0);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool RemasterGL::init(int screen_w, int screen_h)
{
    // Fullscreen quad geometry
    float quadVertices[] = {
        // pos        // uv
        -1.0f,  1.0f,  0.0f, 0.0f,
        -1.0f, -1.0f,  0.0f, 1.0f,
         1.0f, -1.0f,  1.0f, 1.0f,

        -1.0f,  1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 1.0f,
         1.0f,  1.0f,  1.0f, 0.0f
    };

    glGenVertexArrays(1, &s_quad_vao);
    glGenBuffers(1, &s_quad_vbo);

    glBindVertexArray(s_quad_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_quad_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    // Create source texture
    glGenTextures(1, &s_source_texture);
    glBindTexture(GL_TEXTURE_2D, s_source_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Compile Shaders
    if (!compile_shader(s_classic_prog, RemasterShaders::quad_vs, RemasterShaders::classic_fs)) return false;
    if (!compile_shader(s_gbuffer_prog, RemasterShaders::quad_vs, RemasterShaders::gbuffer_fs)) return false;
    if (!compile_shader(s_raytracing_prog, RemasterShaders::quad_vs, RemasterShaders::raytracing_lighting_fs)) return false;
    if (!compile_shader(s_blur_prog, RemasterShaders::quad_vs, RemasterShaders::blur_fs)) return false;
    if (!compile_shader(s_composite_prog, RemasterShaders::quad_vs, RemasterShaders::composite_fs)) return false;

    init_fbo(screen_w, screen_h);
    RemasterHUD::get().init();

    s_initialized = true;
    std::cout << "[RemasterGL] Initialized Modern 2D Deferred GPU Pipeline (GL 3.3 Core)." << std::endl;
    return true;
}

void RemasterGL::shutdown()
{
    if (!s_initialized) return;

    RemasterHUD::get().cleanup();

    glDeleteVertexArrays(1, &s_quad_vao);
    glDeleteBuffers(1, &s_quad_vbo);
    glDeleteTextures(1, &s_source_texture);

    glDeleteProgram(s_classic_prog);
    glDeleteProgram(s_gbuffer_prog);
    glDeleteProgram(s_raytracing_prog);
    glDeleteProgram(s_blur_prog);
    glDeleteProgram(s_composite_prog);

    if (s_gbuffer_fbo)
    {
        glDeleteFramebuffers(1, &s_gbuffer_fbo);
        glDeleteTextures(1, &s_g_albedo);
        glDeleteTextures(1, &s_g_normal);
        glDeleteTextures(1, &s_g_emission);
        glDeleteTextures(1, &s_g_occlusion);
        glDeleteFramebuffers(1, &s_lit_fbo);
        glDeleteTextures(1, &s_lit_texture);
        glDeleteFramebuffers(2, s_bloom_fbo);
        glDeleteTextures(2, s_bloom_texture);
    }

    s_initialized = false;
}

void RemasterGL::resize(int screen_w, int screen_h)
{
    if (s_initialized && (screen_w != s_width || screen_h != s_height))
    {
        init_fbo(screen_w, screen_h);
    }
}

void RemasterGL::render_quad()
{
    glBindVertexArray(s_quad_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

void RemasterGL::capture_screenshot(const char *filepath, int window_w, int window_h)
{
    std::vector<uint8_t> pixels(window_w * window_h * 4);
    glReadPixels(0, 0, window_w, window_h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    uint32_t row_size = ((window_w * 3 + 3) / 4) * 4;
    uint32_t image_size = row_size * window_h;
    uint32_t file_size = 54 + image_size;

    uint8_t header[54] = {
        'B', 'M',
        static_cast<uint8_t>(file_size), static_cast<uint8_t>(file_size >> 8),
        static_cast<uint8_t>(file_size >> 16), static_cast<uint8_t>(file_size >> 24),
        0, 0, 0, 0,
        54, 0, 0, 0,
        40, 0, 0, 0,
        static_cast<uint8_t>(window_w), static_cast<uint8_t>(window_w >> 8),
        static_cast<uint8_t>(window_w >> 16), static_cast<uint8_t>(window_w >> 24),
        static_cast<uint8_t>(window_h), static_cast<uint8_t>(window_h >> 8),
        static_cast<uint8_t>(window_h >> 16), static_cast<uint8_t>(window_h >> 24),
        1, 0, 24, 0,
        0, 0, 0, 0,
        static_cast<uint8_t>(image_size), static_cast<uint8_t>(image_size >> 8),
        static_cast<uint8_t>(image_size >> 16), static_cast<uint8_t>(image_size >> 24),
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };

    FILE *f = fopen(filepath, "wb");
    if (!f) return;
    fwrite(header, 1, 54, f);
    std::vector<uint8_t> row(row_size, 0);
    for (int y = 0; y < window_h; y++)
    {
        for (int x = 0; x < window_w; x++)
        {
            int src_idx = (y * window_w + x) * 4;
            row[x * 3 + 0] = pixels[src_idx + 2]; // B
            row[x * 3 + 1] = pixels[src_idx + 1]; // G
            row[x * 3 + 2] = pixels[src_idx + 0]; // R
        }
        fwrite(row.data(), 1, row_size, f);
    }
    fclose(f);
}

static void check_dump_screenshot(int window_w, int window_h)
{
    static int s_frame_counter = 0;
    s_frame_counter++;
    const char *dump_env = getenv("ABUSE_DUMP_FRAME");
    if (dump_env)
    {
        int target = atoi(dump_env);
        if (s_frame_counter >= target)
        {
            const char *out_path = getenv("ABUSE_DUMP_PATH");
            std::string final_png = out_path ? out_path : "/Users/mberguer/.gemini/antigravity/brain/7829aad7-21ac-4465-ba25-1fade3429f56/test_screen.png";
            std::string tmp_bmp = "/Users/mberguer/.gemini/antigravity/brain/7829aad7-21ac-4465-ba25-1fade3429f56/scratch/abuse_frame.bmp";
            RemasterGL::capture_screenshot(tmp_bmp.c_str(), window_w, window_h);
            std::string cmd = "rm -f \"" + final_png + "\" && sips -s format png \"" + tmp_bmp + "\" --out \"" + final_png + "\" >/dev/null 2>&1";
            system(cmd.c_str());
            printf("[RemasterGL] Captured frame %d to %s\n", s_frame_counter, final_png.c_str());
            exit(0);
        }
    }
}

void RemasterGL::render_classic(const void *pixel_data, int src_w, int src_h, int window_w, int window_h)
{
    auto &cfg = RemasterConfig::get();

    // Update source texture
    glBindTexture(GL_TEXTURE_2D, s_source_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, src_w, src_h, 0, GL_BGRA, GL_UNSIGNED_BYTE, pixel_data);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, window_w, window_h);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    int vp_x = 0, vp_y = 0, vp_w = window_w, vp_h = window_h;
    if (cfg.widescreen && src_h > 0)
    {
        float target_aspect = static_cast<float>(src_w) / static_cast<float>(src_h);
        float win_aspect = static_cast<float>(window_w) / static_cast<float>(window_h);
        if (win_aspect > target_aspect)
        {
            vp_w = static_cast<int>(window_h * target_aspect);
            vp_h = window_h;
            vp_x = (window_w - vp_w) / 2;
            vp_y = 0;
        }
        else
        {
            vp_w = window_w;
            vp_h = static_cast<int>(window_w / target_aspect);
            vp_x = 0;
            vp_y = (window_h - vp_h) / 2;
        }
    }
    glViewport(vp_x, vp_y, vp_w, vp_h);

    glUseProgram(s_classic_prog);
    glUniform1f(glGetUniformLocation(s_classic_prog, "u_flip_y"), 0.0f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s_source_texture);
    glUniform1i(glGetUniformLocation(s_classic_prog, "u_screen"), 0);
    glUniform1i(glGetUniformLocation(s_classic_prog, "u_crt_enabled"), cfg.crt_filter ? 1 : 0);

    render_quad();

    // Remaster HUD & Notification pass
    RemasterHUD::get().render(window_w, window_h, 0, 0, window_w, window_h, src_w, src_h);

    check_dump_screenshot(window_w, window_h);
}

void RemasterGL::render_frame(const void *pixel_data, int src_w, int src_h, int window_w, int window_h,
                              bool in_gameplay,
                              float ui_u1, float ui_v1, float ui_u2, float ui_v2)
{
    s_time += 0.01667f;
    auto &cfg = RemasterConfig::get();

    if (!cfg.enabled)
    {
        render_classic(pixel_data, src_w, src_h, window_w, window_h);
        return;
    }

    if (src_w != s_width || src_h != s_height)
    {
        resize(src_w, src_h);
    }

    // 1. Upload scene pixel data to GPU texture
    glBindTexture(GL_TEXTURE_2D, s_source_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, src_w, src_h, 0, GL_BGRA, GL_UNSIGNED_BYTE, pixel_data);

    // 2. G-Buffer Pass: Extract Albedo, Normal (Sobel filter), Emission, Occlusion
    glBindFramebuffer(GL_FRAMEBUFFER, s_gbuffer_fbo);
    glViewport(0, 0, src_w, src_h);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(s_gbuffer_prog);
    glUniform1f(glGetUniformLocation(s_gbuffer_prog, "u_flip_y"), 1.0f);
    glUniform4f(glGetUniformLocation(s_gbuffer_prog, "u_ui_rect"), ui_u1, ui_v1, ui_u2, ui_v2);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s_source_texture);
    glUniform1i(glGetUniformLocation(s_gbuffer_prog, "u_scene"), 0);
    glUniform2f(glGetUniformLocation(s_gbuffer_prog, "u_texel_size"), 1.0f / src_w, 1.0f / src_h);
    glUniform1f(glGetUniformLocation(s_gbuffer_prog, "u_normal_strength"), cfg.normal_strength);

    render_quad();

    // 3. 2D Ray Tracing & Dynamic Lighting Pass
    glBindFramebuffer(GL_FRAMEBUFFER, s_lit_fbo);
    glViewport(0, 0, src_w, src_h);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(s_raytracing_prog);
    glUniform1f(glGetUniformLocation(s_raytracing_prog, "u_flip_y"), 1.0f);
    glUniform4f(glGetUniformLocation(s_raytracing_prog, "u_ui_rect"), ui_u1, ui_v1, ui_u2, ui_v2);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s_g_albedo);
    glUniform1i(glGetUniformLocation(s_raytracing_prog, "u_albedo"), 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, s_g_normal);
    glUniform1i(glGetUniformLocation(s_raytracing_prog, "u_normal"), 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, s_g_emission);
    glUniform1i(glGetUniformLocation(s_raytracing_prog, "u_emission"), 2);

    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, s_g_occlusion);
    glUniform1i(glGetUniformLocation(s_raytracing_prog, "u_occlusion"), 3);

    // Bind Lights
    const auto &lights = in_gameplay ? RemasterLighting::get().get_lights() : std::vector<GPULight>{};
    int num_lights = in_gameplay ? std::min((int)lights.size(), RemasterLighting::MAX_LIGHTS) : 0;
    glUniform1i(glGetUniformLocation(s_raytracing_prog, "u_num_lights"), num_lights);

    for (int i = 0; i < num_lights; i++)
    {
        std::string base = "u_lights[" + std::to_string(i) + "].";
        glUniform3f(glGetUniformLocation(s_raytracing_prog, (base + "position").c_str()),
                    lights[i].pos_x, lights[i].pos_y, lights[i].pos_z);
        glUniform3f(glGetUniformLocation(s_raytracing_prog, (base + "color").c_str()),
                    lights[i].col_r, lights[i].col_g, lights[i].col_b);
        glUniform1f(glGetUniformLocation(s_raytracing_prog, (base + "radius").c_str()), lights[i].radius);
        glUniform1f(glGetUniformLocation(s_raytracing_prog, (base + "intensity").c_str()), lights[i].intensity);
        glUniform1i(glGetUniformLocation(s_raytracing_prog, (base + "is_spot").c_str()), lights[i].is_spot);
        glUniform2f(glGetUniformLocation(s_raytracing_prog, (base + "spot_dir").c_str()), lights[i].dir_x, lights[i].dir_y);
        glUniform1f(glGetUniformLocation(s_raytracing_prog, (base + "spot_cutoff").c_str()), lights[i].spot_cutoff);
    }

    if (in_gameplay)
    {
        // Ambient illumination matches non-raytracing 100% baseline: full visibility and authentic artwork
        float amb = 1.0f * cfg.ambient_intensity;
        glUniform3f(glGetUniformLocation(s_raytracing_prog, "u_ambient_color"), amb, amb, amb);
        glUniform1i(glGetUniformLocation(s_raytracing_prog, "u_raytracing_enabled"), cfg.raytracing ? 1 : 0);
        glUniform1i(glGetUniformLocation(s_raytracing_prog, "u_soft_shadows"), cfg.soft_shadows ? 1 : 0);
        glUniform1i(glGetUniformLocation(s_raytracing_prog, "u_shadow_quality"), cfg.shadow_quality);
        glUniform1f(glGetUniformLocation(s_raytracing_prog, "u_light_intensity"), cfg.light_intensity);
        glUniform1i(glGetUniformLocation(s_raytracing_prog, "u_volumetric_enabled"), cfg.volumetric_fog ? 1 : 0);
    }
    else
    {
        // Menus / Title Screen / Intro: 100% full bright ambient, no shadow darkness
        glUniform3f(glGetUniformLocation(s_raytracing_prog, "u_ambient_color"), 1.0f, 1.0f, 1.0f);
        glUniform1i(glGetUniformLocation(s_raytracing_prog, "u_raytracing_enabled"), 0);
        glUniform1i(glGetUniformLocation(s_raytracing_prog, "u_soft_shadows"), 0);
        glUniform1i(glGetUniformLocation(s_raytracing_prog, "u_shadow_quality"), 0);
        glUniform1f(glGetUniformLocation(s_raytracing_prog, "u_light_intensity"), 1.0f);
        glUniform1i(glGetUniformLocation(s_raytracing_prog, "u_volumetric_enabled"), 0);
    }

    render_quad();

    // 4. Bloom Pass: Ping-Pong Blur on Emission
    if (cfg.bloom)
    {
        glUseProgram(s_blur_prog);
        glUniform1f(glGetUniformLocation(s_blur_prog, "u_flip_y"), 1.0f);
        int bw = std::max(1, src_w / 2);
        int bh = std::max(1, src_h / 2);
        glViewport(0, 0, bw, bh);

        // Horizontal blur
        glBindFramebuffer(GL_FRAMEBUFFER, s_bloom_fbo[0]);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, s_g_emission);
        glUniform1i(glGetUniformLocation(s_blur_prog, "u_image"), 0);
        glUniform2f(glGetUniformLocation(s_blur_prog, "u_offset"), 1.2f / bw, 0.0f);
        render_quad();

        // Vertical blur
        glBindFramebuffer(GL_FRAMEBUFFER, s_bloom_fbo[1]);
        glBindTexture(GL_TEXTURE_2D, s_bloom_texture[0]);
        glUniform2f(glGetUniformLocation(s_blur_prog, "u_offset"), 0.0f, 1.2f / bh);
        render_quad();
    }

    // 5. Final Composite Pass to Window Framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, window_w, window_h);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    int vp_x = 0, vp_y = 0, vp_w = window_w, vp_h = window_h;
    if (cfg.widescreen && src_h > 0)
    {
        float target_aspect = static_cast<float>(src_w) / static_cast<float>(src_h);
        float win_aspect = static_cast<float>(window_w) / static_cast<float>(window_h);
        if (win_aspect > target_aspect)
        {
            vp_w = static_cast<int>(window_h * target_aspect);
            vp_h = window_h;
            vp_x = (window_w - vp_w) / 2;
            vp_y = 0;
        }
        else
        {
            vp_w = window_w;
            vp_h = static_cast<int>(window_w / target_aspect);
            vp_x = 0;
            vp_y = (window_h - vp_h) / 2;
        }
    }
    glViewport(vp_x, vp_y, vp_w, vp_h);

    glUseProgram(s_composite_prog);
    glUniform1f(glGetUniformLocation(s_composite_prog, "u_flip_y"), 0.0f);
    glUniform4f(glGetUniformLocation(s_composite_prog, "u_ui_rect"), ui_u1, ui_v1, ui_u2, ui_v2);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s_lit_texture);
    glUniform1i(glGetUniformLocation(s_composite_prog, "u_lit_scene"), 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, s_bloom_texture[1]);
    glUniform1i(glGetUniformLocation(s_composite_prog, "u_bloom"), 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, s_g_albedo);
    glUniform1i(glGetUniformLocation(s_composite_prog, "u_albedo"), 2);

    glUniform1f(glGetUniformLocation(s_composite_prog, "u_bloom_intensity"), cfg.bloom_intensity);
    glUniform1i(glGetUniformLocation(s_composite_prog, "u_bloom_enabled"), cfg.bloom ? 1 : 0);
    glUniform1i(glGetUniformLocation(s_composite_prog, "u_reflections_enabled"), (in_gameplay && cfg.reflections) ? 1 : 0);
    glUniform1f(glGetUniformLocation(s_composite_prog, "u_time"), s_time);

    render_quad();

    // 6. Modern Widescreen HUD & In-Game Dashboard Pass
    RemasterHUD::get().render(window_w, window_h, vp_x, vp_y, vp_w, vp_h, src_w, src_h);

    check_dump_screenshot(window_w, window_h);
}
