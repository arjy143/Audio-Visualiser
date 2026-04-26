# Audio Threading Reference

Read this before working on anything in `src/audio/` or the PipeWire callback.

---

## Why the Audio Thread Is Special

PipeWire (and ALSA/JACK beneath it) calls your audio callback on a **real-time thread**
with a strict deadline. On a typical setup, you have ~5–10ms to fill a buffer before
the audio hardware starves and produces an **xrun** (a glitch/click in the output).

The OS scheduler gives this thread `SCHED_FIFO` priority — it preempts everything else.
But this also means: **if your callback blocks for any reason, the entire system suffers.**

### What Can Block (and is therefore forbidden in the callback):

```
❌  malloc / new / delete / free  — may take a lock inside libc
❌  std::mutex::lock()            — can block indefinitely
❌  printf / std::cout            — write() syscall, can block on pipe
❌  std::this_thread::sleep_for   — obvious
❌  file I/O of any kind          — disk can stall
❌  std::vector::push_back        — may reallocate (= malloc)
❌  std::map / std::unordered_map — may allocate on insert
❌  throwing exceptions           — involves allocation in some implementations
❌  virtual function calls        — not forbidden, but cache-unfriendly; avoid
```

### What IS allowed:
```
✅  Reading/writing pre-allocated buffers
✅  std::atomic load/store/fetch_add with relaxed or acq/rel ordering
✅  Math: +, -, *, /, std::sin, std::abs, etc.
✅  memcpy / memmove on known-size buffers
✅  SIMD intrinsics
✅  Setting an atomic flag to signal the render thread
```

---

## PipeWire Stream Setup

PipeWire replaced PulseAudio as the default on modern Linux distros (Fedora 34+,
Ubuntu 22.04+). Its API is in `<pipewire/pipewire.h>`.

```cpp
// Minimal PipeWire capture setup pattern
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>

// The callback signature PipeWire expects:
static void onProcess(void* userdata) {
    // userdata = pointer to your Capture object
    // pw_stream_dequeue_buffer() to get samples
    // pw_stream_queue_buffer() when done
    // NEVER block here
}

static const pw_stream_events kStreamEvents = {
    .version = PW_VERSION_STREAM_EVENTS,
    .process = onProcess,
};
```

Link with: `-lpipewire-0.3`
Include path: `/usr/include/pipewire-0.3`, `/usr/include/spa-0.2`

---

## Lock-Free SPSC Ring Buffer — How It Works

This is the bridge between the audio thread (producer) and the render thread (consumer).

```
Audio Thread                    Render Thread
(Producer)                      (Consumer)
    │                               │
    │  head_ ──────────────────►    │
    │                               │
    │  [  consumed  |  available  ] │
    │               ▲               │
    │               └─── tail_ ─────┘
    │
    write to buffer_[head_ % Capacity]
    then head_.store(head+1, release)
                            │
                            read buffer_[tail_ % Capacity]
                            then tail_.store(tail+1, release)
```

**Why it's safe without a mutex:**
- Only ONE writer (audio thread) ever modifies `head_`
- Only ONE reader (render thread) ever modifies `tail_`
- `memory_order_release` on store ensures writes are visible before the index advances
- `memory_order_acquire` on load ensures the index is read after the data is visible

**Why `alignas(64)` on head_ and tail_:**
Both are `std::atomic<size_t>`. Without alignment, they might share a cache line.
When the audio thread writes `head_` and the render thread reads it, the CPU must
transfer the entire cache line between cores — even though they're accessing different
bytes. This is **false sharing** and can cost 10-100x in throughput.
With `alignas(64)`, each sits on its own cache line.

```cpp
template<typename T, size_t Capacity>
class RingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");
    //            ↑ Bit trick: power of 2 has exactly one bit set,
    //              so (N & N-1) == 0 iff N is a power of 2

    alignas(64) std::atomic<size_t> head_{0}; // Written by producer only
    alignas(64) std::atomic<size_t> tail_{0}; // Written by consumer only
    T buffer_[Capacity];
    // ...
};
```

---

## Mono Downmix in the Callback

PipeWire typically gives you interleaved stereo (L, R, L, R, ...).
Downmix to mono in the callback before writing to the ring buffer:

```cpp
// Interleaved stereo → mono, in-place friendly
// samples: [L0, R0, L1, R1, ..., Ln, Rn]
// out:     [M0, M1, ..., Mn]  where Mi = (Li + Ri) * 0.5f
void downmixToMono(const float* __restrict__ stereo,
                   float* __restrict__ mono,
                   size_t frameCount) noexcept {
    for (size_t i = 0; i < frameCount; ++i)
        mono[i] = (stereo[i * 2] + stereo[i * 2 + 1]) * 0.5f;
    // __restrict__ tells the compiler the buffers don't overlap → auto-vectorised
}
```

⚡ **Performance note:** This loop is trivially auto-vectorised by GCC/Clang with `-O2`.
Check with godbolt.org — you should see `ymm` registers (AVX2) or `xmm` (SSE).

---

## Detecting Xruns

PipeWire reports xruns via the stream info callback. Log them (atomically) so you can
see if your audio thread is overrunning its budget:

```cpp
static void onStreamInfo(void* data, const pw_stream_info* info) {
    if (info->change_mask & PW_STREAM_CHANGE_MASK_PROPS) {
        // Check for xrun count in props
    }
}
```

A good rule of thumb: if you're seeing xruns, the audio callback is taking too long.
Profile it with `perf` — but remember to build with `-O2` or higher first.

---

## Thread Pinning (Advanced)

For ultra-low latency, pin the render thread to a different core than PipeWire's
audio thread to prevent them from competing for the same core's resources:

```cpp
// Pin current thread to core N
void pinToCore(int core) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
}
// Call from main thread: pinToCore(1)
// PipeWire manages its own thread affinity
```
