#include "render/renderer.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>

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
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    window_ = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!window_)
        throw std::runtime_error("Failed to create GLFW window");

    glfwMakeContextCurrent(window_);
    glewInit();

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

    // Particle shader + geometry — 4 floats per particle (x, y, life, hue)
    particle_shader_ = std::make_unique<ShaderProgram>("shaders/particle.vert", "shaders/particle.frag");

    glGenVertexArrays(1, &particle_vao_);
    glGenBuffers(1, &particle_vbo_);
    glBindVertexArray(particle_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, particle_vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(k_max_particles * 4 * sizeof(float)), nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * static_cast<GLsizei>(sizeof(float)), nullptr);
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
    glDeleteBuffers(1, &particle_vbo_);
    glDeleteVertexArrays(1, &particle_vao_);
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
    constexpr float k_min_db   = -90.0f;
    constexpr float k_max_db   = -25.0f;
    constexpr float k_two_pi   = 2.0f * 3.14159265f;
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

    // ── Spawn particles on beat ───────────────────────────────────
    if (beat_hit)
    {
        constexpr int k_spawn = 30;
        for (int p = 0; p < k_spawn && particle_count_ < k_max_particles; ++p)
        {
            const auto  bin   = static_cast<size_t>(std::rand()) % dsp::k_spectrum_bins;
            const float freq  = static_cast<float>(bin) * 24000.0f / static_cast<float>(dsp::k_spectrum_bins - 1);
            const float t     = std::log(std::max(freq, 20.0f) / 20.0f) / std::log(24000.0f / 20.0f);
            const float angle = t * k_two_pi + time * 0.08f;

            const float norm  = std::clamp((spectrum[bin] - k_min_db) / (k_max_db - k_min_db), 0.0f, 1.0f);
            const float r     = 0.05f + bass_norm * 0.15f + norm * 0.82f;

            const float x  = r * std::cos(angle);
            const float y  = r * std::sin(angle);
            const float da = (static_cast<float>(std::rand() % 1000) / 1000.0f - 0.5f) * 0.8f;
            const float spd = 0.003f + static_cast<float>(std::rand() % 1000) / 1000.0f * 0.004f;

            particles_[particle_count_++] = {x, y,
                std::cos(angle + da) * spd,
                std::sin(angle + da) * spd,
                1.0f, t};
        }
    }

    // ── Update particles ──────────────────────────────────────────
    {
        size_t alive = 0;
        for (size_t i = 0; i < particle_count_; ++i)
        {
            Particle& p = particles_[i];
            p.x    += p.vx;
            p.y    += p.vy;
            p.life *= 0.96f;
            if (p.life > 0.01f)
                particles_[alive++] = p;
        }
        particle_count_ = alive;
    }

    // ── Pass 1: fade + spectrum + particles → scene FBO ──────────
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
    glUniform1f(uniforms_.scale,       1.0f);
    glUniform1f(uniforms_.fan_mode,    0.0f);
    glUniform1f(uniforms_.alpha,       1.0f);

    const GLsizei k_bins = static_cast<GLsizei>(dsp::k_spectrum_bins);

    glBindVertexArray(vao_);
    for (int i = 0; i < current_symmetry_; ++i)
    {
        const float rotation = k_two_pi * static_cast<float>(i) / static_cast<float>(current_symmetry_);
        glUniform1f(uniforms_.rotation, rotation);

        for (int flip = 0; flip < 2; ++flip)
        {
            glUniform1f(uniforms_.flip, static_cast<float>(flip));
            glDrawArrays(GL_LINE_LOOP, 0, k_bins);
        }
    }

    // Draw particles into the same scene FBO (they get bloom too)
    if (particle_count_ > 0)
    {
        std::array<float, k_max_particles * 4> pdata{};
        for (size_t i = 0; i < particle_count_; ++i)
        {
            pdata[i*4+0] = particles_[i].x;
            pdata[i*4+1] = particles_[i].y;
            pdata[i*4+2] = particles_[i].life;
            pdata[i*4+3] = particles_[i].hue;
        }
        glBindBuffer(GL_ARRAY_BUFFER, particle_vbo_);
        glBufferSubData(GL_ARRAY_BUFFER, 0,
            static_cast<GLsizeiptr>(particle_count_ * 4 * sizeof(float)), pdata.data());

        particle_shader_->bind();
        glBindVertexArray(particle_vao_);
        glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(particle_count_));
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
