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

    // Bass pulse: unit circle geometry, uploaded once and never changed.
    // The vertex shader scales it by uRadius each draw call — no per-frame CPU work.
    {
        constexpr float k_pi2 = 2.0f * 3.14159265f;
        std::array<float, k_pulse_segs * 2> unit_circle{};
        for (int i = 0; i < k_pulse_segs; ++i)
        {
            const float a = k_pi2 * static_cast<float>(i) / static_cast<float>(k_pulse_segs);
            unit_circle[static_cast<size_t>(i) * 2]     = std::cos(a);
            unit_circle[static_cast<size_t>(i) * 2 + 1] = std::sin(a);
        }
        pulse_shader_   = std::make_unique<ShaderProgram>("shaders/pulse.vert", "shaders/pulse.frag");
        pulse_uniforms_ = { pulse_shader_->uniform("uRadius"), pulse_shader_->uniform("uColour") };

        glGenVertexArrays(1, &pulse_vao_);
        glGenBuffers(1, &pulse_vbo_);
        glBindVertexArray(pulse_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, pulse_vbo_);
        glBufferData(GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(unit_circle.size() * sizeof(float)),
            unit_circle.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
        glEnableVertexAttribArray(0);
        glBindVertexArray(0);
    }

    // Corona VAO/VBO — same (x,y,r,g,b,a) per-vertex format as the web, dynamic each frame
    glGenVertexArrays(1, &corona_vao_);
    glGenBuffers(1, &corona_vbo_);
    glBindVertexArray(corona_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, corona_vbo_);
    glBufferData(GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(corona_data_.size() * sizeof(float)),
        nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
        reinterpret_cast<const void*>(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    // Resonance web VAO/VBO — stride 6 floats: (x, y, r, g, b, a)
    web_shader_ = std::make_unique<ShaderProgram>("shaders/web.vert", "shaders/web.frag");

    glGenVertexArrays(1, &web_vao_);
    glGenBuffers(1, &web_vbo_);
    glBindVertexArray(web_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, web_vbo_);
    glBufferData(GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(web_data_.size() * sizeof(float)),
        nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
        reinterpret_cast<const void*>(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
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
    glDeleteBuffers(1, &corona_vbo_);
    glDeleteVertexArrays(1, &corona_vao_);
    glDeleteBuffers(1, &pulse_vbo_);
    glDeleteVertexArrays(1, &pulse_vao_);
    glDeleteBuffers(1, &web_vbo_);
    glDeleteVertexArrays(1, &web_vao_);
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
        else                        current_symmetry_ = 4;
    }

    // ── Broadband transient detector (snare / hi-hat / clap) ─────────────────
    // Watches bins 10–500 (~120 Hz – 5.9 kHz), the range where sharp percussive
    // attacks live. Decays 3× faster than beat_kick_ so snare hits feel snappy.
    {
        constexpr int k_onset_lo = 10, k_onset_hi = 500;
        float onset_sum = 0.0f;
        for (int i = k_onset_lo; i < k_onset_hi; ++i)
            onset_sum += spectrum[static_cast<size_t>(i)];
        const float onset_norm = std::clamp(
            (onset_sum / static_cast<float>(k_onset_hi - k_onset_lo) - k_min_db) / (k_max_db - k_min_db),
            0.0f, 1.0f);
        onset_avg_  = onset_avg_  * 0.97f + onset_norm * 0.03f;
        onset_kick_ *= 0.80f;
        if (onset_kick_ < 0.4f && onset_norm > onset_avg_ * 1.45f && onset_avg_ > 0.04f)
            onset_kick_ = 0.9f;
    }
    const bool onset_hit = onset_kick_ >= 0.88f;  // true only on the frame it was set

    // ── Mode switching ────────────────────────────────────────────
    // Auto-advance every ~90 s, but only on a beat so the cut feels musical.
    // M key forces an immediate switch at any time.
    constexpr int k_mode_duration = 90 * 60;   // frames (~90 s at 60 fps)
    constexpr int k_num_modes     = 7;

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

        case 5: // Bass pulse — frequency corona + staggered membrane + ripple rings
        {
            constexpr float k_base_r     = 0.28f;
            constexpr float k_expand_spd = 0.016f;  // NDC/frame — ring crosses screen in ~34 frames
            constexpr float k_ring_fade  = 0.922f;  // lifetime tuned to match expansion speed
            constexpr float k_outer_r    = 0.82f;
            constexpr float k_bin_hz     = 48000.0f / static_cast<float>(dsp::k_FFT_size);
            constexpr float k_spoke_len  = 0.24f;   // max spoke length from frequency energy alone
            constexpr float k_flare      = 0.20f;   // every beat flares ALL spokes by this much

            const float impulse = std::max(beat_kick_, onset_kick_);
            const float mem_r   = k_base_r + bass_norm * 0.06f + impulse * 0.32f;

            // ── Update ring pool ──────────────────────────────────────────────────
            for (auto& ring : pulse_rings_)
            {
                if (ring.alpha < 0.01f) continue;
                ring.radius += k_expand_spd;
                ring.alpha  *= k_ring_fade;
            }

            // Spawn helper — silently drops if all slots are taken
            auto spawn = [&](float r, float a)
            {
                for (auto& ring : pulse_rings_)
                    if (ring.alpha < 0.01f) { ring.radius = r; ring.alpha = a; return; }
            };

            if (beat_hit)
            {
                spawn(mem_r, 1.0f);
                spawn(mem_r * 0.86f, 0.65f);  // trailing inner ring launched simultaneously
            }
            if (onset_hit && !beat_hit)
                spawn(mem_r, 0.68f);

            // ── Build corona: 64 radial spokes, coloured by frequency band ───────
            // k_flare ensures every spoke pops outward on a beat even when that
            // frequency band is quiet — creating a uniform starburst on each hit.
            // A slow rotation keeps the display alive during sustained quiet passages.
            const float rot = time * 0.40f;

            auto hsv_rgb = [](float h, float s, float v) -> std::array<float, 3>
            {
                const float c = v * s;
                const float x = c * (1.0f - std::abs(std::fmod(h * 6.0f, 2.0f) - 1.0f));
                const float m = v - c;
                float r = m, g = m, b = m;
                switch (static_cast<int>(h * 6.0f) % 6)
                {
                    case 0: r += c; g += x; break;
                    case 1: r += x; g += c; break;
                    case 2: g += c; b += x; break;
                    case 3: g += x; b += c; break;
                    case 4: r += x; b += c; break;
                    case 5: r += c; b += x; break;
                }
                return {r, g, b};
            };

            for (int i = 0; i < k_corona_n; ++i)
            {
                const float angle = k_two_pi * static_cast<float>(i)
                                    / static_cast<float>(k_corona_n) + rot;
                const float ca = std::cos(angle), sa = std::sin(angle);

                const float t    = static_cast<float>(i) / static_cast<float>(k_corona_n - 1);
                const float freq = 40.0f * std::pow(8000.0f / 40.0f, t);
                const int   bin  = std::clamp(static_cast<int>(freq / k_bin_hz),
                                              1, static_cast<int>(dsp::k_spectrum_bins) - 1);
                const int lo = std::max(1, bin - 2);
                const int hi = std::min(static_cast<int>(dsp::k_spectrum_bins) - 1, bin + 2);
                float peak = k_min_db;
                for (int b = lo; b <= hi; ++b)
                    peak = std::max<float>(peak, spectrum[static_cast<size_t>(b)]);
                const float energy = std::clamp((peak - k_min_db) / (k_max_db - k_min_db),
                                               0.0f, 1.0f);

                const float r_base = mem_r + 0.025f;
                const float r_tip  = std::min(r_base + energy * k_spoke_len + impulse * k_flare,
                                              k_outer_r - 0.012f);

                const auto  col    = hsv_rgb(t, 0.80f, 0.40f + energy * 0.60f);
                const float tip_a  = std::min(0.15f + energy * 0.85f + impulse * 0.40f, 1.0f);

                const size_t vi = static_cast<size_t>(i) * 12;
                corona_data_[vi + 0]  = r_base * ca;
                corona_data_[vi + 1]  = r_base * sa;
                corona_data_[vi + 2]  = col[0] * 0.25f;
                corona_data_[vi + 3]  = col[1] * 0.25f;
                corona_data_[vi + 4]  = col[2] * 0.25f;
                corona_data_[vi + 5]  = tip_a  * 0.15f;
                corona_data_[vi + 6]  = r_tip * ca;
                corona_data_[vi + 7]  = r_tip * sa;
                corona_data_[vi + 8]  = col[0];
                corona_data_[vi + 9]  = col[1];
                corona_data_[vi + 10] = col[2];
                corona_data_[vi + 11] = tip_a;
            }

            glBindBuffer(GL_ARRAY_BUFFER, corona_vbo_);
            glBufferSubData(GL_ARRAY_BUFFER, 0,
                static_cast<GLsizeiptr>(corona_data_.size() * sizeof(float)),
                corona_data_.data());
            web_shader_->bind();
            glBindVertexArray(corona_vao_);
            glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(k_corona_n * 2));

            // ── Ripple rings ──────────────────────────────────────────────────────
            pulse_shader_->bind();
            glBindVertexArray(pulse_vao_);
            const GLsizei k_segs = static_cast<GLsizei>(k_pulse_segs);

            for (const auto& ring : pulse_rings_)
            {
                if (ring.alpha < 0.01f) continue;
                glUniform1f(pulse_uniforms_.radius, ring.radius);
                glUniform4f(pulse_uniforms_.colour, 0.40f, 0.85f, 1.0f, ring.alpha * 0.90f);
                glDrawArrays(GL_LINE_LOOP, 0, k_segs);
            }

            // ── 3 staggered membrane rings ────────────────────────────────────────
            // ⚡ Performance note: each ring uses impulse^(1 + i*0.3). As impulse
            // decays frame-by-frame, higher powers decay faster — inner rings spring
            // back first, giving the illusion of a thick vibrating cone in 3D.
            for (int i = 0; i < 3; ++i)
            {
                const float staggered = std::pow(impulse, 1.0f + static_cast<float>(i) * 0.30f);
                const float r         = k_base_r + bass_norm * 0.06f + staggered * 0.32f;
                const float hot       = std::min(bass_norm * 0.4f + staggered * 0.9f, 1.0f);
                const float alpha     = (0.95f - static_cast<float>(i) * 0.20f)
                                        * (0.65f + hot * 0.35f);
                glUniform1f(pulse_uniforms_.radius, r);
                glUniform4f(pulse_uniforms_.colour,
                    0.55f + hot * 0.45f,
                    0.75f + hot * 0.25f,
                    1.00f,
                    alpha);
                glDrawArrays(GL_LINE_LOOP, 0, k_segs);
            }

            // Outer basket — anchor ring that gives the whole display a sense of scale
            glUniform1f(pulse_uniforms_.radius, k_outer_r);
            glUniform4f(pulse_uniforms_.colour, 0.20f, 0.35f, 0.55f, 0.30f);
            glDrawArrays(GL_LINE_LOOP, 0, k_segs);

            break;
        }

        case 6: // Resonance web — full mesh of frequency-band vertices with per-vertex colour
        {
            // 🧠 Concept: with alpha blending, every overlapping semi-transparent line
            // adds more brightness at its pixels. Lines share k_web_n*(k_web_n-1)/2
            // crossing pairs — where many frequencies are simultaneously active the
            // crossing knots accumulate toward white.

            constexpr float k_bin_hz   = 48000.0f / static_cast<float>(dsp::k_FFT_size);
            constexpr float k_base_r   = 0.45f;   // resting circle radius in NDC
            constexpr float k_radial   = 0.40f;   // max additional outward push from energy
            constexpr float k_alpha    = 0.20f;   // per-line alpha; crossings stack this
            constexpr float k_freq_lo  = 40.0f;
            constexpr float k_freq_hi  = 8000.0f;

            // Extract energy per band
            float energies[k_web_n];
            for (int i = 0; i < k_web_n; ++i)
            {
                const float t    = static_cast<float>(i) / static_cast<float>(k_web_n - 1);
                const float freq = k_freq_lo * std::pow(k_freq_hi / k_freq_lo, t);
                const int   bin  = std::clamp(static_cast<int>(freq / k_bin_hz),
                                              1, static_cast<int>(dsp::k_spectrum_bins) - 1);
                const int lo = std::max(1, bin - 1);
                const int hi = std::min(static_cast<int>(dsp::k_spectrum_bins) - 1, bin + 1);
                float peak = k_min_db;
                for (int b = lo; b <= hi; ++b)
                    peak = std::max<float>(peak, spectrum[static_cast<size_t>(b)]);
                energies[i] = std::clamp((peak - k_min_db) / (k_max_db - k_min_db), 0.0f, 1.0f);
            }

            // Compute vertex positions — each vertex on a regular k_web_n-gon,
            // displaced outward by its frequency band's energy
            float vx[k_web_n], vy[k_web_n];
            for (int i = 0; i < k_web_n; ++i)
            {
                // Start from top (-π/2) so bass sits at the bottom, treble at top
                const float angle = k_two_pi * static_cast<float>(i) / static_cast<float>(k_web_n)
                                    - 3.14159265f * 0.5f;
                const float r = k_base_r
                              + energies[i] * k_radial * (1.0f + beat_kick_ * 0.3f);
                vx[i] = r * std::cos(angle);
                vy[i] = r * std::sin(angle);
            }

            // HSV → RGB, computed on CPU so the fragment shader stays trivial
            auto hsv_rgb = [](float h, float s, float v) -> std::array<float, 3>
            {
                const float c  = v * s;
                const float x  = c * (1.0f - std::abs(std::fmod(h * 6.0f, 2.0f) - 1.0f));
                const float m  = v - c;
                float r = m, g = m, b = m;
                switch (static_cast<int>(h * 6.0f) % 6)
                {
                    case 0: r += c; g += x; break;
                    case 1: r += x; g += c; break;
                    case 2: g += c; b += x; break;
                    case 3: g += x; b += c; break;
                    case 4: r += x; b += c; break;
                    case 5: r += c; b += x; break;
                }
                return {r, g, b};
            };

            // Fill VBO: for each pair (i,j) write 2 vertices × 6 floats
            size_t vi = 0;
            for (int i = 0; i < k_web_n; ++i)
            {
                for (int j = i + 1; j < k_web_n; ++j)
                {
                    const float bright_i = 0.25f + 0.75f * energies[i];
                    const float bright_j = 0.25f + 0.75f * energies[j];
                    const auto  ci = hsv_rgb(static_cast<float>(i) / static_cast<float>(k_web_n),
                                             0.75f, bright_i);
                    const auto  cj = hsv_rgb(static_cast<float>(j) / static_cast<float>(k_web_n),
                                             0.75f, bright_j);
                    web_data_[vi++] = vx[i]; web_data_[vi++] = vy[i];
                    web_data_[vi++] = ci[0]; web_data_[vi++] = ci[1];
                    web_data_[vi++] = ci[2]; web_data_[vi++] = k_alpha;
                    web_data_[vi++] = vx[j]; web_data_[vi++] = vy[j];
                    web_data_[vi++] = cj[0]; web_data_[vi++] = cj[1];
                    web_data_[vi++] = cj[2]; web_data_[vi++] = k_alpha;
                }
            }

            glBindBuffer(GL_ARRAY_BUFFER, web_vbo_);
            glBufferSubData(GL_ARRAY_BUFFER, 0,
                static_cast<GLsizeiptr>(web_data_.size() * sizeof(float)),
                web_data_.data());

            web_shader_->bind();
            glBindVertexArray(web_vao_);
            glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(k_web_lines * 2));
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
