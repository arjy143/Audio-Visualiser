# OpenGL Rendering Reference

Read this before working on anything in `src/render/` or `shaders/`.

---

## OpenGL 3.3 Core Profile — Why This Version

We use **OpenGL 3.3 Core Profile** because:
- Available on virtually every Linux GPU (Intel, AMD, NVIDIA) since 2010
- "Core" profile removes deprecated legacy API (immediate mode, `glBegin`/`glEnd`)
- Forces modern buffer-object workflow — better for learning and performance
- No extensions needed for what we're doing

**GLFW** creates the window and OpenGL context for us (replaces raw X11/Wayland setup).
Link with: `-lGL -lglfw`

---

## The Modern OpenGL Pipeline

```
CPU side                              GPU side
─────────────────────────────────────────────────────
Vertex data (float array)
       │
       │ glBufferSubData()  ← updates GPU buffer (no reallocation)
       ▼
  VBO (Vertex Buffer Object)  ←── lives in GPU VRAM
       │
  VAO (Vertex Array Object)   ←── remembers how to interpret VBO data
       │
       │ glDrawArrays()
       ▼
  Vertex Shader (GLSL)        ← runs once per vertex on GPU
       │  gl_Position = ...
       ▼
  Rasterisation               ← GPU converts triangles to fragments
       ▼
  Fragment Shader (GLSL)      ← runs once per pixel
       │  fragColour = ...
       ▼
  Framebuffer → screen
```

---

## VAO/VBO Setup (Do Once at Startup)

```cpp
// Spectrum bar geometry: one float per bin = the magnitude
// The vertex shader will compute bar position from gl_VertexID
GLuint vao, vbo;
glGenVertexArrays(1, &vao);
glGenBuffers(1, &vbo);

glBindVertexArray(vao);
glBindBuffer(GL_ARRAY_BUFFER, vbo);

// Allocate GPU buffer — kSpectrumBins floats, updated every frame
// GL_DYNAMIC_DRAW hints that we'll update frequently (GPU puts it in fast memory)
glBufferData(GL_ARRAY_BUFFER,
             kSpectrumBins * sizeof(float),
             nullptr,          // no data yet
             GL_DYNAMIC_DRAW);

// Attribute 0: one float per vertex (the magnitude)
glVertexAttribPointer(0, 1, GL_FLOAT, GL_FALSE, sizeof(float), nullptr);
glEnableVertexAttribArray(0);

glBindVertexArray(0); // Unbind — good practice
```

---

## Updating the GPU Buffer Every Frame

The key insight: we **never reallocate** the GPU buffer. We just overwrite its contents:

```cpp
// Every frame — O(n) memcpy to GPU, no allocation
void uploadSpectrum(GLuint vbo, std::span<const float> spectrum) noexcept {
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferSubData(GL_ARRAY_BUFFER,
                    0,                                    // offset
                    spectrum.size_bytes(),                // size
                    spectrum.data());                     // pointer
}
```

⚡ **Performance note:** `glBufferSubData` is faster than `glBufferData` (no realloc).
For even better performance later, look into **persistent mapped buffers** (OpenGL 4.4),
but `glBufferSubData` is fine for 60fps with ~8KB of spectrum data.

---

## Spectrum Vertex Shader

The vertex shader runs once per spectrum bin. It receives the magnitude as attribute 0
and computes the bar's position on screen using `gl_VertexID`:

```glsl
// shaders/spectrum.vert
#version 330 core

layout(location = 0) in float magnitude;  // dB value for this bin

uniform int   uBinCount;     // total number of bins
uniform float uMinDB;        // e.g. -90.0
uniform float uMaxDB;        // e.g.   0.0

out float vMagnitude; // passed to fragment shader for colouring

void main() {
    // Map bin index to X position in [-1, 1] (NDC)
    float x = (float(gl_VertexID) / float(uBinCount - 1)) * 2.0 - 1.0;

    // Map dB magnitude to Y height in [0, 1] then to [-1, 1]
    float normalised = clamp((magnitude - uMinDB) / (uMaxDB - uMinDB), 0.0, 1.0);
    float y = normalised * 2.0 - 1.0;

    gl_Position = vec4(x, y, 0.0, 1.0);
    vMagnitude = normalised;
}
```

---

## Spectrum Fragment Shader

```glsl
// shaders/spectrum.frag
#version 330 core

in  float vMagnitude;
out vec4  fragColour;

void main() {
    // Colour gradient: blue (quiet) → cyan → green → yellow → red (loud)
    vec3 colour;
    if (vMagnitude < 0.5) {
        colour = mix(vec3(0.0, 0.2, 1.0), vec3(0.0, 1.0, 0.5), vMagnitude * 2.0);
    } else {
        colour = mix(vec3(0.0, 1.0, 0.5), vec3(1.0, 0.2, 0.0), (vMagnitude - 0.5) * 2.0);
    }
    fragColour = vec4(colour, 1.0);
}
```

---

## Drawing Spectrum Bars

Draw as `GL_LINES` from the bottom of the screen to the magnitude height:

```cpp
// Option A: GL_LINE_STRIP — connects all points (waveform style)
glDrawArrays(GL_LINE_STRIP, 0, kSpectrumBins);

// Option B: GL_POINTS — one dot per bin (dot spectrum)
glPointSize(2.0f);
glDrawArrays(GL_POINTS, 0, kSpectrumBins);

// Option C: For bars, you need 2 vertices per bin (bottom + top)
// Upload both y=−1.0 and y=magnitude per bin, draw as GL_LINES
```

---

## Render Loop Structure

```cpp
while (!glfwWindowShouldClose(window)) {
    // 1. Poll input events (keyboard, resize, close)
    glfwPollEvents();

    // 2. DSP — read audio, run FFT (see dsp-patterns.md)
    analyser.update();

    // 3. Upload spectrum to GPU
    uploadSpectrum(vbo, analyser.spectrum());

    // 4. Clear and draw
    glClearColor(0.05f, 0.05f, 0.08f, 1.0f); // Dark background
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(shaderProgram);
    glBindVertexArray(vao);
    glDrawArrays(GL_LINE_STRIP, 0, kSpectrumBins);

    // 5. Swap front/back buffers (presents frame, blocks on vsync)
    glfwSwapBuffers(window);
}
```

⚡ `glfwSwapBuffers` blocks until vsync — this naturally caps us at 60fps with no
sleep needed. The time spent blocking is "free" for the CPU.

---

## Shader Loading

```cpp
[[nodiscard]] GLuint loadShader(GLenum type, std::string_view source) {
    GLuint shader = glCreateShader(type);
    const char* src = source.data();
    const GLint len = static_cast<GLint>(source.size());
    glShaderSource(shader, 1, &src, &len);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        throw std::runtime_error(std::string("Shader compile error: ") + log);
    }
    return shader;
}
```

---

## OpenGL Error Checking (Debug Builds Only)

```cpp
// After any GL call in debug builds:
#ifndef NDEBUG
#define GL_CHECK() checkGLError(__FILE__, __LINE__)
void checkGLError(const char* file, int line) {
    GLenum err;
    while ((err = glGetError()) != GL_NO_ERROR) {
        fprintf(stderr, "OpenGL error 0x%x at %s:%d\n", err, file, line);
    }
}
#else
#define GL_CHECK() ((void)0)
#endif
```

Or enable OpenGL debug output (preferred, requires 4.3+):
```cpp
glEnable(GL_DEBUG_OUTPUT);
glDebugMessageCallback([](GLenum, GLenum, GLuint, GLenum severity,
                          GLsizei, const GLchar* msg, const void*) {
    if (severity != GL_DEBUG_SEVERITY_NOTIFICATION)
        fprintf(stderr, "GL: %s\n", msg);
}, nullptr);
```
