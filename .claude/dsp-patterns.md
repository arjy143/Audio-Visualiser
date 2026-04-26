# DSP Patterns Reference

Read this before working on anything in `src/dsp/`.

---

## The Fourier Transform — What It Actually Does

The FFT answers the question: **"What frequencies are present in this chunk of audio?"**

Time domain: a waveform — amplitude vs time (what you record)
Frequency domain: a spectrum — amplitude vs frequency (what FFT produces)

```
Time domain:     ▁▃▇▅▂▆▄▁▃▇    (samples over time)
                       │ FFT
Frequency domain:    bass │ mid │ treble
                     ████ │ ██  │ █
```

The DFT (Discrete Fourier Transform) computes this exactly, but is O(N²).
The **FFT** (Fast Fourier Transform, Cooley-Tukey algorithm) computes the same result
in O(N log N) by exploiting symmetry. For N=4096: DFT needs 16M ops, FFT needs ~50K.

**Key parameters:**
- `N` = FFT size (must be power of 2) — controls frequency resolution
- `fs` = sample rate (48000 Hz) — controls frequency range
- Frequency resolution: `Δf = fs / N` — e.g., 48000/4096 ≈ 11.7 Hz per bin
- Max frequency (Nyquist): `fs / 2` = 24000 Hz
- Number of useful bins: `N/2 + 1` = 2049

---

## Windowing — Why You Need It

The FFT assumes its input is **one period of an infinitely repeating signal**.
If the waveform doesn't start and end at the same value, the FFT "sees" a sharp
discontinuity at the boundary — this causes **spectral leakage**: energy from one
frequency bleeds into neighbouring bins, smearing the spectrum.

**The fix:** multiply the input samples by a window function that tapers to zero
at both ends before running the FFT.

```
Without window:   [1.0, 0.8, 0.3, -0.2, ..., 0.9]  ← sharp jump at boundary
With Hann window: [0.0, 0.1, 0.2, 0.1, ..., 0.0]  ← smooth tapering
```

### Hann Window (most common choice)
```cpp
// w(n) = 0.5 * (1 - cos(2π·n / (N-1)))
constexpr auto makeHannWindow(size_t N) {
    std::array<float, N> w{};
    for (size_t n = 0; n < N; ++n)
        w[n] = 0.5f * (1.0f - std::cos(2.0f * std::numbers::pi_v<float> * n / (N - 1)));
    return w;
}
constexpr auto kHannWindow = makeHannWindow<kFFTSize>(); // Computed at compile time
```

### Blackman Window (better frequency separation, wider main lobe)
```cpp
// w(n) = 0.42 - 0.5·cos(2πn/N) + 0.08·cos(4πn/N)
// Use when you need to distinguish two close frequencies
```

⚡ **SIMD acceleration:** The windowing step is `output[i] = input[i] * window[i]` —
a pure element-wise multiply. This is trivially auto-vectorised. With AVX2:
8 multiplies per instruction, effectively free.

---

## FFT Output — Converting to Decibels

The FFT produces complex numbers `(real, imaginary)`. We want magnitude:

```
magnitude[k] = sqrt(real[k]² + imag[k]²)
```

Then convert to decibels (logarithmic scale, matches human hearing):
```
dB[k] = 20 * log10(magnitude[k] / N)
        ^           ^              ^
        dB scale   normalise      FFT size normalisation
```

The `/ N` normalisation prevents the magnitude from scaling with FFT size.
Clamp the minimum to something like -90 dB (silence) to avoid log10(0).

```cpp
void toDecibels(std::span<const std::complex<float>> fftOut,
                std::span<float> dBOut) noexcept {
    constexpr float kRef = 1.0f / kFFTSize;
    constexpr float kFloor = -90.0f;
    for (size_t k = 0; k < dBOut.size(); ++k) {
        const float mag = std::abs(fftOut[k]) * kRef;
        dBOut[k] = mag > 0 ? 20.0f * std::log10(mag) : kFloor;
    }
}
```

⚠️ **Pitfall:** `std::log10(0)` is undefined (returns -inf). Always guard with a floor.

---

## Smoothing — Reducing Flicker

Raw FFT output jumps wildly frame-to-frame. Apply **exponential moving average (EMA)**:

```
smoothed[k] = α * new[k] + (1 - α) * smoothed[k]
```

- `α = 1.0`: no smoothing (raw output)
- `α = 0.1`: heavy smoothing (slow response)
- `α = 0.3`: good starting point for visualisation

```cpp
void smoothSpectrum(std::span<float> current,
                    std::span<const float> prev,
                    float alpha) noexcept {
    const float oneMinusAlpha = 1.0f - alpha;
    for (size_t k = 0; k < current.size(); ++k)
        current[k] = alpha * current[k] + oneMinusAlpha * prev[k];
    // Auto-vectorised: pure element-wise FMA
}
```

⚡ Use different α for attack (signal rising) vs decay (signal falling):
```cpp
float alpha = newVal > oldVal ? kAttack : kDecay; // Per-bin
```

---

## Frequency Bin to Hz Conversion

```cpp
// Bin k corresponds to frequency:
float binToHz(size_t k, uint32_t sampleRate, size_t fftSize) noexcept {
    return static_cast<float>(k) * sampleRate / fftSize;
}

// Hz to bin (for finding e.g. where 1kHz falls):
size_t hzToBin(float hz, uint32_t sampleRate, size_t fftSize) noexcept {
    return static_cast<size_t>(hz * fftSize / sampleRate);
}
```

---

## Logarithmic Frequency Axis

Human hearing is logarithmic — we perceive octaves, not linear frequency steps.
Mapping bins linearly to screen pixels wastes 90% of the display on inaudible
high frequencies. Use a log scale:

```cpp
// Map screen pixel x (0..width) to FFT bin index
size_t pixelToBin(int x, int width, size_t numBins) noexcept {
    // log scale: pixel 0 → bin 1 (20Hz), pixel width → bin numBins (24kHz)
    constexpr float kMinHz = 20.0f;
    constexpr float kMaxHz = 20000.0f;
    const float logMin = std::log10(kMinHz);
    const float logMax = std::log10(kMaxHz);
    const float hz = std::pow(10.0f, logMin + (logMax - logMin) * x / width);
    return hzToBin(hz, kSampleRate, kFFTSize);
}
```

---

## Peak Detection

Hold peaks visually for ~1 second then let them fall:

```cpp
struct PeakHolder {
    float value{-90.0f};
    int holdFrames{0};
    static constexpr int kHoldDuration = 60; // frames
    static constexpr float kDecayRate = 0.5f; // dB per frame

    void update(float newVal) noexcept {
        if (newVal > value) { value = newVal; holdFrames = kHoldDuration; }
        else if (holdFrames > 0) { --holdFrames; }
        else { value -= kDecayRate; }
    }
};
```
