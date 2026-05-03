#!/bin/bash
# install-native.sh
# Native installation of MIRAGE - Maximum Performance
#
# Installs all dependencies and builds MIRAGE directly on the host system,
# without Docker containerization. Use this for production deployments
# where every millisecond counts, or on platforms where Docker adds
# unacceptable overhead (e.g., CSI cameras at 60fps+, real-time GPIO).
#
# Supports: NVIDIA Jetson (Nano, Orin Nano, NX), Raspberry Pi (ARM64),
#           and generic x86/x64 Linux.
#
# Usage:
#   sudo ./scripts/install-native.sh
#
# For Docker-based development, see Dockerfile.dev instead.

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Configuration
SDL2_VERSION="2.28.2"
SDL2_IMAGE_VERSION="2.6.3"
SDL2_TTF_VERSION="2.20.2"
INSTALL_DIR="/opt/mirage"
LOG_FILE="/tmp/mirage-install.log"

# Functions
log() {
    echo -e "$1" | tee -a "$LOG_FILE"
}

error_exit() {
    log "${RED}Error: $1${NC}"
    exit 1
}

check_root() {
    if [ "$EUID" -ne 0 ]; then
        error_exit "Please run with sudo"
    fi
}

detect_platform() {
    if [ -f /etc/nv_tegra_release ]; then
        PLATFORM="jetson"
        ARCH=$(uname -m)
    elif grep -q "Raspberry Pi" /proc/cpuinfo 2>/dev/null; then
        PLATFORM="rpi"
        ARCH="aarch64"
    else
        PLATFORM="generic"
        ARCH=$(uname -m)
    fi

    log "${GREEN}Detected: $PLATFORM ($ARCH)${NC}"
}

print_header() {
    log "${BLUE}========================================${NC}"
    log "${BLUE}MIRAGE Native Installation${NC}"
    log "${BLUE}Maximum Performance Setup${NC}"
    log "${BLUE}========================================${NC}"
    log ""
    log "Platform: $PLATFORM"
    log "Architecture: $ARCH"
    log "Install directory: $INSTALL_DIR"
    log "Log file: $LOG_FILE"
    log ""
}

# Step 1: Update system
step_update_system() {
    log "${BLUE}[1/9] Updating system packages...${NC}"

    apt-get update >> "$LOG_FILE" 2>&1
    apt-get upgrade -y >> "$LOG_FILE" 2>&1

    log "${GREEN}  System updated${NC}"
}

# Step 2: Install build tools and dependencies
step_install_dependencies() {
    log "${BLUE}[2/9] Installing dependencies...${NC}"

    # Essential build tools
    apt-get install -y \
        build-essential \
        cmake \
        git \
        wget \
        curl \
        pkg-config \
        autoconf \
        automake \
        libtool \
        >> "$LOG_FILE" 2>&1

    # MIRAGE dependencies (from CMakeLists.txt)
    apt-get install -y \
        libudev-dev \
        libxext-dev \
        libwebp-dev \
        libpulse-dev \
        libvorbis-dev \
        libjson-c-dev \
        libsamplerate0-dev \
        libfreetype6-dev \
        libcurl4-openssl-dev \
        libmosquitto-dev \
        mosquitto \
        mosquitto-clients \
        libsndfile1-dev \
        libssl-dev \
        libgd-dev \
        >> "$LOG_FILE" 2>&1

    # SDL2 build dependencies
    apt-get install -y \
        libdrm-dev \
        libgbm-dev \
        libgl1-mesa-dev \
        libegl1-mesa-dev \
        libgles2-mesa-dev \
        libibus-1.0-dev \
        libdbus-1-dev \
        libx11-dev \
        libxrandr-dev \
        libxcursor-dev \
        libxinerama-dev \
        libxi-dev \
        libxss-dev \
        libxxf86vm-dev \
        libasound2-dev \
        libpng-dev \
        libjpeg-dev \
        libtiff-dev \
        libharfbuzz-dev \
        >> "$LOG_FILE" 2>&1

    # GStreamer
    apt-get install -y \
        libgstreamer1.0-dev \
        libgstreamer-plugins-base1.0-dev \
        gstreamer1.0-plugins-base \
        >> "$LOG_FILE" 2>&1

    # OpenGL / GLEW
    apt-get install -y \
        libglew-dev \
        >> "$LOG_FILE" 2>&1

    # SDL2_gfx (from apt)
    apt-get install -y \
        libsdl2-gfx-dev \
        >> "$LOG_FILE" 2>&1

    # Python for utilities
    apt-get install -y \
        python3 \
        python3-pip \
        python3-dev \
        python3-numpy \
        >> "$LOG_FILE" 2>&1

    # Platform-specific packages
    if [ "$PLATFORM" = "jetson" ]; then
        log "  Installing Jetson-specific packages..."
        apt-get install -y nvidia-jetpack >> "$LOG_FILE" 2>&1
    elif [ "$PLATFORM" = "rpi" ]; then
        log "  Installing Raspberry Pi specific packages..."
        apt-get install -y \
            libraspberrypi-dev \
            libraspberrypi0 \
            libraspberrypi-bin \
            raspi-config \
            >> "$LOG_FILE" 2>&1
    fi

    log "${GREEN}  Dependencies installed${NC}"
}

# Step 3: Build and install SDL2 from source
step_install_sdl2() {
    log "${BLUE}[3/9] Building SDL2 libraries from source...${NC}"

    cd /tmp

    # SDL2
    log "  Building SDL2 ${SDL2_VERSION}..."
    if [ ! -f "/usr/local/lib/libSDL2.so" ]; then
        wget -q "https://github.com/libsdl-org/SDL/releases/download/release-${SDL2_VERSION}/SDL2-${SDL2_VERSION}.tar.gz"
        tar -xzf "SDL2-${SDL2_VERSION}.tar.gz"
        cd "SDL2-${SDL2_VERSION}"

        # Platform-specific SDL2 configuration
        if [ "$PLATFORM" = "rpi" ]; then
            ./configure --prefix=/usr/local \
                --enable-video-kmsdrm \
                --enable-video-opengles \
                --disable-video-opengl \
                >> "$LOG_FILE" 2>&1
        else
            ./configure --prefix=/usr/local >> "$LOG_FILE" 2>&1
        fi

        make -j$(nproc) >> "$LOG_FILE" 2>&1
        make install >> "$LOG_FILE" 2>&1
        ldconfig
        cd /tmp
        rm -rf "SDL2-${SDL2_VERSION}"*
        log "${GREEN}    SDL2 installed${NC}"
    else
        log "${YELLOW}    SDL2 already installed${NC}"
    fi

    # SDL2_image
    log "  Building SDL2_image ${SDL2_IMAGE_VERSION}..."
    if [ ! -f "/usr/local/lib/libSDL2_image.so" ]; then
        wget -q "https://github.com/libsdl-org/SDL_image/releases/download/release-${SDL2_IMAGE_VERSION}/SDL2_image-${SDL2_IMAGE_VERSION}.tar.gz"
        tar -xzf "SDL2_image-${SDL2_IMAGE_VERSION}.tar.gz"
        cd "SDL2_image-${SDL2_IMAGE_VERSION}"
        ./configure --prefix=/usr/local >> "$LOG_FILE" 2>&1
        make -j$(nproc) >> "$LOG_FILE" 2>&1
        make install >> "$LOG_FILE" 2>&1
        ldconfig
        cd /tmp
        rm -rf "SDL2_image-${SDL2_IMAGE_VERSION}"*
        log "${GREEN}    SDL2_image installed${NC}"
    else
        log "${YELLOW}    SDL2_image already installed${NC}"
    fi

    # SDL2_ttf
    log "  Building SDL2_ttf ${SDL2_TTF_VERSION}..."
    if [ ! -f "/usr/local/lib/libSDL2_ttf.so" ]; then
        wget -q "https://github.com/libsdl-org/SDL_ttf/releases/download/release-${SDL2_TTF_VERSION}/SDL2_ttf-${SDL2_TTF_VERSION}.tar.gz"
        tar -xzf "SDL2_ttf-${SDL2_TTF_VERSION}.tar.gz"
        cd "SDL2_ttf-${SDL2_TTF_VERSION}"
        ./configure --prefix=/usr/local >> "$LOG_FILE" 2>&1
        make -j$(nproc) >> "$LOG_FILE" 2>&1
        make install >> "$LOG_FILE" 2>&1
        ldconfig
        cd /tmp
        rm -rf "SDL2_ttf-${SDL2_TTF_VERSION}"*
        log "${GREEN}    SDL2_ttf installed${NC}"
    else
        log "${YELLOW}    SDL2_ttf already installed${NC}"
    fi

    log "${GREEN}  SDL2 libraries installed${NC}"
}

# Step 4: Install AI frameworks
step_install_ai() {
    log "${BLUE}[4/9] Installing AI/ML frameworks...${NC}"

    if [ "$PLATFORM" = "jetson" ]; then
        log "  Installing Jetson Inference..."
        if [ ! -d "/opt/jetson-inference" ]; then
            cd /opt
            git clone --recursive --depth 1 https://github.com/dusty-nv/jetson-inference >> "$LOG_FILE" 2>&1
            cd jetson-inference
            mkdir -p build
            cd build
            cmake .. >> "$LOG_FILE" 2>&1
            make -j$(nproc) >> "$LOG_FILE" 2>&1
            make install >> "$LOG_FILE" 2>&1
            ldconfig
            log "${GREEN}    Jetson Inference installed${NC}"
        else
            log "${YELLOW}    Jetson Inference already installed${NC}"
        fi
    else
        log "  Installing TensorFlow Lite..."
        pip3 install --break-system-packages \
            tflite-runtime \
            opencv-python-headless \
            >> "$LOG_FILE" 2>&1
        log "${GREEN}    TensorFlow Lite installed${NC}"
    fi

    log "${GREEN}  AI frameworks installed${NC}"
}

# Step 5: Configure hardware interfaces
step_configure_hardware() {
    log "${BLUE}[5/9] Configuring hardware interfaces...${NC}"

    if [ "$PLATFORM" = "jetson" ]; then
        log "${YELLOW}  Jetson hardware configuration:${NC}"
        log "  Run manually: sudo /opt/nvidia/jetson-io/jetson-io.py"
        log "    - Enable SPI1 on 40-pin header"
        log "    - Configure CSI camera connector"

    elif [ "$PLATFORM" = "rpi" ]; then
        log "  Enabling Raspberry Pi interfaces..."

        # Enable SPI
        raspi-config nonint do_spi 0 >> "$LOG_FILE" 2>&1
        log "    SPI enabled"

        # Enable Camera
        raspi-config nonint do_camera 0 >> "$LOG_FILE" 2>&1 || \
            raspi-config nonint do_legacy 0 >> "$LOG_FILE" 2>&1
        log "    Camera enabled"

        # Enable I2C
        raspi-config nonint do_i2c 0 >> "$LOG_FILE" 2>&1
        log "    I2C enabled"

        # Load kernel modules
        modprobe spi_bcm2835 2>/dev/null || modprobe spi-bcm2835 2>/dev/null
        modprobe spidev 2>/dev/null
        modprobe i2c-dev 2>/dev/null

        # Create udev rules for GPIO
        cat > /etc/udev/rules.d/99-gpio.rules << 'UDEV_EOF'
SUBSYSTEM=="gpio", KERNEL=="gpiochip*", MODE="0660", GROUP="gpio"
SUBSYSTEM=="gpio", KERNEL=="gpio*", MODE="0660", GROUP="gpio"
UDEV_EOF
        udevadm control --reload-rules
        udevadm trigger

        log "${GREEN}    Raspberry Pi interfaces configured${NC}"
    fi

    log "${GREEN}  Hardware configured${NC}"
}

# Step 6: Set user permissions
step_set_permissions() {
    log "${BLUE}[6/9] Configuring user permissions...${NC}"

    # Get the actual user (not root)
    if [ -n "$SUDO_USER" ]; then
        TARGET_USER="$SUDO_USER"
    else
        log "${YELLOW}  Enter username for permission setup:${NC}"
        read -r -p "  Username: " TARGET_USER
    fi

    if [ -z "$TARGET_USER" ]; then
        log "${YELLOW}  No username provided, skipping permission setup${NC}"
        return
    fi

    log "  Configuring permissions for user: $TARGET_USER"

    # Essential groups
    usermod -aG dialout,video "$TARGET_USER"

    # Platform-specific groups
    if [ "$PLATFORM" = "rpi" ]; then
        usermod -aG gpio,spi,i2c "$TARGET_USER"
    fi

    log "${GREEN}  Permissions configured for $TARGET_USER${NC}"
}

# Step 7: Install Python dependencies
step_install_python() {
    log "${BLUE}[7/9] Installing Python dependencies...${NC}"

    pip3 install --break-system-packages \
        paho-mqtt \
        >> "$LOG_FILE" 2>&1

    log "${GREEN}  Python dependencies installed${NC}"
}

# Step 8: Create systemd services (optional)
step_create_services() {
    log "${BLUE}[8/9] Creating systemd services (optional)...${NC}"

    # MQTT broker service (if not already enabled)
    if ! systemctl is-enabled mosquitto >> /dev/null 2>&1; then
        systemctl enable mosquitto >> "$LOG_FILE" 2>&1
        systemctl start mosquitto >> "$LOG_FILE" 2>&1
        log "    MQTT broker service enabled"
    fi

    log "${GREEN}  Services configured${NC}"
}

# Step 9: Verify installation
step_verify() {
    log "${BLUE}[9/9] Verifying installation...${NC}"

    # Check SDL2
    if sdl2-config --version >> /dev/null 2>&1; then
        SDL_VERSION=$(sdl2-config --version)
        log "    SDL2: $SDL_VERSION"
    else
        log "${YELLOW}    SDL2 not found in PATH${NC}"
    fi

    # Check hardware devices
    if [ -e /dev/video0 ]; then
        log "    Camera: /dev/video0"
    else
        log "    Camera not detected (may require reboot)"
    fi

    if ls /dev/spidev* >> /dev/null 2>&1; then
        log "    SPI: $(ls /dev/spidev*)"
    else
        log "    SPI not detected (may require reboot/configuration)"
    fi

    # Check MQTT
    if systemctl is-active mosquitto >> /dev/null 2>&1; then
        log "    MQTT broker running"
    else
        log "    MQTT broker not running"
    fi

    log "${GREEN}  Verification complete${NC}"
}

# Print next steps
print_next_steps() {
    log ""
    log "${GREEN}========================================${NC}"
    log "${GREEN}Installation Complete!${NC}"
    log "${GREEN}========================================${NC}"
    log ""
    log "${YELLOW}Next Steps:${NC}"
    log ""
    log "1. ${RED}LOG OUT AND BACK IN${NC} (for group changes to take effect)"
    log ""

    if [ "$PLATFORM" = "jetson" ]; then
        log "2. Configure Jetson hardware:"
        log "   ${BLUE}sudo /opt/nvidia/jetson-io/jetson-io.py${NC}"
        log ""
    fi

    log "3. Build MIRAGE:"
    log "   ${BLUE}cd $(pwd) && mkdir -p build && cd build && cmake .. && make -j\$(nproc)${NC}"
    log ""
    log "4. Run MIRAGE:"
    log "   ${BLUE}./build/mirage${NC}"
    log ""

    if [ "$PLATFORM" = "jetson" ] || [ "$PLATFORM" = "rpi" ]; then
        log "${RED}REBOOT RECOMMENDED${NC} for all hardware changes to take effect"
        log "   ${BLUE}sudo reboot${NC}"
        log ""
    fi

    log "Installation log: ${LOG_FILE}"
}

# Main execution
main() {
    check_root
    detect_platform
    print_header

    step_update_system
    step_install_dependencies
    step_install_sdl2
    step_install_ai
    step_configure_hardware
    step_set_permissions
    step_install_python
    step_create_services
    step_verify

    print_next_steps
}

# Execute main if run directly (guard clause enables sourcing for tests)
if [ "${BASH_SOURCE[0]}" = "${0}" ]; then
    main "$@"
fi
