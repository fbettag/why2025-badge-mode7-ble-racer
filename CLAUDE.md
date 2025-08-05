# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Mode-7 BLE 1v1 Racer for ESP32-P4 (WHY2025 Badge) - A real-time multiplayer racing game with pseudo-3D graphics, BLE P2P networking, and hardware-accelerated rendering.

## Build Commands

```bash
# Set up ESP-IDF environment (required before any build)
. /opt/esp/idf/export.sh

# Build the project
idf.py build

# Flash to ESP32-P4
idf.py -p /dev/ttyUSB0 flash

# Monitor serial output
idf.py -p /dev/ttyUSB0 monitor

# Build, flash and monitor in one command
idf.py -p /dev/ttyUSB0 flash monitor

# Configure project settings
idf.py menuconfig

# Clean build
idf.py fullclean

# Size analysis
idf.py size
idf.py size-components

# Single component rebuild
idf.py -C components/game build
```

## Architecture Overview

### Component Interaction Flow
```
app_main() → app_init() → game_loop_run()
    ↓
[Input System] → [Physics Update] → [BLE Sync] → [Mode-7 Render] → [Display]
```

### Key Architectural Patterns

1. **Fixed-Point Math Throughout**: All physics and rendering use 16.16 fixed-point math (see `components/game/include/math.h`). Never use floating-point in performance-critical paths.

2. **Dual-Buffer Rendering**: Display uses double buffering with PSRAM. Frame buffers allocated in `components/display/display.c`. Always check PSRAM availability and fall back to regular RAM.

3. **State Machine Game Flow**: 
   - States: MENU → LOBBY → COUNTDOWN → RACING → RESULTS
   - Managed in `main/game_loop.c:game_set_state()`
   - Each state has its own update function

4. **BLE P2P Protocol**:
   - Host/Client topology, no server
   - Game state packets are 32 bytes, sent at 20-50Hz
   - Connection validation required before any BLE operation
   - See `components/ble/protocol.c` for packet format

5. **Mode-7 Rendering Pipeline**:
   - Per-scanline affine transforms using precomputed LUTs
   - 2D-DMA hardware acceleration for texture blits
   - Track data is 1024×1024 tilemap with 8×8 tiles
   - Rendering happens in `game_render()` after physics update

### Memory Management

- **PSRAM**: Frame buffers, track cache, asset storage
- **IRAM**: Critical interrupt handlers, DMA descriptors
- **Flash**: 3MB app, 1MB storage, 2MB tracks partition
- Always use `heap_caps_malloc()` with appropriate caps for DMA buffers

### Critical Performance Constraints

- **Frame Budget**: 33ms (30 FPS target)
  - Physics: 2-3ms
  - Mode-7 render: 12-18ms
  - BLE: <1ms
- **Physics**: Fixed 60Hz update rate (16ms timestep)
- **Network**: ≤80ms latency, <2% packet loss

## Hardware Configuration

### Pin Assignments (ESP32-P4)
- **LCD Data**: GPIO 39-48 (RGB565)
- **LCD Control**: PCLK(14), HSYNC(21), VSYNC(15), DE(16)
- **IMU**: I2C on GPIO 21(SDA), 22(SCL)
- **Keyboard**: GPIO 24-31 matrix
- **ESP32-C6 UART**: TX(4), RX(5), RST(6)

### ESP-IDF Configuration
Key settings in `sdkconfig.defaults`:
- PSRAM: OCT mode, 80MHz
- 2D-DMA: Enabled with IRAM-safe
- BLE: NimBLE stack, 2M PHY
- CPU: 240MHz, dual-core enabled

## Development Guidelines

### Testing Approach
- Unit tests for physics/math in `main/test_assets.c`
- BLE loopback testing before P2P implementation
- Performance monitoring via `game_get_fps()`

### Common Issues and Solutions

1. **Low FPS**: Enable half-resolution mode in game_config
2. **BLE Connection Failures**: Check 2M PHY support, fall back to 1M
3. **Memory Allocation Failures**: Verify PSRAM enabled in menuconfig
4. **Display Not Working**: Check RGB pin connections and backlight GPIO

### Code Style Requirements
- No floating-point in physics or rendering
- All pointer parameters must be null-checked
- Use ESP_LOG macros for debugging (TAG per component)
- Fixed-point conversions via FLOAT_TO_FIXED16/FIXED16_TO_FLOAT

## Project-Specific Conventions

### Adding New Tracks
1. Create ASCII layout in track file (G=grass, R=road, C=checkpoint)
2. System auto-converts to binary .trk format
3. Place in /spiffs/tracks/ directory
4. Track loader handles caching automatically

### Network State Synchronization
- Host is authoritative for game state
- Client performs prediction + reconciliation
- Input buffer maintains last 32 frames for rollback
- Packet format defined in `components/ble/include/protocol.h`

### Asset Pipeline
- PNG → RGB565 conversion happens automatically
- Tilesheet must be 256 colors max
- Assets cached in PSRAM with LRU eviction
- Use `asset_load_texture()` for loading

## Important File Locations

- **Game Loop**: `main/game_loop.c` - Main state machine and frame timing
- **Physics**: `components/game/physics.c` - Car physics and collision
- **BLE Protocol**: `components/ble/protocol.c` - Network packet handling
- **Mode-7 Renderer**: `components/game/include/mode7.h` - Rendering pipeline
- **Fixed-Point Math**: `components/game/include/math.h` - Math utilities
- **Track Format**: `components/track/include/track_format.h` - Track data structures