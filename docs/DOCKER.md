# MIRAGE Docker Guide

Docker configurations for building and running MIRAGE on multiple platforms.

## Overview

MIRAGE provides three platform-specific Dockerfiles:

| Dockerfile | Base Image | Target Platform | Use Case |
|-----------|-----------|-----------------|----------|
| `Dockerfile.dev` | `ubuntu:22.04` | x86/x64 | Development, CI/CD, build verification |
| `Dockerfile.jetson` | `nvcr.io/nvidia/l4t-base:r35.4.1` | NVIDIA Jetson | Production with GPU acceleration |
| `Dockerfile.rpi` | `arm64v8/debian:bookworm-slim` | Raspberry Pi (ARM64) | Production on RPi hardware |

Each Dockerfile is fully self-contained — see [ADR-0005](https://github.com/malcolmhoward/the-oasis-project-meta-repo/blob/main/coordination/decisions/adr/0005-dockerfile-independence.md) for the rationale.

> **Note**: These containers build the actual MIRAGE C application. Mock hardware is not yet supported in MIRAGE's C source — see the [O.A.S.I.S. meta-repo](https://github.com/malcolmhoward/the-oasis-project-meta-repo) for mock hardware plans.

## Prerequisites

### All Platforms

- Docker installed and running
- MIRAGE source code cloned

### NVIDIA Jetson

1. **JetPack SDK** installed on host
2. **NVIDIA Docker runtime**:
   ```bash
   sudo apt-get update
   sudo apt-get install -y docker.io nvidia-docker2
   sudo systemctl restart docker
   ```
3. **Hardware configured** (before building):
   ```bash
   sudo /opt/nvidia/jetson-io/jetson-io.py
   # Enable SPI1 on 40-pin header
   # Configure CSI cameras
   ```

### Raspberry Pi

1. **64-bit OS** (Raspberry Pi OS 64-bit or Ubuntu)
2. **Docker installed**:
   ```bash
   curl -fsSL https://get.docker.com -o get-docker.sh
   sudo sh get-docker.sh
   sudo usermod -aG docker $USER
   ```
3. **Hardware interfaces enabled**:
   ```bash
   sudo raspi-config
   # Interface Options -> SPI -> Enable
   # Interface Options -> Camera -> Enable
   ```

## Building

```bash
# Development (x86/x64)
docker build -f Dockerfile.dev -t mirage-dev .

# NVIDIA Jetson (build on Jetson hardware)
docker build -f Dockerfile.jetson -t mirage:jetson .

# Raspberry Pi (build on RPi hardware)
docker build -f Dockerfile.rpi -t mirage:rpi .
```

## Running

### Development Container

```bash
docker run --rm -it mirage-dev
```

The dev container starts Mosquitto in the background and drops to a shell with the built MIRAGE binary at `/opt/mirage/build/mirage`.

### Jetson Container

```bash
docker run --rm -it \
  --runtime nvidia \
  --privileged \
  --network host \
  --device=/dev/spidev1.0 \
  --device=/dev/video0 \
  --device=/dev/video1 \
  -v /tmp/.X11-unix:/tmp/.X11-unix \
  -e DISPLAY=$DISPLAY \
  mirage:jetson
```

### Raspberry Pi Container

```bash
docker run --rm -it \
  --privileged \
  --network host \
  --device=/dev/spidev0.0 \
  --device=/dev/video0 \
  --device=/dev/gpiomem \
  -v /sys:/sys \
  -v /tmp/.X11-unix:/tmp/.X11-unix \
  -e DISPLAY=$DISPLAY \
  mirage:rpi
```

## Multi-Component Development

For running MIRAGE alongside other O.A.S.I.S. components (DAWN, AURA, SPARK), use the `docker-compose.yml` in the [S.C.O.P.E. meta-repo](https://github.com/malcolmhoward/the-oasis-project-meta-repo). The compose configuration orchestrates multiple component containers with shared networking.

## Platform Differences

| Feature | Dockerfile.dev | Dockerfile.jetson | Dockerfile.rpi |
|---------|---------------|-------------------|----------------|
| Architecture | x86/x64 | ARM64 (Tegra) | ARM64 |
| GPU | Mesa (software) | CUDA / Jetson Inference | Mesa (software) |
| AI Framework | None | Jetson Inference | TensorFlow Lite |
| SDL2 video | Default | Default | KMS/DRM + OpenGLES |
| SPI device | N/A | `/dev/spidev1.0` | `/dev/spidev0.0` |
| CMake flags | Default | `-DUSE_JETSON_INFERENCE=ON -DUSE_CUDA=ON -DPLATFORM=JETSON` | `-DPLATFORM=RPI` |

## Hardware Access

### SPI

- **Jetson**: Configure via `jetson-io.py`, access `/dev/spidev1.0`
- **RPi**: Enable via `raspi-config`, access `/dev/spidev0.0`
- Requires `--device` flag or `--privileged`

### Cameras

- **Jetson**: CSI cameras via `/dev/video0`, `/dev/video1`
- **RPi**: Camera Module via `/dev/video0` or libcamera
- USB cameras: pass through with `--device=/dev/video0`

### Display

If the GUI doesn't appear:
```bash
xhost +local:docker
export DISPLAY=:0
```

## Troubleshooting

### Permission Denied on Devices

```bash
# Add user to device groups
sudo usermod -aG video,dialout $USER    # Jetson
sudo usermod -aG video,gpio,spi,dialout $USER  # RPi
```

### Camera Not Detected

```bash
# List video devices on host
ls -l /dev/video*
v4l2-ctl --list-devices
```

### Build Failures

1. Check that all SDL2 source downloads succeeded (network issues)
2. On Jetson, verify JetPack SDK is installed: `dpkg -l | grep nvidia-jetpack`
3. Review the build log inside the container: check CMake output for missing dependencies

### SPI Not Working

```bash
# Verify SPI is enabled on host
ls -l /dev/spidev*
lsmod | grep spi
```

## Docker vs Native

For a detailed comparison of Docker vs native performance, see [docs/PERFORMANCE.md](PERFORMANCE.md).

**Quick guidance**:
- **Use Docker** for development, CI/CD, and deployments where 2-5% overhead is acceptable
- **Use native** ([scripts/install-native.sh](../scripts/install-native.sh)) for production on dedicated hardware where maximum performance is required

## Security Considerations

The production containers run with `--privileged` for hardware access. In production:
- Use specific `--device` flags instead of `--privileged` where possible
- Run MIRAGE as a non-root user inside the container
- Use TLS for MQTT communication (see `mosquitto_comms.h`)

---

*Part of the [O.A.S.I.S. Project](https://github.com/The-OASIS-Project). For ecosystem orchestration, see [S.C.O.P.E.](https://github.com/malcolmhoward/the-oasis-project-meta-repo).*
