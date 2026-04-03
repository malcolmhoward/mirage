# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

MIRAGE (Multi-Input Reconnaissance and Guidance Environment) is a heads-up display (HUD) system for the OASIS Project, designed to run on NVIDIA Jetson and Raspberry Pi platforms. It renders real-time camera feeds with overlaid HUD elements to dual displays (one per eye), with support for object detection, video recording, and streaming.

## Important: Working with the Developer

When the developer asks questions, they are typically asking for feedback, analysis, or suggestions FIRST - not requesting immediate implementation. Always provide your thoughts, recommendations, and discuss trade-offs before taking action. Wait for explicit confirmation (e.g., "go ahead", "do it", "yes") before implementing changes.

**CRITICAL: NEVER delete files.** Always tell the developer which files should be deleted and let them do it manually. Files may contain secrets, credentials, or other data that cannot be recovered.

**CRITICAL: NEVER run `git add` or `git commit`.** Always tell the developer which files to add and suggest a commit message. Let them run the git commands manually.

## Building the Project

### Standard Build Process

```bash
# Configure with CMake preset (creates build directory automatically)
cmake --preset debug

# Build
make -C build-debug -j$(nproc)

# Run from project root
./build-debug/mirage
```

### Legacy Build (CMake < 3.21)

```bash
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

### Build with Optional Features

```bash
cmake --preset debug -DUSE_JETSON_INFERENCE=ON -DUSE_CUDA=ON
```

### Code Formatting

```bash
# Format all code (required before committing)
./format_code.sh

# Format only changed files (fast)
./format_code.sh --changed

# Check formatting without modifying files
./format_code.sh --check

# Install pre-commit hook
./install-git-hooks.sh
```

The `.clang-format` configuration enforces:
- 3-space indentation (no tabs)
- 100 character line limit
- K&R brace style
- Right-aligned pointers (`int *ptr`)
- Automatic include sorting

### Dependencies

See `README.md` and `GETTING_STARTED.md` for full installation. Core dependencies:
- CMake 3.15+ (3.21+ for presets)
- SDL2, SDL2_image, SDL2_ttf, SDL2_gfx
- GStreamer 1.0 (with plugins)
- json-c
- libmosquitto
- libcurl, OpenSSL
- OpenGL, GLEW, GD
- libvorbisfile, ALSA
- Optional: jetson-inference, CUDA

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

- `mirage.c/h` - Main entry point, thread coordination, SDL renderer
- `hud_manager.c/h` - HUD state management and element lifecycle
- `element_renderer.c/h` - Renders individual HUD elements (static, animated, text, special)
- `gauge_renderer.c/h` - Dynamic gauge drawing (car-like gauges)
- `config_parser.c/h` - JSON configuration file parsing, element/animation structures
- `config_manager.c/h` - Runtime configuration management with auto-refresh (5s polling)
- `mosquitto_comms.c/h` - MQTT communication with other OASIS components
- `recording.c/h` - GStreamer-based video recording and streaming
- `screenshot.c/h` - Screenshot and snapshot capture (async, for AI vision)
- `audio.c/h` - Audio playback via POSIX message queue (8 concurrent threads)
- `command_processing.c/h` - USB/Serial input handling
- `detect.cpp/h` - Object detection using Jetson Inference (deprecated)
- `hud_discovery.c/h` - MQTT HUD discovery protocol (OCP v1.3)
- `component_status.c/h` - OCP component keepalive protocol
- `system_metrics.c/h` - CPU/memory/temperature monitoring
- `logging.c/h` - Centralized logging with file/line/function tracking

### Communication

MIRAGE communicates with other OASIS components via MQTT:
- **AURA** (Helmet Sensors) -> MIRAGE: Motion, orientation, environmental data, GPS
- **SPARK** (Armor Sensors) -> MIRAGE: Component status, audio commands
- **DAWN** (AI Assistant) <-> MIRAGE: AI state, TTS notifications, image capture requests

### Platform Support

Controlled via `PLATFORM` CMake variable (AUTO, JETSON, RPI):
- `PLATFORM_JETSON` - NVIDIA hardware encoding (nvv4l2h264enc), nvarguscamerasrc
- `PLATFORM_RPI` - Raspberry Pi camera module (libcamerasrc)
- Generic ARM - USB cameras only, software encoding

### Configuration

- `config.json` - Runtime configuration (camera settings, HUD elements, fonts, paths). Hot-reloads every 5 seconds.
- `config-720p.json` - Alternative 720p resolution config
- `defines.h` - Compile-time settings (camera resolution, display dimensions, GStreamer pipelines, feature flags)
- `secrets.h` - API keys and credentials (gitignored)
- `version.h` - Version number (manually maintained)

## Coding Standards

**MUST follow `CODING_STYLE_GUIDE.md`** - enforced by clang-format.

Key points:
- 3-space indentation, no tabs
- 100 character line limit
- K&R brace style, always use braces for single statements
- `snake_case` for functions and variables
- `UPPER_CASE` for constants and macros
- Types: `typedef` with `_t` suffix (e.g., `log_level_t`)
- Pointers align right: `int *ptr`
- Return values: use `SUCCESS` (0) and `FAILURE` (1), never negative values
- Prefer static allocation over dynamic for embedded systems
- Use `LOG_INFO()`, `LOG_WARNING()`, `LOG_ERROR()` macros for logging
- All new files must include the GPL license header block

**File Header (REQUIRED for all new .c/.cpp/.h files)**:
```c
/*
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * By contributing to this project, you agree to license your contributions
 * under the GPLv3 (or any later version) or any future licenses chosen by
 * the project author(s). Contributions include any modifications,
 * enhancements, or additions to the project. These contributions become
 * part of the project and are adopted by the project author(s).
 *
 * [Brief description of file purpose]
 */
```

## Development Guidelines

### Thread Safety

When working with shared resources:
- **SDL Renderer**: Main thread only -- never call SDL_Render* from other threads
- **GStreamer Pipeline**: Recording thread (`vid_out_thread`) only
- **Config Reload**: Mutex-protected in `config_manager.c` (5-second polling)
- **MQTT Callbacks**: Run in Mosquitto's loop thread, route via topic matching
- **Audio Playback**: Isolated via POSIX message queues (8 threads, no shared state)
- **Object Detection**: Separate threads per eye, read-only access to frames

### File Size Monitoring

**Proactively warn** when files approach size limits:

- **1,500+ lines (C)**: Mention that the file is getting large
- **2,500+ lines**: Recommend splitting before adding more features
- **New feature in large file**: Suggest creating a separate module instead

**Current large files requiring attention:**

| File | Lines | Status |
|------|-------|--------|
| `mirage.c` | 2,546 | AT splitting threshold -- needs modular extraction |
| `element_renderer.c` | 2,212 | Approaching threshold -- split by renderer type |
| `config_parser.c` | 1,607 | Above monitoring threshold |
| `command_processing.c` | 1,533 | Above monitoring threshold |

### Refactoring Large Files

If asked to refactor a large file:

1. **Never attempt full rewrites** - They frequently fail due to interconnected features
2. **Use incremental extraction** - One feature at a time
3. **Keep original working** - Extract into new file, import back, test
4. **Test after each extraction** - Don't batch multiple extractions

## Common Patterns

**MQTT Callbacks** (in `mosquitto_comms.c`):
```c
void on_connect(struct mosquitto *mosq, void *obj, int reason_code) {
   /* Subscribe to topics */
   mosquitto_subscribe(mosq, NULL, "hud", 1);
}

void on_message(struct mosquitto *mosq, void *obj,
                const struct mosquitto_message *msg) {
   /* Route by topic via strncmp */
   if (strncmp(msg->topic, "hud", 3) == 0) { ... }
}
```

**Logging**:
```c
LOG_INFO("System initialized");
LOG_WARNING("Battery voltage low: %.2fV", voltage);
LOG_ERROR("I2C communication failed: %d", error);
```

**Config Access** (via `config_parser.h` getters):
```c
element *elements = get_elements();
int count = get_element_count();
/* Config hot-reloads every 5s via config_manager.c polling */
```

**HUD Element Registration** (JSON-driven in `config.json`):
- Elements defined as JSON objects with type, position, size, layer
- Bitmask-based HUD membership (up to 16 named HUD screens)
- No code changes needed to add/remove HUD elements

## Camera Resolution

Change camera resolution by uncommenting ONE option in `defines.h`:
```c
//#define USE_720P_30FPS
#define USE_720P_60FPS      // Default
//#define USE_1080P_30FPS
//#define USE_1080P_60FPS
```

## Important Files to Know

**Configuration:**
- `config.json`: Runtime HUD configuration (64KB, hot-reloads)
- `config-720p.json`: Alternative 720p config
- `defines.h`: Compile-time settings (resolution, encoding, features)
- `secrets.h`: API keys (gitignored)
- `version.h`: App version (manually maintained)

**Code Formatting:**
- `.clang-format`: C/C++ formatting rules (3-space indent, 100 char lines)
- `format_code.sh`: Formatting automation (clang-format-14)
- `pre-commit.hook`: Git pre-commit hook for formatting
- `install-git-hooks.sh`: Hook installer

**Build:**
- `CMakeLists.txt`: Build system configuration
- `CMakePresets.json`: debug/release/ci presets

**Assets:**
- `ui_assets/`: HUD graphics and fonts (mk2, mk2-720p variants)
- `sound_assets/`: Audio files (Ogg Vorbis)

**Tools:**
- `tools/scale_config.py`: Configuration scaling utility
- `tools/scale_frames.py`: Frame scaling utility

## Known Issues and TODOs

1. Object detection deprecated (Jetson Inference integration is outdated)
2. `mirage.c` at 2,546 lines -- needs modular extraction
3. `element_renderer.c` at 2,212 lines -- should split by renderer type
4. No unit tests yet
5. Flat file structure (all sources in root) -- reorganization planned
6. `mirage.h` <-> `recording.h` circular dependency (works but should use forward declarations)

## Development Lifecycle

### 1. Plan (if non-trivial)
- Use plan mode for features that touch multiple modules
- Launch Explore agents to understand existing code

### 2. Implement
- Build and format check after each logical chunk: `make -C build-debug -j$(nproc)` + `./format_code.sh --check`

### 3. Review
- Run review agents on the diff for non-trivial changes

### 4. Test
- Manual testing on device (camera feed, HUD rendering, MQTT, recording)
- No automated tests yet

### 5. Document
- Update CLAUDE.md if architecture changes

### 6. Commit
- Run `./format_code.sh --check` one final time
- Provide a single `git add` command with all relevant files
- Suggest a commit message (present tense, summary line + bullet details)
- **NEVER run `git add`, `git commit`, or `git push`** -- the developer does this

## License

GPLv3 or later. All source files include GPL header block.
