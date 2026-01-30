# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

MIRAGE (Multi-Input Reconnaissance and Guidance Environment) is a heads-up display (HUD) system for the OASIS Project, designed to run on NVIDIA Jetson and Raspberry Pi platforms. It renders real-time camera feeds with overlaid HUD elements to dual displays (one per eye), with support for object detection, video recording, and streaming.

## Build Commands

```bash
# Configure and build
mkdir -p build && cd build
cmake ..
make -j$(nproc)

# Build with optional features
cmake -DUSE_JETSON_INFERENCE=ON -DUSE_CUDA=ON ..

# Format code (required before committing)
./format_code.sh

# Check formatting without modifying
./format_code.sh --check
```

## Architecture

### Threading Model
The system uses multiple threads for concurrent processing:
- `video_proc_thread` - Camera input processing, renders to SDL
- `vid_out_thread` - Video output (disk recording and/or streaming)
- `command_proc_thread` - USB/Serial input handling
- `thread_handles[0..7]` - Audio playback threads (NUM_AUDIO_THREADS=8)
- `od_L_thread`, `od_R_thread` - Object detection per eye (when enabled)
- `cpu_util_thread` - CPU utilization monitoring
- `map_download_thread` - Map tile updates
- Mosquitto loop - MQTT message handling

### Key Modules
- `mirage.c` - Main entry point, thread coordination
- `hud_manager.c/h` - HUD state management and element lifecycle
- `element_renderer.c/h` - Renders individual HUD elements
- `gauge_renderer.c/h` - Dynamic gauge drawing (car-like gauges)
- `mosquitto_comms.c/h` - MQTT communication with other OASIS components
- `config_parser.c/h` - JSON configuration file parsing
- `config_manager.c/h` - Runtime configuration management with auto-refresh
- `recording.c/h` - GStreamer-based video recording and streaming
- `audio.c/h` - Audio playback via POSIX message queue
- `detect.cpp/h` - Object detection using Jetson Inference

### Communication
MIRAGE communicates with other OASIS components via MQTT:
- **AURA** (Helmet Sensors) → MIRAGE: Motion, orientation, environmental data, GPS
- **SPARK** (Armor Sensors) → MIRAGE: Component status, audio commands
- **DAWN** (AI Assistant) ↔ MIRAGE: AI state, TTS notifications, image capture requests

### Platform Support
Controlled via `PLATFORM` CMake variable (AUTO, JETSON, RPI):
- `PLATFORM_JETSON` - NVIDIA hardware encoding (nvv4l2h264enc), nvarguscamerasrc
- `PLATFORM_RPI` - Raspberry Pi camera module (libcamerasrc)

### Configuration
- `config.json` - Runtime configuration (camera settings, HUD elements, paths)
- `defines.h` - Compile-time settings (camera resolution, display dimensions, GStreamer pipelines)

## Coding Standards

**MUST follow CODING_STYLE_GUIDE.md** - enforced by clang-format

Key points:
- 3-space indentation, no tabs
- 100 character line limit
- K&R brace style, always use braces for single statements
- `snake_case` for functions and variables
- `UPPER_CASE` for constants and macros
- Pointers align right: `int *ptr`
- Return values: use `SUCCESS` (0) and `FAILURE` (1), never negative values
- Prefer static allocation over dynamic for embedded systems
- Use `LOG_INFO()`, `LOG_WARNING()`, `LOG_ERROR()` macros for logging

## Camera Resolution

Change camera resolution by uncommenting ONE option in `defines.h`:
```c
//#define USE_720P_30FPS
#define USE_720P_60FPS      // Default
//#define USE_1080P_30FPS
//#define USE_1080P_60FPS
```

## Dependencies

See README.md for full installation. Core dependencies:
- SDL2, SDL2_image, SDL2_ttf, SDL2_gfx
- GStreamer 1.0
- json-c
- libmosquitto
- libcurl
- OpenGL/GLEW
- Optional: jetson-inference, CUDA
