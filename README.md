# Virtualized Embedded Media Controller for Network-Based Playback

A Embedded Linux music player system demonstrating kernel driver development, device tree integration, socket server implementation, and cloud streaming capabilities on Raspberry Pi 4.

> **Course**: Advanced Embedded Software Development (AESD) - Final Project  
> **Platform**: Raspberry Pi 4 Model B | Custom Buildroot Linux (ARM64) | Linux Kernel 6.6.78-v8

For full project details and documentation, please see the [Project Overview Wiki Page](https://github.com/cu-ecen-aeld/final-project-prudhvibelide/wiki/Project-Overview).

---

## Project Overview

This project implements a complete embedded media controller with dual-mode operation:

- **Local Playback**: MP3 files stored on the SD card
- **Cloud Streaming**: HTTPS-based streaming from GitHub-hosted MP3 files

**Control Interfaces**:
- Physical hardware inputs (buttons + rotary encoder) via custom kernel driver
- Remote HTTP control (port 8888) via web browser or command-line tools
- Real-time visual feedback on HDMI display (TTY1)

---

## Key Features

### Hardware Integration
- **7-GPIO Input System**:
  - 3 control buttons (Play/Pause, Next, Previous)
  - KY-040 rotary encoder (volume control with push-button mute)
  - Cloud/Local mode toggle button
- **Interrupt-driven** button handling with software debouncing
- **Platform device** architecture following Linux kernel best practices

### Software Stack
- **Custom kernel driver** (`music_input`) exposing `/dev/music_input` character device
- **Device Tree overlay** for hardware configuration
- **User-space daemon** multiplexing hardware events and HTTP requests
- **HTTP server** for remote control and web interface
- **ALSA integration** for audio output and volume management

### Cloud Capabilities
- HTTPS streaming using `wget` with OpenSSL and CA certificates
- GitHub Pages hosting for cloud MP3 library
- Network-transparent audio playback

---

## System Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    Hardware Layer                            │
│  [Buttons] [Rotary Encoder] [HDMI Display] [Audio Output]   │
└────────────────────────┬────────────────────────────────────┘
                         │
┌────────────────────────▼────────────────────────────────────┐
│              Kernel Space (Linux 6.6.78-v8)                  │
│  ┌──────────────────────────────────────────────────────┐   │
│  │  Device Tree Overlay (music-input.dtbo)              │   │
│  │  ├─ GPIO Pin Mappings                                │   │
│  │  └─ Platform Device Configuration                    │   │
│  └──────────────────────┬───────────────────────────────┘   │
│                         │                                    │
│  ┌──────────────────────▼───────────────────────────────┐   │
│  │  music_input Platform Driver                         │   │
│  │  ├─ GPIO IRQ Handlers (debouncing)                   │   │
│  │  ├─ Circular Buffer (event queue)                    │   │
│  │  └─ Character Device (/dev/music_input)              │   │
│  └──────────────────────────────────────────────────────┘   │
└────────────────────────┬────────────────────────────────────┘
                         │ read() / poll()
┌────────────────────────▼────────────────────────────────────┐
│                   User Space                                 │
│  ┌──────────────────────────────────────────────────────┐   │
│  │  music_daemon (Main Application)                     │   │
│  │  ├─ Event Dispatcher (poll() multiplexing)           │   │
│  │  ├─ Playback Controller (mpg123)                     │   │
│  │  ├─ Volume Manager (amixer)                          │   │
│  │  ├─ HTTP Server (port 8888)                          │   │
│  │  └─ TTY1 UI Manager                                  │   │
│  └──────────────────────────────────────────────────────┘   │
│                         │                                    │
│  ┌──────────────────────┴───────────────────────────────┐   │
│  │  Local MP3 Files    │    Cloud MP3 Streaming         │   │
│  │  /usr/share/music/  │    wget + HTTPS + mpg123       │   │
│  └──────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

---

## Technical Implementation

### Kernel Driver Development

**Platform Driver Architecture**:
```c
static const struct of_device_id music_input_of_match[] = {
    { .compatible = "music-input-device" },
    { }
};

static struct platform_driver music_input_driver = {
    .probe = music_input_probe,
    .remove = music_input_remove,
    .driver = {
        .name = "music-input",
        .of_match_table = music_input_of_match,
    },
};
```

**Key Driver Features**:
- Managed resource allocation (`devm_*` APIs) for automatic cleanup
- Interrupt-driven GPIO handling with falling-edge detection
- Circular buffer for event queuing (thread-safe with spinlocks)
- Blocking `read()` implementation with wait queues
- Single-byte event protocol (`'P'`, `'N'`, `'R'`, `'U'`, `'D'`, `'M'`, `'C'`)

### Device Tree Integration

The hardware configuration is defined via Device Tree overlay:

```dts
/ {
    compatible = "brcm,bcm2711";
    
    fragment@0 {
        target-path = "/soc";
        __overlay__ {
            music_input: music-input-device {
                compatible = "music-input-device";
                play-gpios = <&gpio 17 GPIO_ACTIVE_LOW>;
                next-gpios = <&gpio 27 GPIO_ACTIVE_LOW>;
                prev-gpios = <&gpio 22 GPIO_ACTIVE_LOW>;
                // ... additional GPIO definitions
            };
        };
    };
};
```

### User-Space Daemon

**Event Multiplexing**:
```c
struct pollfd fds[2];
fds[0].fd = dev_fd;         // /dev/music_input
fds[0].events = POLLIN;
fds[1].fd = server_sock;    // HTTP server socket
fds[1].events = POLLIN;

poll(fds, 2, -1);
```

**Dual-Mode Playback**:
- **Local**: `mpg123 -q /usr/share/music/song.mp3`
- **Cloud**: `wget -qO- "https://example.github.io/music/song.mp3" | mpg123 -q -`

---

## Build System

### Buildroot Integration

The project uses Buildroot's external tree mechanism:

```
br-external/
├── Config.in                    # External package inclusion
├── external.desc                # External tree metadata
├── external.mk                  # Top-level makefile
├── board/                       # Board-specific configuration
├── configs/                     # Custom defconfigs
├── overlay/                     # Root filesystem overlay
│   ├── etc/init.d/              # Startup scripts
│   ├── usr/share/music/         # Local MP3 files
│   └── boot/overlays/           # Device Tree overlays
└── package/
    ├── music-input-driver/      # Kernel module package
    │   ├── Config.in
    │   └── music-input-driver.mk
    └── music-daemon/            # User-space daemon package
        ├── Config.in
        └── music-daemon.mk
```

### Building the System

```bash
# Configure Buildroot
make raspberrypi4_64_defconfig
make menuconfig  # Enable custom packages from br-external

# Build complete image
make

# Deploy to SD card
sudo ./scripts/deploy_sd.sh /dev/sdX
```

---

## Usage

### Physical Controls

| Input | Action |
|-------|--------|
| Play/Pause Button | Toggle playback state |
| Next Button | Skip to next track |
| Previous Button | Return to previous track |
| Rotary Encoder CW | Increase volume |
| Rotary Encoder CCW | Decrease volume |
| Encoder Push Button | Mute/unmute audio |
| Cloud/Local Toggle | Switch between local and cloud mode |

### HTTP Remote Control

The daemon exposes a simple HTTP API on port 8888:

```bash
# Control via curl
curl http://raspberrypi.local:8888/play
curl http://raspberrypi.local:8888/next
curl http://raspberrypi.local:8888/prev
curl http://raspberrypi.local:8888/vol_up
curl http://raspberrypi.local:8888/vol_down
curl http://raspberrypi.local:8888/local?song=3
curl http://raspberrypi.local:8888/cloud?song=1

# Web interface
firefox http://raspberrypi.local:8888/
```

### HDMI Display Output

Real-time status displayed on TTY1:
```
┌────────────────────────────────────┐
│  Now Playing: [1/5]                │
│  Song: Run it UP                   │
│  Artist: Hanuman Kind              │
│  Mode: LOCAL PLAYBACK              │
│  Status: ▶ PLAYING                 │
│  Volume: 75%                       │
└────────────────────────────────────┘
```

---

## Hardware Requirements

### Hardware Components

- **Raspberry Pi 4 Model B**
- **MicroSD Card** (128 GB)
- **7 GPIO Connections**:
  - 3× Momentary push buttons (Play, Next, Prev)
  - 1× Toggle button (Cloud/Local mode)
  - 1× KY-040 Rotary Encoder
- **Pull-up/Pull-down resistors** (if external; internal pull-ups used in this design)
- **HDMI Display** (for status UI)
- **Audio Output**: HDMI audio or 3.5mm jack / USB audio device
- **Network Connection**: Ethernet or WiFi for cloud streaming

### GPIO Pin Mapping

| Function | GPIO Pin | Configuration |
|----------|----------|---------------|
| Play/Pause | GPIO 17 | Input, Pull-up, Active-low |
| Next | GPIO 27 | Input, Pull-up, Active-low |
| Previous | GPIO 22 | Input, Pull-up, Active-low |
| Cloud Toggle | GPIO 23 | Input, Pull-up, Active-low |
| Encoder CLK | GPIO 5 | Input, Pull-up |
| Encoder DT | GPIO 6 | Input, Pull-up |
| Encoder SW | GPIO 13 | Input, Pull-up, Active-low |

---

## Dependencies

### Kernel Configuration
- `CONFIG_GPIO_BCM2835` - Broadcom BCM2835 GPIO support
- `CONFIG_SND_BCM2835` - ALSA driver for Raspberry Pi audio
- `CONFIG_OF` - Device Tree support
- `CONFIG_GPIOLIB` - GPIO subsystem

### Buildroot Packages
- **alsa-utils** - `amixer` for volume control
- **mpg123** - MP3 player
- **wget** - HTTPS streaming (compiled with OpenSSL)
- **openssl** - SSL/TLS support
- **ca-certificates** - Root CA bundle for HTTPS
- **dropbear** - Lightweight SSH server

---

## Development Insights

### Challenges Overcome

1. **Buildroot Caching Issues**
   - Problem: Code changes not reflecting in compiled binaries
   - Solution: Aggressive cache clearing and rebuild strategies

2. **Socket Blocking in HTTP Handlers**
   - Problem: `system()` calls blocking `accept()` loop
   - Solution: Migrated to `fork()` + `exec()` for non-blocking command execution

3. **Device Tree Platform Device Creation**
   - Problem: Driver not probing when device node in root
   - Solution: Moved device node to `/soc` path in overlay

4. **WiFi Driver Integration**
   - Problem: `brcmfmac` driver not loading automatically
   - Solution: Firmware installation and proper kernel configuration

5. **Kernel API Compatibility**
   - Problem: `class_create()` signature changed in kernel 6.4+
   - Solution: Updated driver to use new single-argument API

### Design Decisions

**Why Platform Driver?**
- Proper integration with Device Tree
- Automatic resource management via `devm_*` APIs
- Follows Linux kernel best practices
- Enables hardware abstraction

**Why Character Device?**
- Simple event-based interface
- Non-blocking with `poll()` support
- Familiar UNIX file I/O semantics

**Why Single Daemon?**
- Unified event handling via `poll()` multiplexing
- Reduced context switching
- Simpler state management
- Lower resource overhead

---

## Future Enhancements

### Planned Features

1. **Hypervisor Integration**
   - Isolate local playback domain from network domain
   - Use Qualcomm Gunyah or Xen hypervisor
   - Prevent network failures from affecting audio

2. **Advanced Cloud Features**
   - Spotify/streaming service integration
   - Playlist synchronization
   - Album artwork display

3. **Enhanced UI**
   - Framebuffer graphics instead of text
   - LCD display support
   - Web-based configuration interface

4. **Power Management**
   - Sleep mode when idle
   - Resume playback on wake

---

## Related Coursework

This project builds upon concepts from:

- **Character Device Driver Assignment (`aesdchar`)**
  - Implemented custom `/dev/music_input` character device
  - Ring buffer management and blocking I/O

- **Socket Programming Assignment (`aesdsocket`)**
  - TCP server implementation
  - HTTP request parsing and response handling

- **Buildroot Integration Assignments**
  - External tree structure
  - Custom package creation
  - System service integration

---

