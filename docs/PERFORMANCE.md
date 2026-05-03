# MIRAGE Containerization Performance Analysis

> **Reference data** collected on Jetson Xavier NX and Raspberry Pi 4 hardware. For benchmarking tooling, see the [O.A.S.I.S. meta-repo](https://github.com/malcolmhoward/the-oasis-project-meta-repo).

## Summary

Docker overhead for MIRAGE is **minimal** (1-5%) for most operations. Native installation is preferred only for latency-critical scenarios like CSI cameras at 60fps+ or real-time GPIO bit-banging.

---

## Performance Benchmarks

### Jetson Xavier NX

Test environment: Docker 24.0.5 with NVIDIA runtime.

| Operation | Native | Docker | Overhead |
|-----------|--------|--------|----------|
| SDL2 Frame Render (1080p) | 16.2ms | 16.4ms | **1.2%** |
| MQTT Publish (1KB) | 0.8ms | 0.82ms | **2.5%** |
| MQTT Subscribe | 0.9ms | 0.91ms | **1.1%** |
| Object Detection (Jetson Inference) | 45ms | 48ms | **6.7%** |
| SPI Transfer (1KB @ 500kHz) | 2.1ms | 2.3ms | **9.5%** |
| GPIO Toggle | 12us | 14us | **16.7%** |
| File Read (1MB) | 18ms | 18.5ms | **2.8%** |
| JSON Parse (10KB) | 1.2ms | 1.2ms | **0%** |

### Raspberry Pi 4

| Operation | Native | Docker | Overhead |
|-----------|--------|--------|----------|
| CPU Compute | 100ms | 101ms | **1%** |
| Network (host mode) | 2ms | 2ms | **0%** |
| USB Camera Capture | 33ms | 34ms | **3%** |
| CSI Camera (libcamera) | 28ms | 31ms | **10.7%** |
| TFLite Inference | 120ms | 123ms | **2.5%** |

---

## Overhead by Category

### Negligible (<1%)

CPU-bound operations: SDL2 rendering, MQTT message processing, JSON parsing, text-to-speech, audio playback, algorithm execution. Docker containers run native CPU instructions with no virtualization layer.

### Low (1-5%)

Network operations (mitigated with `--network host`), file I/O (mitigated with bind mounts), USB camera capture.

### Moderate (5-10%)

GPU operations on Jetson (5-7%, mitigated with `--runtime nvidia`), CSI camera access (5-8%, requires `--privileged`).

### High (>10%)

Direct hardware manipulation: GPIO bit-banging at MHz frequencies (10-20%), ultra-low latency SPI (<1us requirements), real-time kernel requirements. **Use native installation for these cases.**

---

## When to Use Docker vs Native

| Scenario | Recommendation |
|----------|---------------|
| Development and testing | Docker |
| CI/CD pipelines | Docker |
| USB cameras | Docker |
| Multiple deployment targets | Docker |
| CSI cameras at 60fps+ | Native |
| Real-time GPIO (<10us) | Native |
| Custom kernel modules | Native |
| Single production device, max performance | Native |

For typical MIRAGE HUD operations, Docker overhead averages **2-3%**.

---

## Optimization Tips

### Docker

- Use `--network host` to eliminate network overhead
- Use bind mounts for near-native file I/O
- Use `--runtime nvidia` on Jetson for GPU passthrough
- Use specific `--device` flags instead of `--privileged` where possible

### Native

- CPU pinning: `taskset -c 0-3 ./mirage`
- Real-time scheduling: `sudo chrt -f 99 ./mirage`
- Disable unnecessary services on dedicated hardware

---

*Part of the [O.A.S.I.S. Project](https://github.com/The-OASIS-Project).*
