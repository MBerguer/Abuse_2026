#include "common.h"
#include "remaster_gl.h"
#include "remaster_shaders.h"
#include "remaster_config.h"
#include "remaster_lighting.h"
#include "remaster_hud.h"
#include "remaster_hd.h"
#include "remaster_particles.h"
#include "image.h"
#include "file_utils.h"
#include "loader2.h"
#include "game.h"

#include <iostream>
#include <vector>
#include <cstring>

bool RemasterGL::s_initialized = false;
int RemasterGL::s_width = 320;
int RemasterGL::s_height = 200;
int RemasterGL::s_fbo_w = 1920;
int RemasterGL::s_fbo_h = 1200;

GLuint RemasterGL::s_quad_vao = 0;
GLuint RemasterGL::s_quad_vbo = 0;
GLuint RemasterGL::s_source_texture = 0;
GLuint RemasterGL::s_sprite_mask_texture = 0;

std::vector<uint8_t> RemasterGL::s_tile_snapshot;
std::vector<uint8_t> RemasterGL::s_sprite_mask;

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
GLuint RemasterGL::s_ai_materials_tex = 0;

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

    const int SCALE_FACTOR = 6;
    s_fbo_w = w * SCALE_FACTOR;
    s_fbo_h = h * SCALE_FACTOR;

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

    auto create_tex = [](GLuint &tex, GLenum internalFormat, GLenum format, GLenum type, GLint filter, int tw, int th) {
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, tw, th, 0, format, type, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    };

    create_tex(s_g_albedo, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR, s_fbo_w, s_fbo_h);
    create_tex(s_g_normal, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR, s_fbo_w, s_fbo_h);
    create_tex(s_g_emission, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR, s_fbo_w, s_fbo_h);
    create_tex(s_g_occlusion, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR, s_fbo_w, s_fbo_h);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_g_albedo, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, s_g_normal, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, s_g_emission, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3, GL_TEXTURE_2D, s_g_occlusion, 0);

    GLenum drawBuffers[4] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3};
    glDrawBuffers(4, drawBuffers);

    // 2. Lit Scene FBO
    glGenFramebuffers(1, &s_lit_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, s_lit_fbo);
    create_tex(s_lit_texture, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR, s_fbo_w, s_fbo_h);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_lit_texture, 0);

    // 3. Bloom Ping-Pong FBOs (Downsampled half size for performance and wider radius)
    int bw = std::max(1, s_fbo_w / 2);
    int bh = std::max(1, s_fbo_h / 2);
    glGenFramebuffers(2, s_bloom_fbo);
    for (int i = 0; i < 2; i++)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, s_bloom_fbo[i]);
        create_tex(s_bloom_texture[i], GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR, bw, bh);
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

    // Create dynamic sprite mask texture
    glGenTextures(1, &s_sprite_mask_texture);
    glBindTexture(GL_TEXTURE_2D, s_sprite_mask_texture);
    std::vector<uint8_t> blank_mask(screen_w * screen_h, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, screen_w, screen_h, 0, GL_RED, GL_UNSIGNED_BYTE, blank_mask.data());
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
    RemasterHD::get().init();

    // Load AI Master Materials (1024x1024 2D Texture Array)
    if (!s_ai_materials_tex)
    {
        std::vector<std::string> candidate_paths;
        char *prefix = get_filename_prefix();
        if (prefix && prefix[0])
        {
            candidate_paths.push_back(std::string(prefix) + "/hd/ai_materials.bin");
            candidate_paths.push_back(std::string(prefix) + "/ai_materials.bin");
        }
        candidate_paths.push_back("data/hd/ai_materials.bin");
        candidate_paths.push_back("abuse.app/Contents/Resources/data/hd/ai_materials.bin");
        candidate_paths.push_back("../Resources/data/hd/ai_materials.bin");

        for (const auto &p : candidate_paths)
        {
            FILE *f_ai = fopen(p.c_str(), "rb");
            if (!f_ai)
            {
                std::cout << "[RemasterGL] AI mat path: " << p << " -> fopen failed" << std::endl;
                continue;
            }
            std::cout << "[RemasterGL] AI mat path: " << p << " -> OPENED!" << std::endl;
            char sig[8];
            size_t n_sig = fread(sig, 1, 8, f_ai);
            std::cout << "[RemasterGL] sig read: " << n_sig << " bytes, cmp=" << (n_sig == 8 ? std::memcmp(sig, "AIMAT1.0", 8) : -1) << std::endl;
            if (n_sig == 8 && std::memcmp(sig, "AIMAT1.0", 8) == 0)
                {
                    uint32_t count = 0, mw = 0, mh = 0;
                    if (fread(&count, 4, 1, f_ai) == 1 &&
                        fread(&mw, 4, 1, f_ai) == 1 &&
                        fread(&mh, 4, 1, f_ai) == 1 && count > 0 && mw > 0 && mh > 0)
                    {
                        std::vector<uint8_t> data((size_t)count * mw * mh * 4);
                        if (fread(data.data(), 1, data.size(), f_ai) == data.size())
                        {
                            glGenTextures(1, &s_ai_materials_tex);
                            glBindTexture(GL_TEXTURE_2D_ARRAY, s_ai_materials_tex);
                            glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, mw, mh, count, 0, GL_RGBA, GL_UNSIGNED_BYTE, data.data());
                            glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
                            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
                            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
                            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
                            glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
                            std::cout << "[RemasterGL] Loaded " << count << " AI Master Materials (" << mw << "x" << mh << ") into GPU Texture Array from " << p << std::endl;
                            fclose(f_ai);
                            break;
                        }
                    }
                }
                fclose(f_ai);
            }
        }

    RemasterParticles::get().init();

    s_initialized = true;
    std::cout << "[RemasterGL] Initialized Modern 2D Deferred GPU Pipeline (GL 3.3 Core)." << std::endl;
    return true;
}

void RemasterGL::shutdown()
{
    if (!s_initialized) return;

    RemasterParticles::get().shutdown();
    RemasterHUD::get().cleanup();
    RemasterHD::get().cleanup();

    if (s_ai_materials_tex)
    {
        glDeleteTextures(1, &s_ai_materials_tex);
        s_ai_materials_tex = 0;
    }

    glDeleteVertexArrays(1, &s_quad_vao);
    glDeleteBuffers(1, &s_quad_vbo);
    glDeleteTextures(1, &s_source_texture);
    if (s_sprite_mask_texture)
    {
        glDeleteTextures(1, &s_sprite_mask_texture);
        s_sprite_mask_texture = 0;
    }
    s_tile_snapshot.clear();
    s_sprite_mask.clear();

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

void RemasterGL::begin_object_drawing(void *screen_ptr)
{
    image *im = static_cast<image *>(screen_ptr);
    if (!im) return;
    int size = im->Size().x * im->Size().y;
    if (s_tile_snapshot.size() < (size_t)size)
        s_tile_snapshot.resize(size);
    std::memcpy(s_tile_snapshot.data(), im->scan_line(0), size);
}

void RemasterGL::end_object_drawing(void *screen_ptr)
{
    image *im = static_cast<image *>(screen_ptr);
    if (!im) return;
    int size = im->Size().x * im->Size().y;
    if (s_sprite_mask.size() < (size_t)size)
        s_sprite_mask.resize(size);

    const uint8_t *cur = im->scan_line(0);
    const uint8_t *snap = s_tile_snapshot.data();
    for (int i = 0; i < size; i++)
    {
        s_sprite_mask[i] = (cur[i] != snap[i]) ? 255 : 0;
    }
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

static void check_dump_screenshot(int window_w, int window_h, bool in_gameplay)
{
    const char *dump_menu = getenv("ABUSE_DUMP_MENU");
    if (dump_menu)
    {
        extern Game *the_game;
        if (!the_game || the_game->state != MENU_STATE)
            return;
        static int s_menu_counter = 0;
        s_menu_counter++;
        int target = atoi(dump_menu);
        if (s_menu_counter >= target)
        {
            const char *out_path = getenv("ABUSE_DUMP_PATH");
            std::string final_png = out_path ? out_path : "/Users/mberguer/.gemini/antigravity/brain/7829aad7-21ac-4465-ba25-1fade3429f56/test_menu.png";
            std::string tmp_bmp = "/Users/mberguer/.gemini/antigravity/brain/7829aad7-21ac-4465-ba25-1fade3429f56/scratch/abuse_menu.bmp";
            RemasterGL::capture_screenshot(tmp_bmp.c_str(), window_w, window_h);
            std::string cmd = "rm -f \"" + final_png + "\" && sips -s format png \"" + tmp_bmp + "\" --out \"" + final_png + "\" >/dev/null 2>&1";
            system(cmd.c_str());
            printf("[RemasterGL] Captured menu frame %d to %s\n", s_menu_counter, final_png.c_str());
            exit(0);
        }
        return;
    }

    if (!in_gameplay) return;
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

void RemasterGL::render_classic(const void *pixel_data, int src_w, int src_h, int window_w, int window_h, bool in_gameplay)
{
    auto &cfg = RemasterConfig::get();

    // Update source texture
    glActiveTexture(GL_TEXTURE0);
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

    // Remaster HUD & Notification pass (only if enabled)
    if (cfg.enabled)
        RemasterHUD::get().render(window_w, window_h, vp_x, vp_y, vp_w, vp_h, src_w, src_h);

    check_dump_screenshot(window_w, window_h, in_gameplay);
}

void RemasterGL::render_frame(const void *pixel_data, int src_w, int src_h, int window_w, int window_h,
                              bool in_gameplay,
                              const std::vector<RemasterUIRect> &ui_rects,
                              int level_ambient,
                              float cam_x, float cam_y)
{
    s_time += 0.01667f;
    auto &cfg = RemasterConfig::get();

    if (!cfg.enabled || !in_gameplay)
    {
        render_classic(pixel_data, src_w, src_h, window_w, window_h, in_gameplay);
        return;
    }

    if (src_w != s_width || src_h != s_height)
    {
        resize(src_w, src_h);
    }

    // Flatten UI rects for GPU upload
    int num_rects = std::min((int)ui_rects.size(), 16);
    float rect_data[16 * 4] = {0};
    for (int i = 0; i < num_rects; i++)
    {
        rect_data[i * 4 + 0] = ui_rects[i].u1;
        rect_data[i * 4 + 1] = ui_rects[i].v1;
        rect_data[i * 4 + 2] = ui_rects[i].u2;
        rect_data[i * 4 + 3] = ui_rects[i].v2;
    }

    // 1. Upload scene pixel data to GPU texture
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s_source_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, src_w, src_h, 0, GL_BGRA, GL_UNSIGNED_BYTE, pixel_data);

    if (getenv("DUMP_RAW_PIXELS") && in_gameplay)
    {
        static int s_raw_done = 0;
        if (s_raw_done++ == 24)
        {
            std::string tmp_bmp = "/Users/mberguer/.gemini/antigravity/brain/7829aad7-21ac-4465-ba25-1fade3429f56/scratch/raw_pixel_data.bmp";
            std::string final_png = "/Users/mberguer/.gemini/antigravity/brain/7829aad7-21ac-4465-ba25-1fade3429f56/raw_pixel_data.png";
            uint32_t row_size = ((src_w * 3 + 3) / 4) * 4;
            uint32_t image_size = row_size * src_h;
            uint32_t file_size = 54 + image_size;
            uint8_t header[54] = {
                'B', 'M',
                static_cast<uint8_t>(file_size), static_cast<uint8_t>(file_size >> 8),
                static_cast<uint8_t>(file_size >> 16), static_cast<uint8_t>(file_size >> 24),
                0, 0, 0, 0, 54, 0, 0, 0, 40, 0, 0, 0,
                static_cast<uint8_t>(src_w), static_cast<uint8_t>(src_w >> 8),
                static_cast<uint8_t>(src_w >> 16), static_cast<uint8_t>(src_w >> 24),
                static_cast<uint8_t>(src_h), static_cast<uint8_t>(src_h >> 8),
                static_cast<uint8_t>(src_h >> 16), static_cast<uint8_t>(src_h >> 24),
                1, 0, 24, 0, 0, 0, 0, 0,
                static_cast<uint8_t>(image_size), static_cast<uint8_t>(image_size >> 8),
                static_cast<uint8_t>(image_size >> 16), static_cast<uint8_t>(image_size >> 24),
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
            };
            FILE *f = fopen(tmp_bmp.c_str(), "wb");
            if (f)
            {
                fwrite(header, 1, 54, f);
                const uint8_t *src_px = static_cast<const uint8_t *>(pixel_data);
                std::vector<uint8_t> row(row_size, 0);
                for (int y = src_h - 1; y >= 0; y--)
                {
                    for (int x = 0; x < src_w; x++)
                    {
                        int src_idx = (y * src_w + x) * 4;
                        row[x * 3 + 0] = src_px[src_idx + 0]; // B
                        row[x * 3 + 1] = src_px[src_idx + 1]; // G
                        row[x * 3 + 2] = src_px[src_idx + 2]; // R
                    }
                    fwrite(row.data(), 1, row_size, f);
                }
                fclose(f);
                std::string cmd = "sips -s format png \"" + tmp_bmp + "\" --out \"" + final_png + "\" >/dev/null 2>&1";
                system(cmd.c_str());
                printf("[RemasterGL] Wrote raw pixel data to %s\n", final_png.c_str());
            }
        }
    }

    // 2. G-Buffer Pass: Extract Albedo, Normal (Sobel filter), Emission, Occlusion
    glBindFramebuffer(GL_FRAMEBUFFER, s_gbuffer_fbo);
    glViewport(0, 0, s_fbo_w, s_fbo_h);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(s_gbuffer_prog);
    glUniform1f(glGetUniformLocation(s_gbuffer_prog, "u_flip_y"), 1.0f);
    glUniform1i(glGetUniformLocation(s_gbuffer_prog, "u_num_ui_rects"), num_rects);
    if (num_rects > 0)
    {
        glUniform4fv(glGetUniformLocation(s_gbuffer_prog, "u_ui_rects"), num_rects, rect_data);
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s_source_texture);
    glUniform1i(glGetUniformLocation(s_gbuffer_prog, "u_scene"), 0);
    glUniform2f(glGetUniformLocation(s_gbuffer_prog, "u_src_size"), (float)src_w, (float)src_h);
    glUniform2f(glGetUniformLocation(s_gbuffer_prog, "u_fbo_size"), (float)s_fbo_w, (float)s_fbo_h);
    glUniform1i(glGetUniformLocation(s_gbuffer_prog, "u_hd_scaler"), cfg.hd_textures ? 1 : 0);
    glUniform1f(glGetUniformLocation(s_gbuffer_prog, "u_normal_strength"), cfg.normal_strength);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, s_sprite_mask_texture);
    if (!s_sprite_mask.empty() && s_sprite_mask.size() >= (size_t)(src_w * src_h))
    {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, src_w, src_h, GL_RED, GL_UNSIGNED_BYTE, s_sprite_mask.data());
    }
    glUniform1i(glGetUniformLocation(s_gbuffer_prog, "u_sprite_mask"), 1);

    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, s_ai_materials_tex);
    glUniform1i(glGetUniformLocation(s_gbuffer_prog, "u_ai_materials"), 5);
    glUniform1i(glGetUniformLocation(s_gbuffer_prog, "u_has_ai_materials"), (s_ai_materials_tex != 0 && cfg.hd_textures) ? 1 : 0);
    glUniform2f(glGetUniformLocation(s_gbuffer_prog, "u_cam_pos"), cam_x, cam_y);

    if (cfg.hd_textures) {
        int ft_w = f_wid > 0 ? f_wid : 16;
        int ft_h = f_hi > 0 ? f_hi : 16;
        RemasterHD::get().update_frame_data((int)cam_x, (int)cam_y, src_w, src_h, ft_w, ft_h, 0, 0);
        glActiveTexture(GL_TEXTURE6);
        glBindTexture(GL_TEXTURE_2D, RemasterHD::get().get_tile_grid_tex());
        glUniform1i(glGetUniformLocation(s_gbuffer_prog, "u_tile_grid"), 6);
        float grid_bounds[4] = {
            (float)RemasterHD::get().get_grid_ox(), (float)RemasterHD::get().get_grid_oy(),
            (float)RemasterHD::get().get_grid_w(), (float)RemasterHD::get().get_grid_h()
        };
        glUniform4fv(glGetUniformLocation(s_gbuffer_prog, "u_grid_bounds"), 1, grid_bounds);
        glUniform2f(glGetUniformLocation(s_gbuffer_prog, "u_tile_size"), (float)ft_w, (float)ft_h);
    }

    render_quad();

    if (cfg.hd_textures) {
        glActiveTexture(GL_TEXTURE6);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);

    // 3. 2D Ray Tracing & Dynamic Lighting Pass
    glBindFramebuffer(GL_FRAMEBUFFER, s_lit_fbo);
    glViewport(0, 0, s_fbo_w, s_fbo_h);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(s_raytracing_prog);
    glUniform1f(glGetUniformLocation(s_raytracing_prog, "u_flip_y"), 1.0f);
    glUniform1i(glGetUniformLocation(s_raytracing_prog, "u_num_ui_rects"), num_rects);
    if (num_rects > 0)
    {
        glUniform4fv(glGetUniformLocation(s_raytracing_prog, "u_ui_rects"), num_rects, rect_data);
    }

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
    glUniform1f(glGetUniformLocation(s_raytracing_prog, "u_aspect"), (float)src_w / (float)src_h);

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
        // High-contrast atmospheric sci-fi ambient, modulated by level's authentic darkness (0..63)
        float level_factor = std::clamp((float)level_ambient / 32.0f, 0.40f, 1.25f);
        float amb = std::max(0.20f, 0.45f * cfg.ambient_intensity * level_factor);
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

    // 3b. Particles Layer: Render particles onto lit scene and into emission buffer
    if (in_gameplay)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, s_lit_fbo);
        RemasterParticles::get().render_all(cam_x, cam_y, (float)src_w, (float)src_h, s_fbo_w, s_fbo_h);

        if (cfg.bloom)
        {
            glBindFramebuffer(GL_FRAMEBUFFER, s_gbuffer_fbo);
            GLenum emDraw = GL_COLOR_ATTACHMENT2;
            glDrawBuffers(1, &emDraw);
            RemasterParticles::get().render_emissive(cam_x, cam_y, (float)src_w, (float)src_h, s_fbo_w, s_fbo_h);
            GLenum allDraw[4] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3};
            glDrawBuffers(4, allDraw);
        }
    }

    // 4. Bloom Pass: Ping-Pong Blur on Emission
    if (cfg.bloom)
    {
        glUseProgram(s_blur_prog);
        glUniform1f(glGetUniformLocation(s_blur_prog, "u_flip_y"), 1.0f);
        int bw = std::max(1, s_fbo_w / 2);
        int bh = std::max(1, s_fbo_h / 2);
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
    glUniform1i(glGetUniformLocation(s_composite_prog, "u_num_ui_rects"), num_rects);
    if (num_rects > 0)
    {
        glUniform4fv(glGetUniformLocation(s_composite_prog, "u_ui_rects"), num_rects, rect_data);
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s_lit_texture);
    glUniform1i(glGetUniformLocation(s_composite_prog, "u_lit_scene"), 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, s_bloom_texture[1]);
    glUniform1i(glGetUniformLocation(s_composite_prog, "u_bloom"), 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, s_g_albedo);
    glUniform1i(glGetUniformLocation(s_composite_prog, "u_albedo"), 2);

    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, s_g_normal);
    glUniform1i(glGetUniformLocation(s_composite_prog, "u_normal"), 3);

    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, s_g_occlusion);
    glUniform1i(glGetUniformLocation(s_composite_prog, "u_occlusion"), 4);

    glUniform1f(glGetUniformLocation(s_composite_prog, "u_bloom_intensity"), cfg.bloom_intensity);
    glUniform1i(glGetUniformLocation(s_composite_prog, "u_bloom_enabled"), cfg.bloom ? 1 : 0);
    glUniform1i(glGetUniformLocation(s_composite_prog, "u_reflections_enabled"), (in_gameplay && cfg.reflections) ? 1 : 0);
    glUniform1f(glGetUniformLocation(s_composite_prog, "u_time"), s_time);

    render_quad();

    // 6. Modern Widescreen HUD & In-Game Dashboard Pass
    RemasterHUD::get().render(window_w, window_h, vp_x, vp_y, vp_w, vp_h, src_w, src_h);

    check_dump_screenshot(window_w, window_h, in_gameplay);
}
