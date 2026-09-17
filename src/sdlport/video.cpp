/*
 *  Abuse - dark 2D side-scrolling platform game
 *  Copyright (c) 2001 Anthony Kruize <trandor@labyrinth.net.au>
 *  Copyright (c) 2005-2011 Sam Hocevar <sam@hocevar.net>
 *  Copyright (c) 2024 Andrej Pancik
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software Foundation,
 *  Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 */

#if defined HAVE_CONFIG_H
#include "config.h"
#endif

#include <memory>
#include <stdexcept>
#include <string>
#include <array>
#include <cstring>

#include "SDL.h"

#include "common.h"
#include "video.h"
#include "image.h"
#include "setup.h"
#include "errorui.h"
#include "view.h"
#include "remaster/remaster_gl.h"
#include "remaster/remaster_config.h"
#include "remaster/remaster_lighting.h"
#include "game.h"
#include "level.h"
#include "cop.h"
#include "sbar.h"
#include "compiled.h"

extern view *player_list;

// Core SDL components
SDL_Window *window = nullptr;
SDL_GLContext gl_context = nullptr;
SDL_Renderer *renderer = nullptr;
SDL_Surface *surface = nullptr; // 8-bit paletted surface for game rendering
SDL_Surface *screen = nullptr;  // 32-bit RGB surface for final display
SDL_Texture *texture = nullptr; // GPU texture for hardware-accelerated rendering
image *main_screen = nullptr;   // Game's primary drawing surface

// Factors for converting window coordinates to game coordinates
int mouse_xpad = 0;   // Horizontal padding for letterboxing
int mouse_ypad = 0;   // Vertical padding for letterboxing
int mouse_xscale = 1; // 16.16 fixed point horizontal scale factor
int mouse_yscale = 1; // 16.16 fixed point vertical scale factor

// Virtual screen dimensions (game coordinates)
int xres = 0;
int yres = 0;

extern palette *lastl;
extern Settings settings;

bool has_notch()
{
// Only proceed with detection on macOS
#ifdef __APPLE__
    SDL_Rect display_bounds;
    SDL_Rect usable_bounds;

    // Get the bounds of the main display (index 0)
    if (SDL_GetDisplayBounds(0, &display_bounds) != 0 ||
        SDL_GetDisplayUsableBounds(0, &usable_bounds) != 0)
    {
        return false; // Return false on error
    }

    // On MacBooks with notch:
    // - display_bounds represents the full screen including the notch area
    // - usable_bounds represents the screen excluding the notch area
    // Therefore, if there's a notch:
    // - The usable height will be less than display height
    // - The usable width will equal the display width    
    return (usable_bounds.h < display_bounds.h &&
            usable_bounds.w == display_bounds.w &&
            (display_bounds.h - usable_bounds.h) >= 32 &&
            (display_bounds.h - usable_bounds.h) <= 37);
#else
    return false; // Not macOS, so definitely no notch
#endif
}

//
// Calculate mouse scaling factors and window aspect ratio
// This needs to be exposed for event handling
//
void handle_window_resize()
{
    int window_width, window_height;
    SDL_GetWindowSize(window, &window_width, &window_height);

    if (!RemasterConfig::get().widescreen)
    {
        float target_aspect = static_cast<float>(xres) / yres;
        float current_aspect = static_cast<float>(window_width) / window_height;

        if (current_aspect > target_aspect)
            window_width = static_cast<int>(window_height * target_aspect);
        else
            window_height = static_cast<int>(window_width / target_aspect);

        if(target_aspect != current_aspect)
            SDL_SetWindowSize(window, window_width, window_height);
    }

    SDL_Rect viewport = {0, 0, window_width, window_height};
    if (RemasterConfig::get().widescreen && yres > 0)
    {
        float target_aspect = static_cast<float>(xres) / yres;
        float current_aspect = static_cast<float>(window_width) / window_height;
        if (current_aspect > target_aspect)
        {
            viewport.w = static_cast<int>(window_height * target_aspect);
            viewport.h = window_height;
            viewport.x = (window_width - viewport.w) / 2;
            viewport.y = 0;
        }
        else
        {
            viewport.w = window_width;
            viewport.h = static_cast<int>(window_width / target_aspect);
            viewport.x = 0;
            viewport.y = (window_height - viewport.h) / 2;
        }
        mouse_xscale = (viewport.w << 16) / xres;
        mouse_yscale = (viewport.h << 16) / yres;
        mouse_xpad = viewport.x;
        mouse_ypad = viewport.y;
    }
    else
    {
        if (renderer)
            SDL_RenderGetViewport(renderer, &viewport);

        mouse_xscale = (window_width << 16) / xres;
        mouse_yscale = (window_height << 16) / yres;

        mouse_xpad = viewport.x;
        mouse_ypad = viewport.y;
    }
}

//
// Initialize video subsystem
//
void set_mode(int argc, char **argv)
{
    try
    {
        // Auto-detect screen dimensions if not specified in settings
        if (settings.screen_width == 0 || settings.screen_height == 0)
        {
            int display_width, display_height;
            if(has_notch())
            {                
                SDL_Rect usable_bounds;
                if (SDL_GetDisplayUsableBounds(0, &usable_bounds) != 0)
                {
                    throw std::runtime_error(SDL_GetError());
                }

                display_width = usable_bounds.w;
                display_height = usable_bounds.h;
            }
            else
            {
                SDL_DisplayMode display_mode;
                if (SDL_GetCurrentDisplayMode(0, &display_mode) != 0)
                {
                    throw std::runtime_error(SDL_GetError());
                }

                display_width = display_mode.w;
                display_height = display_mode.h;
            }                        
            
            if (settings.fullscreen)
            {
                settings.screen_width = display_width;
                settings.screen_height = display_height;
            }
            else
            {
                if (settings.screen_width == 0)
                {
                    settings.screen_width = settings.virtual_width;
                }
                settings.screen_height = (int)(settings.screen_width * ((float)display_height / display_width));
            }
        }

        // Calculate virtual resolution preserving aspect ratio
        xres = settings.virtual_width;
        yres = settings.virtual_height ? settings.virtual_height : (int)(xres * ((float)settings.screen_height / settings.screen_width));

        // Set up window flags based on display settings
        uint32_t flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
        if (settings.fullscreen == 1)
            flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
        else if (settings.fullscreen == 2)
            flags |= SDL_WINDOW_FULLSCREEN;
        if (settings.borderless)
            flags |= SDL_WINDOW_BORDERLESS;

        flags |= SDL_WINDOW_OPENGL;

        // Configure OpenGL 3.2+ Core Profile attributes
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

        // Initialize rendering pipeline:
        window = SDL_CreateWindow("Abuse 2026 Remaster",
                                  SDL_WINDOWPOS_CENTERED,
                                  SDL_WINDOWPOS_CENTERED,
                                  settings.screen_width,
                                  settings.screen_height,
                                  flags);
        if (!window)
        {
            throw std::runtime_error(SDL_GetError());
        }

        int window_pixel_width, window_pixel_height;
        SDL_GetWindowSizeInPixels(window, &window_pixel_width, &window_pixel_height);

        float scale_factor = static_cast<float>(window_pixel_width) / settings.screen_width;

        // Try creating OpenGL Context
        gl_context = SDL_GL_CreateContext(window);
        if (gl_context)
        {
            SDL_GL_MakeCurrent(window, gl_context);
            SDL_GL_SetSwapInterval(settings.vsync ? 1 : 0);
            RemasterConfig::get().load();
            RemasterGL::init(xres, yres);
        }
        else
        {
            uint32_t render_flags = SDL_RENDERER_ACCELERATED;
            if (settings.vsync)
                render_flags |= SDL_RENDERER_PRESENTVSYNC;

            renderer = SDL_CreateRenderer(window, -1, render_flags);
            if (!renderer)
            {
                renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
                if (!renderer)
                {
                    throw std::runtime_error(SDL_GetError());
                }
            }

            SDL_RenderSetScale(renderer, scale_factor, scale_factor);
            SDL_RenderSetLogicalSize(renderer, xres, yres);
            SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,
                        settings.linear_filter ? "1" : "0");
        }

        main_screen = new image(ivec2(xres, yres), nullptr, 2);
        if (!main_screen)
        {
            throw std::runtime_error("Unable to create screen image");
        }
        main_screen->clear();

        surface = SDL_CreateRGBSurface(0, xres, yres, 8, 0, 0, 0, 0);
        if (!surface)
        {
            throw std::runtime_error(SDL_GetError());
        }

        screen = SDL_CreateRGBSurface(0, xres, yres, 32, 0, 0, 0, 0);
        if (!screen)
        {
            throw std::runtime_error(SDL_GetError());
        }

        if (renderer)
        {
            texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                        SDL_TEXTUREACCESS_STREAMING,
                                        xres, yres);
        }

        handle_window_resize();
        SDL_ShowCursor(0);
    }
    catch (const std::exception &e)
    {
        show_startup_error("Video initialization failed: %s", e.what());
        exit(1);
    }
}

//
// Update video settings (fullscreen, scaling)
//
void toggle_fullscreen()
{
    // Cycle through fullscreen modes: windowed -> fullscreen desktop -> fullscreen
    settings.fullscreen = (settings.fullscreen + 1) % 3;

    uint32_t flags = 0;
    if (settings.fullscreen == 1)
        flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    else if (settings.fullscreen == 2)
        flags |= SDL_WINDOW_FULLSCREEN;
    if (settings.borderless)
        flags |= SDL_WINDOW_BORDERLESS;

    SDL_SetWindowFullscreen(window, flags);
    handle_window_resize();
}

//
// Cleanup video subsystem
//
void close_graphics()
{    
    if (lastl)
    {
        delete lastl;
        lastl = nullptr;
    }

    if (surface)
    {
        SDL_FreeSurface(surface);
        surface = nullptr;
    }

    if (screen)
    {
        SDL_FreeSurface(screen);
        screen = nullptr;
    }

    if (texture)
    {
        SDL_DestroyTexture(texture);
        texture = nullptr;
    }

    if (main_screen)
    {
        delete main_screen;
        main_screen = nullptr;
    }

    if (renderer)
    {
        SDL_DestroyRenderer(renderer);
        renderer = nullptr;
    }

    if (gl_context)
    {
        RemasterConfig::get().save();
        RemasterGL::shutdown();
        SDL_GL_DeleteContext(gl_context);
        gl_context = nullptr;
    }

    if (window)
    {
        SDL_DestroyWindow(window);
        window = nullptr;
    }
}

//
// Draw a portion of an image to the screen
//
void put_part_image(image *im, int x, int y, int x1, int y1, int x2, int y2)
{    
    CHECK(x1 >= 0 && x2 >= x1 && y1 >= 0 && y2 >= y1);
    
    // Skip if completely off screen
    if (y > yres || x > xres)
        return;    

    // Clip drawing region to screen boundaries
    if (x < 0)
    {
        x1 += -x;
        x = 0;
    }

    int xe = (x + (x2 - x1) >= xres) ? xres - x + x1 - 1 : x2;

    if (y < 0)
    {
        y1 += -y;
        y = 0;
    }

    int ye = (y + (y2 - y1) >= yres) ? yres - y + y1 - 1 : y2;

    // Nothing to draw after clipping
    if (x1 >= xe || y1 >= ye)
        return;

    if (SDL_MUSTLOCK(surface))
    {
        SDL_LockSurface(surface);
    }

    const int width = xe - x1;
    const int height = ye - y1;
    uint8_t *base_pixel = static_cast<uint8_t *>(surface->pixels) + y * surface->pitch + x;
    const int dst_pitch = surface->pitch;
        
    for (int i = 0; i < height; i++)
    {
        const uint8_t *src = im->scan_line(y1 + i) + x1;
        uint8_t *dst = base_pixel + i * dst_pitch;
        std::memcpy(dst, src, width);
    }

    if (SDL_MUSTLOCK(surface))
    {
        SDL_UnlockSurface(surface);
    }
}

//
// Load and apply a palette
//
void palette::load()
{
    delete lastl;
    lastl = copy();

    // Force to only 256 colours
    if (ncolors > 256)
        ncolors = 256;

    // Set up SDL color palette
    std::array<SDL_Color, 256> colors{};
    for (int i = 0; i < ncolors; i++)
    {
        colors[i] = {
            static_cast<uint8_t>(red(i)),
            static_cast<uint8_t>(green(i)),
            static_cast<uint8_t>(blue(i)),
            255};
    }

    // Update palette and redraw
    SDL_SetPaletteColors(surface->format->palette, colors.data(), 0, ncolors);
    update_window_done();
}

void palette::load_nice()
{
    load();
}

//
// Update the window with current screen contents
//
void update_window_done()
{
    // Convert paletted surface to 32-bit RGB for display
    SDL_BlitSurface(surface, nullptr, screen, nullptr);

    int win_w = settings.screen_width;
    int win_h = settings.screen_height;
    if (window)
        SDL_GetWindowSizeInPixels(window, &win_w, &win_h);

    if (gl_context && RemasterGL::is_initialized())
    {
        bool in_gameplay = (the_game && (the_game->state == RUN_STATE || the_game->state == PAUSE_STATE) && current_level);

        if (RemasterConfig::get().enabled && in_gameplay && player_list)
        {
            game_object *player_obj = player_list->m_focus;
            bool is_dead = false;
            bool is_climbing = false;

            if (player_obj)
            {
                if (!player_obj->alive() || player_obj->hp() <= 0 ||
                    player_obj->state == dead || player_obj->state == dieing ||
                    player_obj->state == S_blown_back_dead || player_obj->aistate() == 2)
                {
                    is_dead = true;
                }

                if (player_obj->state == S_climbing ||
                    player_obj->state == S_climb_on ||
                    player_obj->state == S_climb_off)
                {
                    is_climbing = true;
                }
            }

            bool flashlight_on = (!is_dead && !is_climbing);

            int p_world_x = player_list->x_center();
            int p_world_y = player_list->y_center() - 16;
            int muzzle_world_x = p_world_x;
            int muzzle_world_y = p_world_y;
            float aim_dir_x = 0.0f, aim_dir_y = 0.0f;
            get_player_muzzle_pos(player_list, muzzle_world_x, muzzle_world_y, aim_dir_x, aim_dir_y);

            int aim_world_x = player_list->pointer_x;
            int aim_world_y = player_list->pointer_y;

            ivec2 p_screen = the_game ? the_game->GameToMouse(ivec2(p_world_x, p_world_y), player_list)
                                      : ivec2(p_world_x - player_list->xoff(), p_world_y - player_list->yoff());
            ivec2 m_screen = the_game ? the_game->GameToMouse(ivec2(muzzle_world_x, muzzle_world_y), player_list)
                                      : ivec2(muzzle_world_x - player_list->xoff(), muzzle_world_y - player_list->yoff());
            ivec2 aim_screen = the_game ? the_game->GameToMouse(ivec2(aim_world_x, aim_world_y), player_list)
                                        : ivec2(aim_world_x - player_list->xoff(), aim_world_y - player_list->yoff());

            if (aim_world_x == 0 && aim_world_y == 0 && wm)
            {
                aim_screen = wm->GetMousePos();
            }

            // Direction from player shoulder/torso to gun muzzle (physical barrel orientation)
            float b_dx = (float)(m_screen.x - p_screen.x);
            float b_dy = (float)(m_screen.y - p_screen.y);
            float b_len = std::hypot(b_dx, b_dy);

            // Direction from player shoulder/torso to mouse cursor
            float a_dx = (float)(aim_screen.x - p_screen.x);
            float a_dy = (float)(aim_screen.y - p_screen.y);
            float a_len = std::hypot(a_dx, a_dy);

            float out_dir_x = 0.0f;
            float out_dir_y = 0.0f;

            if (b_len > 2.0f)
            {
                float b_norm_x = b_dx / b_len;
                float b_norm_y = b_dy / b_len;

                if (a_len > 4.0f)
                {
                    float a_norm_x = a_dx / a_len;
                    float a_norm_y = a_dy / a_len;

                    // Ensure aim is in the forward hemisphere of the gun barrel
                    float dot = a_norm_x * b_norm_x + a_norm_y * b_norm_y;
                    if (dot > 0.25f)
                    {
                        out_dir_x = a_norm_x;
                        out_dir_y = a_norm_y;
                    }
                    else
                    {
                        // Cursor is behind or inside: follow weapon barrel pointing outward!
                        out_dir_x = b_norm_x;
                        out_dir_y = b_norm_y;
                    }
                }
                else
                {
                    // Cursor directly on the player center: shoot forward along the barrel!
                    out_dir_x = b_norm_x;
                    out_dir_y = b_norm_y;
                }
            }
            else
            {
                out_dir_x = (player_obj && player_obj->direction < 0) ? -1.0f : 1.0f;
                out_dir_y = 0.0f;
            }

            RemasterLighting::get().update_frame_lights(
                player_list->xoff(), player_list->yoff(), xres, yres,
                p_screen.x, p_screen.y,
                aim_screen.x, aim_screen.y,
                player_list->b1_suggestion != 0,
                out_dir_x, out_dir_y,
                m_screen.x, m_screen.y,
                flashlight_on
            );
        }
        else
        {
            RemasterLighting::get().clear();
        }

        std::vector<RemasterUIRect> ui_rects;

        // 1. UI Layer (Status Bar): sits on top of game world, unshaded by ambient darkness or raytracing
        int sx1, sy1, sx2, sy2;
        if (in_gameplay && sbar.get_area(sx1, sy1, sx2, sy2))
        {
            RemasterUIRect r;
            r.u1 = (float)sx1 / (float)xres;
            r.v1 = (float)sy1 / (float)yres;
            r.u2 = (float)sx2 / (float)xres;
            r.v2 = 1.0f;
            ui_rects.push_back(r);
        }

        // 2. All active GUI Windows (Save Game dialogs, Load Game, preview thumbnails, Automap, Volume, Popups, etc.)
        if (wm)
        {
            for (Jwindow *w = wm->m_first; w; w = w->next)
            {
                if (!w->is_hidden() && w->m_size.x > 0 && w->m_size.y > 0)
                {
                    int wx1 = std::max(0, w->m_pos.x - 1);
                    int wy1 = std::max(0, w->m_pos.y - 1);
                    int wx2 = std::min(xres, w->m_pos.x + w->m_size.x + 1);
                    int wy2 = std::min(yres, w->m_pos.y + w->m_size.y + 1);

                    RemasterUIRect r;
                    r.u1 = (float)wx1 / (float)xres;
                    r.v1 = (float)wy1 / (float)yres;
                    r.u2 = (float)wx2 / (float)xres;
                    r.v2 = (float)wy2 / (float)yres;
                    ui_rects.push_back(r);
                }
            }
        }

        // 3. In-game guidance text & training instructions ("This console saves the state of the game", station alerts, hints)
        if (the_game && the_game->is_showing_help())
        {
            int banner_h = (wm && wm->font()) ? (wm->font()->Size().y + 14) : 26;
            RemasterUIRect r;
            r.u1 = 0.0f;
            r.v1 = 0.0f;
            r.u2 = 1.0f;
            r.v2 = std::min(1.0f, (float)banner_h / (float)yres);
            ui_rects.push_back(r);
        }

        // 4. In-game Pause overlay
        if (the_game && the_game->state == PAUSE_STATE)
        {
            RemasterUIRect r;
            r.u1 = 0.35f;
            r.v1 = 0.0f;
            r.u2 = 0.65f;
            r.v2 = 0.15f;
            ui_rects.push_back(r);
        }


        if (RemasterConfig::get().show_pos_debug && in_gameplay && player_list && player_list->m_focus)
        {
            extern int f_wid, f_hi;
            int px = player_list->m_focus->x;
            int py = player_list->m_focus->y;
            int tw = f_wid > 0 ? f_wid : 16;
            int th = f_hi > 0 ? f_hi : 16;
            int tx = (px >= 0) ? (px / tw) : -1;
            int ty = (py >= 0) ? (py / th) : -1;

            if (window)
            {
                static int s_title_timer = 0;
                if (s_title_timer++ % 10 == 0)
                {
                    char title_buf[128];
                    snprintf(title_buf, sizeof(title_buf), "Abuse 2026 | POS: X=%d Y=%d | TILE: [%d, %d]", px, py, tx, ty);
                    SDL_SetWindowTitle(window, title_buf);
                }
            }
        }

        int level_amb = (in_gameplay && player_list) ? player_list->ambient : 32;
        float cam_x = (in_gameplay && player_list) ? (float)player_list->xoff() : 0.0f;
        float cam_y = (in_gameplay && player_list) ? (float)player_list->yoff() : 0.0f;
        RemasterGL::render_frame(screen->pixels, xres, yres, win_w, win_h, in_gameplay,
                                 ui_rects, level_amb, cam_x, cam_y);
        SDL_GL_SwapWindow(window);
    }
    else if (renderer && texture)
    {
        // Update GPU texture and render to display
        SDL_UpdateTexture(texture, nullptr, screen->pixels, screen->pitch);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);
    }
}