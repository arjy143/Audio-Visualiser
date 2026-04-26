# AudioVis — Architecture

## System Overview

AudioVis is a pipeline application. Audio samples flow in one direction:

```
Linux Audio Subsystem (PipeWire)
         │
         │  raw PCM samples (float32, interleaved)
         ▼
  [ Audio Callback ]  ← real-time thread, budget: < 1ms
         │
         │  write to SPSC ring buffer (lock-free)
         ▼
  [ Ring Buffer ]  ← shared memory, single producer / single consumer
         │
         │  read by render thread
         ▼
  [ DSP Pipeline ]  ← main thread, runs every frame
    1. Dequeue samples
    2. Apply window function (Hann)
    3. Run FFT
    4. Convert to magnitude/dB
    5. Peak detection + smoothing
         │
         │  frequency-domain data (N/2 + 1 bins)
         ▼
  [ Render Pipeline ]  ← main thread, 60fps
    1. Upload spectrum data to GPU (glBufferSubData)
    2. Draw spectrum bars / waveform
    3. Swap buffers (vsync)
```

---

## Key Data Types

```cpp
// Audio domain
using Sample = float;                    // 32-bit PCM
constexpr size_t kFFTSize    = 4096;    // Must be power of 2
constexpr size_t kSpectrumBins = kFFTSize / 2 + 1;
constexpr uint32_t kSampleRate = 48000; // Hz — standard for PipeWire

// Ring buffer between audio and render thread
// Holds 65536 samples = ~1.4 seconds at 48kHz — plenty of headroom
constexpr size_t kRingBufferSize = 65536; // Must be power of 2

// Render domain
constexpr int kTargetFPS = 60;
constexpr double kFrameBudgetMs = 1000.0 / kTargetFPS; // ~16.67ms
```

---

## Component Responsibilities

### `audio/capture.hpp` — PipeWire Stream
- Opens a PipeWire stream for audio capture
- Registers the real-time callback
- Converts interleaved stereo → mono (average channels) in the callback
- Writes samples to the ring buffer
- Handles connect/disconnect events

### `audio/ring_buffer.hpp` — Lock-Free SPSC Ring Buffer
- Template: `RingBuffer<T, Capacity>` where Capacity is power-of-2
- Producer (audio thread): `push(const T* data, size_t count)` — never blocks
- Consumer (render thread): `pop(T* dest, size_t count)` — returns available count
- Uses `std::atomic` with acquire/release ordering — no mutex
- `alignas(64)` on head and tail to prevent false sharing

### `dsp/fft.hpp` — FFT Wrapper
- Wraps KissFFT or a hand-rolled Cooley-Tukey implementation
- Manages the FFT plan (allocated once at startup)
- Input: `std::span<const float>` of kFFTSize samples
- Output: `std::span<float>` of kSpectrumBins magnitudes (in dB)

### `dsp/window.hpp` — Windowing Functions
- `hann(std::span<float> samples)` — multiply in-place, most common
- `blackman(std::span<float> samples)` — better sidelobe suppression
- Coefficients computed at compile time (`constexpr` array)
- SIMD-accelerated multiplication (AVX2 if available)

### `dsp/analyser.hpp` — Spectrum Analysis
- Reads from ring buffer, runs window + FFT pipeline
- Applies smoothing (exponential moving average) to reduce flicker
- Peak detection for visual accents
- Output: stable `std::array<float, kSpectrumBins>` ready for GPU upload

### `render/renderer.hpp` — OpenGL Context
- GLFW window creation and OpenGL 3.3 Core context
- VAO/VBO setup for spectrum geometry
- Main render loop with frame timing

### `render/shader.hpp` — GLSL Management
- Loads vertex + fragment shaders from files
- Compiles, links, validates
- Caches uniform locations

### `render/visualiser.hpp` — Per-Frame Drawing
- Receives spectrum data from analyser
- Updates GPU buffer (glBufferSubData — no reallocation)
- Issues draw calls
- Manages visual parameters (colour, scale, smoothing)

---

## Memory Budget

Everything is pre-allocated at startup. Zero allocations during runtime.

| Component | Memory | Notes |
|---|---|---|
| Ring buffer | 256 KB | 65536 × float32 |
| FFT input buffer | 16 KB | 4096 × float32 |
| FFT output buffer | 16 KB | 4096 × complex float |
| Windowing coefficients | 16 KB | 4096 × float32, constexpr |
| Spectrum bins | 8 KB | 2049 × float32 |
| GPU vertex buffer | 16 KB | 2049 vertices × 2 floats (x, magnitude) |
| **Total** | **~350 KB** | Well within L2 cache on most CPUs |

---

## Error Handling Strategy

- **Startup errors** (PipeWire connect fail, OpenGL init fail): throw `std::runtime_error`,
  caught in `main()`, printed and exit cleanly
- **Runtime errors in hot path**: never throw — use return codes or atomic error flags
- **Audio callback errors**: set an atomic flag, log on next render thread wake
- **Shader compile errors**: logged with GLSL error string, program falls back or exits

---

## Build Targets

| Target | Purpose |
|---|---|
| `audiovis` | Main application |
| `audiovis_tests` | Unit tests (Catch2) |
| `audiovis_bench` | Micro-benchmarks (Google Benchmark, optional) |
