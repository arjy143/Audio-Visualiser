# C++ HPC Reference — AudioVis Edition

Read this before making any performance-sensitive decision. This is a distilled
version of the full HPC guide, focused on patterns that appear in this project.

---

## The Audio Visualiser Performance Budget

```
Audio callback:  < 1ms    (PipeWire deadline — non-negotiable)
DSP pipeline:    < 4ms    (FFT + windowing + smoothing)
Render frame:    < 8ms    (upload + draw + shader)
Frame total:     < 16ms   (60fps)
```

If any step exceeds its budget, you'll see either xruns (audio glitches) or
dropped frames. Profile before assuming where the time goes.

---

## Memory Layout Rules for This Project

### Ring Buffer
```cpp
// CORRECT: head and tail on separate cache lines
struct alignas(64) RingBuffer {
    std::atomic<size_t> head_{0};  // 8 bytes + 56 bytes padding
    alignas(64) std::atomic<size_t> tail_{0}; // own cache line
    float buffer_[65536];
};
// Without alignas(64): every audio thread write to head_ invalidates
// the render thread's cached tail_ — false sharing, ~10x slower
```

### DSP Buffers
```cpp
// Align FFT buffers for SIMD — 32-byte alignment for AVX2
alignas(32) float inputBuffer_[kFFTSize];
alignas(32) float windowedBuffer_[kFFTSize];
alignas(32) float spectrumBuffer_[kSpectrumBins];
```

---

## Key Compiler Hints for This Codebase

```cpp
// In the audio callback — signal that this function cannot throw
void onAudioProcess(void* data) noexcept { ... }

// DSP functions — no aliasing between input and output
void applyWindow(const float* __restrict__ in,
                 float* __restrict__ out,
                 const float* __restrict__ window,
                 size_t n) noexcept;

// Hot path marking
[[gnu::hot]] void processFrame() noexcept;

// Cold path marking (shader loading, error handling)
[[gnu::cold]] void loadShaders();

// Likely/unlikely for rendering conditions
if ([[unlikely]] windowResized_) handleResize();
if ([[likely]] audioAvailable_)) runDSP();
```

---

## SIMD in This Project

The windowing multiply is the main SIMD opportunity:

```cpp
// Scalar version (compiler will auto-vectorise with __restrict__ + -O2)
void applyHannWindow(const float* __restrict__ input,
                     float* __restrict__ output,
                     const float* __restrict__ window,
                     size_t n) noexcept {
    for (size_t i = 0; i < n; ++i)
        output[i] = input[i] * window[i];
}

// Explicit AVX2 version (8 floats per iteration)
#include <immintrin.h>
void applyHannWindowAVX2(const float* __restrict__ input,
                          float* __restrict__ output,
                          const float* __restrict__ window,
                          size_t n) noexcept {
    size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        __m256 in  = _mm256_load_ps(input  + i);
        __m256 win = _mm256_load_ps(window + i);
        _mm256_store_ps(output + i, _mm256_mul_ps(in, win));
    }
    for (; i < n; ++i) output[i] = input[i] * window[i]; // tail
}
// Use the scalar version first — verify it auto-vectorises, only hand-write if needed
```

---

## Atomic Patterns Used in This Project

```cpp
// Signal from audio thread to render thread (no data, just a flag)
std::atomic<bool> newDataAvailable_{false};

// Audio thread:
ringBuffer_.push(samples, count);
newDataAvailable_.store(true, std::memory_order_release);

// Render thread:
if (newDataAvailable_.load(std::memory_order_acquire)) {
    newDataAvailable_.store(false, std::memory_order_relaxed);
    runDSP();
}

// Error flag (audio thread → render thread)
std::atomic<uint32_t> xrunCount_{0};
// Audio thread (on xrun detection):
xrunCount_.fetch_add(1, std::memory_order_relaxed);
// Render thread (log it):
if (auto xruns = xrunCount_.load(std::memory_order_relaxed); xruns > 0)
    logger_.warn("Xruns detected: {}", xruns);
```

---

## Allocation Strategy

All allocation happens in constructors / `init()`. The runtime is allocation-free.

```cpp
class Analyser {
    // All buffers pre-allocated as members — no heap in hot path
    std::array<float, kFFTSize>      inputBuf_{};
    std::array<float, kFFTSize>      windowedBuf_{};
    std::array<float, kSpectrumBins> spectrumBuf_{};
    std::array<float, kSpectrumBins> smoothedBuf_{};
    // FFT plan allocated once in constructor
    FFTPlan fftPlan_{kFFTSize};
public:
    // [[nodiscard]] so caller can't ignore construction failure
    [[nodiscard]] static std::expected<Analyser, std::string> create();
};
```

---

## Build Flags for This Project

```bash
# Debug — sanitisers catch bugs in the DSP and threading code
-O1 -g3 -fsanitize=address,undefined -fno-omit-frame-pointer
# Note: -O1 not -O0 for audio code — some timing bugs only appear with optimisation

# Release — maximum throughput
-O3 -march=native -ffast-math -flto -DNDEBUG
# -ffast-math is acceptable for audio DSP — we don't need IEEE 754 edge cases
# but be aware: it allows reordering of float operations

# Verify vectorisation:
-O3 -march=native -fopt-info-vec    # GCC: shows which loops were vectorised
-O3 -march=native -Rpass=loop-vectorize  # Clang equivalent
```

---

## Profiling This Project

```bash
# Sample the render loop
perf record -g ./build/release/audiovis
perf report

# Check cache behaviour (is DSP memory-bound or compute-bound?)
perf stat -e cycles,instructions,L1-dcache-load-misses,cache-misses ./build/release/audiovis

# Measure FFT performance specifically
# → Add Google Benchmark target for fft.cpp
# → bench/bench_fft.cpp: BM_FFT->Arg(1024)->Arg(4096)->Arg(16384)
```

**Expected bottleneck:** The FFT is compute-bound (O(N log N) operations).
The ring buffer and windowing should be memory-bandwidth-bound and effectively free.
The render thread's bottleneck is usually the GPU upload (`glBufferSubData`).

---

## Template Patterns in This Project

```cpp
// Ring buffer is a template — capacity baked in at compile time
// This means: no heap, size known to the compiler, can optimise modulo to bitmask
template<typename T, size_t Capacity>
class RingBuffer { /* ... */ };
using AudioRingBuffer = RingBuffer<float, 65536>;

// Window functions are constexpr — computed at compile time, live in .rodata
template<size_t N>
constexpr std::array<float, N> makeHannCoefficients() { /* ... */ }
constexpr auto kHannCoefficients = makeHannCoefficients<kFFTSize>();
// Zero runtime cost for windowing setup — array is baked into the binary
```
