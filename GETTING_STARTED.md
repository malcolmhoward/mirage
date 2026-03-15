# Getting Started with MIRAGE

Quick guide to get MIRAGE built and running. For a project overview, see [README.md](README.md).

## Prerequisites

- **OS**: Ubuntu 22.04+, Debian 12+, or Jetson Linux (L4T)
- **Hardware**: NVIDIA Jetson (recommended) or Raspberry Pi 4/5
- **Display**: One or two displays connected (HDMI/DSI)
- **Camera**: CSI (MIPI) or USB camera (one or two for stereo)
- **MQTT Broker**: Mosquitto running locally (required for OASIS communication)

---

## 1. Install System Dependencies

```bash
sudo apt update && sudo apt install -y \
  build-essential cmake git pkg-config \
  libudev-dev libxext-dev libwebp-dev \
  libpulse-dev libvorbis-dev libjson-c-dev \
  libsamplerate-dev libfreetype6-dev \
  libcurl4-openssl-dev libssl-dev \
  libmosquitto-dev mosquitto \
  libsndfile-dev libgd-dev \
  libglew-dev libgstreamer1.0-dev \
  libgstreamer-plugins-base1.0-dev \
  gstreamer1.0-plugins-good gstreamer1.0-plugins-bad \
  gstreamer1.0-plugins-ugly
```

> **Jetson users**: Install the full JetPack SDK for CUDA, hardware encoding, and camera support:
> ```bash
> sudo apt install nvidia-jetpack
> ```

---

## 2. Install SDL2 Libraries

The system apt packages for SDL2 are often outdated. Build from source for best compatibility.

### SDL2 (2.28.x or later)

```bash
wget https://github.com/libsdl-org/SDL/releases/download/release-2.28.5/SDL2-2.28.5.tar.gz
tar xzf SDL2-2.28.5.tar.gz && cd SDL2-2.28.5
mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr
make -j$(nproc) && sudo make install
cd ../..
```

### SDL2_image

```bash
wget https://github.com/libsdl-org/SDL_image/releases/download/release-2.6.3/SDL2_image-2.6.3.tar.gz
tar xzf SDL2_image-2.6.3.tar.gz && cd SDL2_image-2.6.3
mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr
make -j$(nproc) && sudo make install
cd ../..
```

### SDL2_ttf

```bash
wget https://github.com/libsdl-org/SDL_ttf/releases/download/release-2.20.2/SDL2_ttf-2.20.2.tar.gz
tar xzf SDL2_ttf-2.20.2.tar.gz && cd SDL2_ttf-2.20.2
mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr
make -j$(nproc) && sudo make install
cd ../..
```

### SDL2_gfx

```bash
sudo apt install -y libsdl2-gfx-dev
```

If the apt version is too old, build from source:
```bash
git clone https://github.com/ferzkopp/SDL2_gfx.git && cd SDL2_gfx
autoreconf -fi
./configure --prefix=/usr
make -j$(nproc) && sudo make install
cd ..
```

---

## 3. Install Jetson Inference (Optional — Object Detection, Deprecated)

Only needed if you want object detection on the HUD. Requires a Jetson with CUDA. Note: this feature is deprecated and needs rework.

```bash
git clone https://github.com/dusty-nv/jetson-inference
cd jetson-inference
git submodule update --init
mkdir build && cd build
cmake ..
make -j$(nproc)
sudo make install
cd ../..
```

See the [Jetson Inference build guide](https://github.com/dusty-nv/jetson-inference/blob/master/docs/building-repo-2.md) for details.

---

## 4. Configure Cameras (Jetson)

If using CSI cameras on Jetson, configure the pin headers:

```bash
sudo /opt/nvidia/jetson-io/jetson-io.py
```

1. **Configure Jetson 40pin Header** -> Configure header pins manually -> Enable `spi1` -> Save
2. **Configure Jetson 24pin CSI Connector** -> Configure for compatible hardware -> Select your camera (IMX219 Dual, IMX477 Dual, etc.) -> Save
3. Save and exit, then reboot

---

## 5. User Permissions

Add your user to the required groups:

```bash
sudo usermod -a -G dialout,video $USER
```

Log out and back in for the group changes to take effect.

---

## 6. Build MIRAGE

```bash
cd mirage
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

### Build with Object Detection

```bash
cmake -DUSE_JETSON_INFERENCE=ON -DUSE_CUDA=ON ..
make -j$(nproc)
```

### Specify Platform Manually

```bash
# Force Jetson platform (if auto-detection fails)
cmake -DPLATFORM=JETSON ..

# Force Raspberry Pi platform
cmake -DPLATFORM=RPI ..
```

---

## 7. Configure the HUD

Copy and edit the configuration file:

```bash
cd ..  # back to mirage root
cp config.json config.json.bak  # backup
```

Edit `config.json` to match your display setup. Key settings in the `Global` section:

```json
{
  "Global": {
    "Height": 1440,
    "Width": 1440,
    "Image Path": "ui_assets/mk2/",
    "Font Path": "ui_assets/fonts/",
    "Sound Path": "sound_assets/",
    "Stereo Offset": 0,
    "Pitch Offset": 0,
    "Wifi": "wlan0",
    "Snapshot Overlay": true,
    "Vision Inline Data": true
  }
}
```

| Setting | Description |
|---------|-------------|
| `Height` / `Width` | Per-eye output resolution (match your display) |
| `Image Path` | Directory containing HUD element images |
| `Font Path` | Directory containing TTF fonts |
| `Sound Path` | Directory containing audio files |
| `Stereo Offset` | Pixel offset for stereo depth adjustment |
| `Pitch Offset` | Compensate for helmet sensor tilt |
| `Wifi` | Network interface name for WiFi signal display |
| `Snapshot Overlay` | Include HUD overlay in AI snapshots |
| `Vision Inline Data` | Send base64 image data vs file path to DAWN |

A 720p-specific config is available as `config-720p.json`.

### Camera Resolution

Set the camera resolution in `defines.h` by uncommenting ONE option:

```c
//#define USE_720P_30FPS
#define USE_720P_60FPS      // Default
//#define USE_1080P_30FPS
//#define USE_1080P_60FPS
//#define USE_1440P_30FPS
//#define USE_1440P_60FPS
```

Rebuild after changing.

---

## 8. Configure MQTT

MIRAGE requires an MQTT broker for communication with DAWN and other OASIS components.

### Start Mosquitto

```bash
# Ensure mosquitto is running
sudo systemctl enable mosquitto
sudo systemctl start mosquitto
```

The default configuration connects to `127.0.0.1:1883` (localhost, no authentication). If your MQTT broker is on a different host, edit the `mosquitto_connect` call in `mirage.c`.

---

## 9. Configure Secrets

MIRAGE uses API keys for Google Maps (map element) and YouTube (streaming). Copy the template and fill in your keys:

```bash
cp secrets.h.sav secrets.h
```

Edit `secrets.h`:
```c
#define YOUTUBE_STREAM_KEY "your-youtube-stream-key"
#define GOOGLE_API_KEY "your-google-maps-api-key"
```

> **Important**: `secrets.h` should be in `.gitignore` and never committed to version control.

The Google API key needs the **Maps Static API** enabled in the Google Cloud Console. Restrict the key to that API and your device's IP address.

---

## 10. Run MIRAGE

Run from the project root directory (not the build directory) so it can find `config.json` and asset directories:

```bash
# Basic: dual CSI cameras, fullscreen
./build/mirage -f -c csi -n 2

# Single USB camera, windowed (for testing)
./build/mirage -c usb -n 1

# No camera, black background (UI development)
./build/mirage -b

# With recording to a directory
./build/mirage -f -c csi -n 2 -r -p ~/recordings/

# With YouTube live streaming
./build/mirage -f -c csi -n 2 -s

# With helmet TCP command server on custom port
./build/mirage -f -c csi -n 2 -H=5000

# With USB/serial helmet communication
./build/mirage -f -c csi -n 2 -u -d /dev/ttyACM0
```

### Keyboard Controls

| Key | Action |
|-----|--------|
| `1`-`9` | Switch HUD screens |
| `ESC` or `q` | Quit |
| Element hotkeys | Toggle specific elements (configured in `config.json`) |

---

## 11. HUD Configuration Reference

### Element Types

Elements are defined in `config.json` under named HUD sections. Each element has a `type`:

| Type | Description | Required Fields |
|------|-------------|-----------------|
| `static` | Fixed image overlay | `filename`, `x`, `y`, `width`, `height` |
| `animated` | Spritesheet animation | `filename`, `x`, `y`, `width`, `height`, `frames`, `speed` |
| `text` | Dynamic text display | `x`, `y`, `font`, `font_size`, `text` (with `*TOKEN*` substitution) |
| `special` | Data-driven renderer | `special_name`, `x`, `y`, `width`, `height` |

### Special Element Names

| Name | Description |
|------|-------------|
| `map` | Google Maps tile with GPS marker |
| `detect` | Object detection bounding boxes |
| `gauge` | Dynamic gauge (linear, arc, or ring) |
| `heading` | Compass/heading indicator |
| `pitch` | Pitch ladder |
| `altitude` | Altitude indicator |
| `wifi` | WiFi signal strength |
| `battery` | Battery status display |
| `armor_display` | Multi-component armor status panel |

### HUD Screen Membership

Each element has a `hud` field — a comma-separated list of HUD screen names the element belongs to. Elements appear only when their HUD screen is active.

---

## Troubleshooting

### Camera not detected

```bash
# Check CSI cameras
ls /dev/video*
# Check USB cameras
v4l2-ctl --list-devices
```

On Jetson, ensure the CSI connector is configured via `jetson-io.py` and you've rebooted after saving.

### No display output

Ensure SDL2 can access the display:
```bash
echo $DISPLAY   # Should be :0 or :1
```

For headless setups or SSH, you may need to set `DISPLAY=:0` and run with `sudo` or ensure the user has display access.

### MQTT connection refused

```bash
# Check if mosquitto is running
systemctl status mosquitto

# Test connection
mosquitto_sub -t "hud" -v
```

### GStreamer encoding errors

```bash
# Check available encoders
gst-inspect-1.0 | grep -i h264

# Jetson should show: nvv4l2h264enc
# RPi should show: avenc_h264_omx
```

### Build errors with Jetson Inference

Ensure CUDA is properly installed:
```bash
nvcc --version
ls /usr/local/cuda/include/cuda.h
```

---

## What's Next

- **Connect DAWN** — Set up the [DAWN AI assistant](https://github.com/The-OASIS-Project/dawn) for voice control and AI vision through the HUD.
- **Add AURA** — Connect helmet sensors for motion, environmental, and GPS data.
- **Add SPARK** — Connect armor sensors for component status monitoring.
- **Customize HUD** — Edit `config.json` to design your own HUD layout. Changes are picked up automatically within 5 seconds.
