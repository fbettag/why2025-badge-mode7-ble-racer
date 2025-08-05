# Mode-7 BLE 1v1 Racer

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen)](https://github.com/yourusername/mode7-ble-racer-why2025)
[![Platform](https://img.shields.io/badge/platform-ESP32--P4-blue)](https://www.espressif.com/)
[![License](https://img.shields.io/badge/license-MIT-yellow)](LICENSE)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.0+-red)](https://github.com/espressif/esp-idf)
[![BLE](https://img.shields.io/badge/BLE-5.0-purple)](https://www.bluetooth.com/)

A high-performance Mode-7 racing game for ESP32-P4 with BLE 5.0 multiplayer support, featuring 720x720 display, IMU steering, and real-time P2P synchronization.

## 🏁 Features

### Core Gameplay
- **Mode-7 Graphics Engine**: Pseudo-3D racing with perspective-correct rendering
- **Real-time Physics**: Car dynamics with realistic handling and collision detection
- **Multiplayer**: 1v1 BLE racing with 25-30 FPS and ≤80ms latency
- **Track System**: Custom track creation and loading with checkpoint system
- **Input Methods**: QWERTY keyboard + MPU6050 IMU tilt steering

### Hardware Support
- **ESP32-P4**: Main processor with 2D-DMA optimized rendering
- **ESP32-C6**: BLE 5.0 coordinator with 2M PHY support
- **Display**: 720x720 LCD with RGB interface
- **Memory**: PSRAM support for large assets and caching
- **Storage**: SPIFFS filesystem for tracks and assets

### Technical Highlights
- **Fixed-point Math**: 16.16 precision for performance-critical calculations
- **Asset Pipeline**: PNG to RGB565 conversion with caching
- **Track Format**: Binary format with heightmaps and collision data
- **BLE Protocol**: Optimized game state synchronization protocol
- **Performance Monitoring**: Real-time FPS and latency tracking

## 🚀 Quick Start

### Prerequisites
- ESP-IDF 6.0 or later
- ESP32-P4 development board
- ESP32-C6 module (for BLE)
- 720x720 LCD display
- MPU6050 IMU sensor

### Build Instructions

```bash
# Set up ESP-IDF environment
. /opt/esp/idf/export.sh

# Configure project
idf.py set-target esp32p4
idf.py menuconfig

# Build and flash
idf.py build
idf.py flash
idf.py monitor
```

### Configuration Options
```bash
# Enable PSRAM
Component config → ESP32P4-specific → Support for external, SPI-connected RAM

# Enable BLE
Component config → Bluetooth → Bluetooth
Component config → Bluetooth → NimBLE Options

# Enable SPIFFS
Component config → SPI Flash driver → SPIFFS Filesystem
```

## 🎮 Game Controls

### Keyboard Controls
- **WASD**: Throttle, brake, steer
- **Enter**: Menu select / Game start
- **ESC**: Menu / Pause
- **Space**: Handbrake

### IMU Controls
- **Tilt Left/Right**: Steering
- **Tilt Forward**: Throttle
- **Tilt Backward**: Brake

## 📁 Project Structure

```
mode7-ble-racer-why2025/
├── main/
│   ├── game_loop.c        # Main game loop and state management
│   ├── game_loop.h
│   └── test_assets.c      # Asset system test suite
├── components/
│   ├── ble/
│   │   ├── ble.c          # BLE stack and communication
│   │   ├── lobby.c        # P2P lobby system
│   │   ├── gatt.c         # GATT services
│   │   └── protocol.c     # Game state protocol
│   ├── game/
│   │   ├── physics.c      # Car physics and collision
│   │   └── math.c         # Fixed-point math utilities
│   ├── display/
│   │   └── display.c      # LCD display driver
│   ├── input/
│   │   └── input.c        # Keyboard and IMU handling
│   ├── track/
│   │   ├── track_loader.c # Track loading and caching
│   │   └── track_format.h # Track file format
│   └── assets/
│       ├── asset_loader.c # Asset loading and caching
│       └── tile_converter.c # Track conversion tools
├── sdkconfig.defaults     # Default configuration
├── partitions.csv         # Partition layout
└── README.md
```

## 🔧 Hardware Setup

### ESP32-P4 Connections
```
LCD Data Lines → GPIO 0-15 (RGB565)
LCD VSYNC → GPIO 16
LCD HSYNC → GPIO 17
LCD DE → GPIO 18
LCD PCLK → GPIO 19

MPU6050 SDA → GPIO 21
MPU6050 SCL → GPIO 22
MPU6050 INT → GPIO 23

Keyboard Matrix → GPIO 24-31
```

### ESP32-C6 BLE Setup
```
UART TX → GPIO 4
UART RX → GPIO 5
RST → GPIO 6
```

## 🎯 Game Modes

### 1. Single Player
- Practice against ghost car
- Time trial mode
- Track exploration

### 2. Multiplayer (1v1)
- Host/Client P2P connection
- Real-time synchronization
- Cross-device racing

## 🛠 Development

### Adding New Tracks
1. Create ASCII track file:
```
### Track Layout ###
#GGGGGGGGGGGGGGGGGG#
#G  RRRRRRRRRRRR  G#
#G  R          R  G#
#G  R  XXXXXX  R  G#
#G  R  CCCCCC  R  G#
#G  R          R  G#
#G  RRRRRRRRRRRR  G#
#GGGGGGGGGGGGGGGGGG#
```
- G = Grass
- R = Road
- C = Checkpoint
- X = Start/Finish
- S = Sand
- W = Water

2. Convert to binary format:
```c
// The system will auto-convert ASCII to .trk format
```

### Adding Assets
1. Place PNG files in `/spiffs/assets/`
2. System auto-converts to RGB565 tilesheets
3. Access via asset loader:
```c
texture_t* tiles = asset_load_texture("/spiffs/assets/tilesheet.png");
```

## 📊 Performance Metrics

### Target Performance
- **Frame Rate**: 25-30 FPS
- **Latency**: ≤80ms (BLE)
- **Memory Usage**: ≤2MB PSRAM
- **CPU Load**: ≤80% on 240MHz core

### Monitoring
- Real-time FPS counter
- BLE latency measurement
- Memory usage tracking
- Cache hit/miss statistics

## 🔍 Debugging

### Enable Debug Logging
```bash
idf.py menuconfig
# Component config → Log output → Default log verbosity → Debug
```

### Common Issues
1. **Display not working**: Check RGB pin connections
2. **BLE connection fails**: Ensure 2M PHY is enabled
3. **Low FPS**: Reduce track complexity or resolution
4. **Memory issues**: Check PSRAM configuration

### Debug Commands
```bash
# Check memory
idf.py size

# Monitor logs
idf.py monitor --baud 115200

# Erase flash
idf.py erase-flash
```

## 🔄 BLE Protocol

### Connection Flow
1. **Discovery**: Device scanning and advertising
2. **Pairing**: BLE connection establishment
3. **Lobby**: Game configuration exchange
4. **Sync**: Real-time game state synchronization

### Packet Types
- **Game State**: 40-byte position/velocity data
- **Input**: 16-byte control input
- **Config**: 16-byte game settings

## 🎨 Customization

### Track Creation
- Use ASCII format for easy editing
- System auto-converts to optimized binary
- Support for checkpoints and collision data

### Asset Pipeline
- PNG to RGB565 conversion
- Automatic tilesheet generation
- Palette-based color reduction

## 📚 API Reference

### Game Loop
```c
esp_err_t game_loop_init(void);
void game_loop_run(void);
void game_loop_deinit(void);
void game_set_state(game_state_t state);
```

### BLE Lobby
```c
esp_err_t lobby_init(const lobby_config_t *config);
esp_err_t lobby_start_hosting(void);
esp_err_t lobby_connect_to_device(const uint8_t *addr);
```

### Asset Loader
```c
texture_t* asset_load_texture(const char *filename);
palette_t* asset_load_palette(const char *filename);
void asset_free_texture(texture_t *texture);
```

## 🤝 Contributing

1. Fork the repository
2. Create feature branch: `git checkout -b feature/new-track`
3. Commit changes: `git commit -am 'Add new track'`
4. Push to branch: `git push origin feature/new-track`
5. Submit pull request

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## 🙏 Acknowledgments

- ESP-IDF team for the excellent framework
- Espressif for ESP32-P4/C6 hardware
- Mode-7 rendering techniques from classic games
- Open source community for inspiration