#include "render/renderer.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

// Must come after GL headers; gates the X11 native-handle API in glfw3native.h
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>
#include <X11/Xatom.h>

namespace render
{

namespace
{
    void make_fbo(int w, int h, GLuint& fbo, GLuint& tex) noexcept
    {
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
}

Renderer::Renderer(dsp::Analyser& analyser, const char* title, int width, int height)
    : analyser_(analyser)
{
#ifdef GLFW_PLATFORM_X11
    // Force GLFW to use X11 even when WAYLAND_DISPLAY is set, so we can
    // apply EWMH desktop-background hints via the X11 native API.
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
#endif
    glfwInit();

    // Cover the full primary monitor — ignore the width/height arguments.
    // Guard both calls: glfwGetPrimaryMonitor() returns null if DISPLAY is wrong,
    // and passing null to glfwGetVideoMode() triggers an internal assert → SIGABRT.
    GLFWmonitor* primary = glfwGetPrimaryMonitor();
    if (!primary)
        throw std::runtime_error("GLFW: no monitor found — check DISPLAY env var");
    const GLFWvidmode* mode = glfwGetVideoMode(primary);
    if (!mode)
        throw std::runtime_error("GLFW: could not read video mode for primary monitor");
    width  = mode->width;
    height = mode->height;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);  // no title bar or border

    window_ = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!window_)
        throw std::runtime_error("Failed to create GLFW window");

    glfwMakeContextCurrent(window_);
    glewInit();

    // ── EWMH: tell KWin to treat this as a desktop-layer window ──────
    {
        Display* dpy = glfwGetX11Display();
        ::Window  xw  = glfwGetX11Window(window_);

        auto atom = [&](const char* name) {
            return XInternAtom(dpy, name, False);
        };

        // _NET_WM_WINDOW_TYPE_DESKTOP puts the window in the desktop layer
        const Atom type_desktop = atom("_NET_WM_WINDOW_TYPE_DESKTOP");
        XChangeProperty(dpy, xw,
                        atom("_NET_WM_WINDOW_TYPE"), XA_ATOM, 32, PropModeReplace,
                        reinterpret_cast<const unsigned char*>(&type_desktop), 1);

        // GLFW maps the window during glfwCreateWindow, so KWin already owns it
        // by this point.  XChangeProperty silently succeeds but the WM ignores
        // state changes on managed windows set that way.  The correct mechanism
        // is a ClientMessage sent to the root window — the WM intercepts it and
        // applies the state change itself.
        const Atom wm_state = atom("_NET_WM_STATE");
        auto request_state  = [&](Atom state_atom) {
            XEvent ev          = {};
            ev.xclient.type         = ClientMessage;
            ev.xclient.window       = xw;
            ev.xclient.message_type = wm_state;
            ev.xclient.format       = 32;
            ev.xclient.data.l[0]    = 1;                      // _NET_WM_STATE_ADD
            ev.xclient.data.l[1]    = static_cast<long>(state_atom);
            ev.xclient.data.l[2]    = 0;
            ev.xclient.data.l[3]    = 1;                      // source: application
            XSendEvent(dpy, DefaultRootWindow(dpy), False,
                       SubstructureRedirectMask | SubstructureNotifyMask, &ev);
        };

        request_state(atom("_NET_WM_STATE_BELOW"));
        request_state(atom("_NET_WM_STATE_STICKY"));
        request_state(atom("_NET_WM_STATE_SKIP_TASKBAR"));
        request_state(atom("_NET_WM_STATE_SKIP_PAGER"));

        // Pin to all virtual desktops via ClientMessage (same reason as above)
        {
            XEvent ev          = {};
            ev.xclient.type         = ClientMessage;
            ev.xclient.window       = xw;
            ev.xclient.message_type = atom("_NET_WM_DESKTOP");
            ev.xclient.format       = 32;
            ev.xclient.data.l[0]    = 0xFFFFFFFFL;            // all desktops sentinel
            ev.xclient.data.l[1]    = 1;                      // source: application
            XSendEvent(dpy, DefaultRootWindow(dpy), False,
                       SubstructureRedirectMask | SubstructureNotifyMask, &ev);
        }

        XFlush(dpy);
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_PROGRAM_POINT_SIZE);

    // Spectrum shader + geometry
    shader_ = std::make_unique<ShaderProgram>("shaders/spectrum.vert", "shaders/spectrum.frag");
    uniforms_ = {
        shader_->uniform("uBinCount"),
        shader_->uniform("uMinDB"),
        shader_->uniform("uMaxDB"),
        shader_->uniform("uMinFreq"),
        shader_->uniform("uMaxFreq"),
        shader_->uniform("uTime"),
        shader_->uniform("uRotation"),
        shader_->uniform("uFlip"),
        shader_->uniform("uFanMode"),
        shader_->uniform("uScale"),
        shader_->uniform("uAlpha"),
        shader_->uniform("uBassEnergy"),
        shader_->uniform("uBeatKick"),
    };

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, dsp::k_spectrum_bins * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 1, GL_FLOAT, GL_FALSE, sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    // Bars VAO/VBO — 2 floats per bin (inner + outer vertex share same magnitude)
    glGenVertexArrays(1, &bars_vao_);
    glGenBuffers(1, &bars_vbo_);
    glBindVertexArray(bars_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, bars_vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(dsp::k_spectrum_bins * 2 * sizeof(float)), nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 1, GL_FLOAT, GL_FALSE, sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    // Post-processing FBOs
    make_fbo(width, height, scene_fbo_, scene_tex_);
    make_fbo(width, height, ping_fbo_,  ping_tex_);
    make_fbo(width, height, pong_fbo_,  pong_tex_);

    glGenVertexArrays(1, &quad_vao_);

    blur_shader_ = std::make_unique<ShaderProgram>("shaders/quad.vert", "shaders/blur.frag");
    blur_uniforms_ = { blur_shader_->uniform("uImage"), blur_shader_->uniform("uHorizontal") };

    composite_shader_ = std::make_unique<ShaderProgram>("shaders/quad.vert", "shaders/composite.frag");
    composite_uniforms_ = { composite_shader_->uniform("uScene"), composite_shader_->uniform("uBloom") };

    fade_shader_ = std::make_unique<ShaderProgram>("shaders/quad.vert", "shaders/fade.frag");
    fade_uniforms_ = {
        fade_shader_->uniform("uBassEnergy"),
        fade_shader_->uniform("uBeatKick"),
        fade_shader_->uniform("uTime"),
    };

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    for (const GLuint fbo : {scene_fbo_, ping_fbo_, pong_fbo_})
    {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

Renderer::~Renderer()
{
    glDeleteBuffers(1, &bars_vbo_);
    glDeleteVertexArrays(1, &bars_vao_);
    glDeleteFramebuffers(1, &pong_fbo_);
    glDeleteFramebuffers(1, &ping_fbo_);
    glDeleteFramebuffers(1, &scene_fbo_);
    glDeleteTextures(1, &pong_tex_);
    glDeleteTextures(1, &ping_tex_);
    glDeleteTextures(1, &scene_tex_);
    glDeleteVertexArrays(1, &quad_vao_);
    glDeleteBuffers(1, &vbo_);
    glDeleteVertexArrays(1, &vao_);
    if (window_)
        glfwDestroyWindow(window_);
    glfwTerminate();
}

[[nodiscard]] bool Renderer::running() const noexcept
{
    return !glfwWindowShouldClose(window_);
}

void Renderer::render() noexcept
{
    glfwPollEvents();
    analyser_.update();

    const auto  spectrum  = analyser_.spectrum();
    const float time      = static_cast<float>(glfwGetTime());

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(spectrum.size_bytes()), spectrum.data());

    // ── DSP / beat detection ──────────────────────────────────────
    constexpr float k_min_db    = -90.0f;
    constexpr float k_max_db    = -25.0f;
    constexpr float k_two_pi    = 2.0f * 3.14159265f;
    constexpr int   k_bass_bins = 17;

    float bass_sum = 0.0f;
    for (int i = 0; i < k_bass_bins; ++i)
        bass_sum += spectrum[static_cast<size_t>(i)];
    const float bass_norm = std::clamp(
        (bass_sum / static_cast<float>(k_bass_bins) - k_min_db) / (k_max_db - k_min_db),
        0.0f, 1.0f);

    bass_avg_  = bass_avg_ * 0.98f + bass_norm * 0.02f;
    beat_kick_ *= 0.88f;
    const bool beat_hit = beat_kick_ < 0.5f && bass_norm > bass_avg_ * 1.5f && bass_avg_ > 0.05f;
    if (beat_hit)
    {
        beat_kick_ = 1.0f;
        if      (bass_norm > 0.75f) current_symmetry_ = 8;
        else if (bass_norm > 0.50f) current_symmetry_ = 6;
        else if (bass_norm > 0.25f) current_symmetry_ = 4;
        else                        current_symmetry_ = 4;
    }

    // ── Mode switching ────────────────────────────────────────────
    // Auto-advance every ~90 s, but only on a beat so the cut feels musical.
    // M key forces an immediate switch at any time.
    constexpr int k_mode_duration = 90 * 60;   // frames (~90 s at 60 fps)
    constexpr int k_num_modes     = 5;

    ++mode_frames_;
    const bool key_m     = glfwGetKey(window_, GLFW_KEY_M) == GLFW_PRESS;
    const bool key_trig  = key_m && !key_m_prev_;
    key_m_prev_          = key_m;

    if (key_trig || (beat_hit && mode_frames_ >= k_mode_duration))
    {
        mode_        = (mode_ + 1) % k_num_modes;
        mode_frames_ = 0;
        // Overload beat_kick_ so the bloom flares on the mode change
        beat_kick_   = std::max(beat_kick_, 1.8f);
    }

    // ── Pass 1: fade + spectrum → scene FBO ──────────────────────
    glBindFramebuffer(GL_FRAMEBUFFER, scene_fbo_);
    glEnable(GL_BLEND);

    fade_shader_->bind();
    glUniform1f(fade_uniforms_.bass_energy, bass_norm);
    glUniform1f(fade_uniforms_.beat_kick,   beat_kick_);
    glUniform1f(fade_uniforms_.time,        time);
    glBindVertexArray(quad_vao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    shader_->bind();
    glUniform1i(uniforms_.bin_count,   static_cast<int>(dsp::k_spectrum_bins));
    glUniform1f(uniforms_.min_db,      k_min_db);
    glUniform1f(uniforms_.max_db,      k_max_db);
    glUniform1f(uniforms_.min_freq,    20.0f);
    glUniform1f(uniforms_.max_freq,    24000.0f);
    glUniform1f(uniforms_.time,        time);
    glUniform1f(uniforms_.bass_energy, bass_norm);
    glUniform1f(uniforms_.beat_kick,   beat_kick_);
    glUniform1f(uniforms_.alpha,       1.0f);

    const GLsizei k_bins = static_cast<GLsizei>(dsp::k_spectrum_bins);

    // Shared lambda: draw one pass of the current VAO for all symmetry copies
    auto draw_ring = [&](GLenum primitive, GLsizei count, int flips, float scale, float alpha, float offset) {
        glUniform1f(uniforms_.scale, scale);
        glUniform1f(uniforms_.alpha, alpha);
        for (int i = 0; i < current_symmetry_; ++i)
        {
            const float rot = k_two_pi * static_cast<float>(i) / static_cast<float>(current_symmetry_) + offset;
            glUniform1f(uniforms_.rotation, rot);
            for (int flip = 0; flip < flips; ++flip)
            {
                glUniform1f(uniforms_.flip, static_cast<float>(flip));
                glDrawArrays(primitive, 0, count);
            }
        }
    };

    switch (mode_)
    {
        case 0: // Aura — flowing kaleidoscope lines
        {
            glUniform1f(uniforms_.fan_mode, 0.0f);
            glBindVertexArray(vao_);
            draw_ring(GL_LINE_LOOP, k_bins, 2, 1.0f, 1.0f, 0.0f);
            break;
        }

        case 1: // Tunnel — 6 nested rings creating a zoom-into-infinity effect
        {
            glUniform1f(uniforms_.fan_mode, 0.0f);
            glBindVertexArray(vao_);
            constexpr int k_rings = 6;
            for (int ring = 0; ring < k_rings; ++ring)
            {
                const float frac   = static_cast<float>(ring) / static_cast<float>(k_rings - 1);
                const float scale  = 0.2f + 0.8f * frac;
                const float alpha  = 0.25f + 0.75f * frac;
                // Offset each ring's rotation so the layers feel independent
                const float offset = static_cast<float>(ring) * 0.28f;
                draw_ring(GL_LINE_LOOP, k_bins, 2, scale, alpha, offset);
            }
            break;
        }

        case 2: // Bars — radial spokes from inner circle to spectrum amplitude
        {
            // Upload doubled magnitudes: [m0,m0, m1,m1, ...]
            for (size_t i = 0; i < dsp::k_spectrum_bins; ++i)
            {
                bars_data_[i * 2]     = spectrum[i];
                bars_data_[i * 2 + 1] = spectrum[i];
            }
            glBindBuffer(GL_ARRAY_BUFFER, bars_vbo_);
            glBufferSubData(GL_ARRAY_BUFFER, 0,
                static_cast<GLsizeiptr>(dsp::k_spectrum_bins * 2 * sizeof(float)),
                bars_data_.data());

            glUniform1f(uniforms_.fan_mode, 2.0f);
            glBindVertexArray(bars_vao_);
            draw_ring(GL_LINES, static_cast<GLsizei>(dsp::k_spectrum_bins * 2), 2, 1.0f, 1.0f, 0.0f);
            break;
        }

        case 3: // Burst — mirrored constellation of points
        {
            glUniform1f(uniforms_.fan_mode, 3.0f);
            glBindVertexArray(vao_);
            draw_ring(GL_POINTS, k_bins, 2, 1.0f, 1.0f, 0.0f);
            break;
        }

        case 4: // Circle — single unadorned ring, full spectrum once around
        {
            glUniform1f(uniforms_.fan_mode, 0.0f);
            glUniform1f(uniforms_.scale,    1.0f);
            glUniform1f(uniforms_.alpha,    1.0f);
            glUniform1f(uniforms_.rotation, 0.0f);
            glUniform1f(uniforms_.flip,     0.0f);
            glBindVertexArray(vao_);
            glDrawArrays(GL_LINE_LOOP, 0, k_bins);
            break;
        }
    }

    // ── Passes 2-5: Gaussian blur ─────────────────────────────────
    glDisable(GL_BLEND);
    blur_shader_->bind();
    glBindVertexArray(quad_vao_);

    const GLuint blur_sequence[4][2] = {
        {scene_tex_, ping_fbo_},
        {ping_tex_,  pong_fbo_},
        {pong_tex_,  ping_fbo_},
        {ping_tex_,  pong_fbo_},
    };
    const int horizontal[4] = {1, 0, 1, 0};

    for (int pass = 0; pass < 4; ++pass)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, blur_sequence[pass][1]);
        glClear(GL_COLOR_BUFFER_BIT);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, blur_sequence[pass][0]);
        glUniform1i(blur_uniforms_.image,      0);
        glUniform1i(blur_uniforms_.horizontal, horizontal[pass]);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    // ── Pass 6: composite to screen ──────────────────────────────
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    composite_shader_->bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, scene_tex_);
    glUniform1i(composite_uniforms_.scene, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, pong_tex_);
    glUniform1i(composite_uniforms_.bloom, 1);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glfwSwapBuffers(window_);
}

}
