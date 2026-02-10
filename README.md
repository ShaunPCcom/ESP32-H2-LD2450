# LD2450-ZB-H2

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

## Overview

ESP32-H2 + HLK-LD2450 mmWave presence sensor with native Zigbee support. This firmware is a Zigbee alternative to ESPHome-based implementations, bringing native Zigbee mesh networking to the LD2450 sensor.

The LD2450 is a 24GHz mmWave radar sensor that tracks up to 3 targets simultaneously, reporting their X/Y coordinates in millimeters. This firmware adds **5 configurable quadrilateral zones** for room-level presence detection and integrates everything into Home Assistant via Zigbee2MQTT.

**Based on**: [TillFleisch/ESPHome-HLK-LD2450](https://github.com/TillFleisch/ESPHome-HLK-LD2450) - UART protocol implementation derived from this ESPHome component (MIT License). Reimplemented in C for ESP-IDF with Zigbee support and multi-zone architecture.

## Features

- **3-target tracking**: Real-time X/Y coordinates (mm) for up to 3 moving targets
- **5 configurable zones**: Define custom quadrilateral presence detection areas (e.g., "couch", "desk", "bed")
- **Zigbee2MQTT integration**: 59 Home Assistant entities via external converter
- **NVS persistence**: All configuration survives reboots (independent of coordinator)
- **Serial CLI**: Configure zones, tracking mode, distance/angle limits over UART
- **LED status indicator**: WS2812 RGB shows connection state
- **Two-level factory reset**: 3s hold = Zigbee network reset (keeps config), 10s hold = full factory reset

## Comparison: Zigbee vs ESPHome

This Zigbee implementation offers different trade-offs compared to ESPHome-based versions:

### **Advantages of Zigbee Version**
- ✅ **Native Zigbee** - No WiFi configuration, works with any Zigbee coordinator
- ✅ **Mesh networking** - Router-capable, extends Zigbee network range
- ✅ **NVS persistence** - Config survives without coordinator connection
- ✅ **Multi-endpoint architecture** - 6 Zigbee endpoints (cleaner HA organization)
- ✅ **Two-level factory reset** - Separate Zigbee vs full config reset
- ✅ **Serial CLI** - Direct UART configuration without network dependency
- ✅ **5 configurable zones** - More than typical ESPHome examples (which show 1 zone)

### **Advantages of ESPHome Version**
- ✅ **Unlimited zones** - Component supports unlimited zones (vs fixed 5)
- ✅ **Flexible polygons** - 3+ vertices per zone (vs fixed 4-vertex quadrilaterals)
- ✅ **Rich per-target data** - Individual speed, distance, angle sensors per target
- ✅ **Dynamic zone updates** - Runtime polygon updates via actions
- ✅ **Web interface** - ESPHome web UI for configuration
- ✅ **WiFi diagnostics** - Built-in web-based tools

### **Equivalent Features**
- Occupancy detection (overall + per-zone)
- Target count reporting
- Max distance / angle limits
- Tracking mode (single/multi)
- Coordinate publishing
- Restart button

**Choose Zigbee if**: You want native Zigbee mesh, simpler setup, or network-independent config.
**Choose ESPHome if**: You need unlimited zones, flexible polygons, or prefer WiFi/web management.

## Hardware

### Requirements

- **ESP32-H2 DevKit** (native 802.15.4 Zigbee radio)
- **HLK-LD2450** 24GHz mmWave radar module
- 5V power supply (USB or mains adapter)

### Wiring

| ESP32-H2 Pin | LD2450 Pin | Function |
|--------------|------------|----------|
| GPIO12       | RX         | ESP32 TX → LD2450 RX (commands) |
| GPIO22       | TX         | ESP32 RX ← LD2450 TX (data) |
| GPIO8        | -          | Status LED (built-in on most DevKits) |
| GPIO9        | -          | BOOT button (factory reset) |
| 5V           | 5V         | Power |
| GND          | GND        | Ground |

**Notes**:
- UART baud rate: 256000
- GPIO9 is the ESP32-H2 DevKit BOOT button (active-low, internal pull-up)
- **GPIO8 Status LED**: Many ESP32-H2 development boards (such as the ESP32-H2-DevKitM-1) include a built-in addressable RGB LED (WS2812) connected to GPIO8. This is the programmable status LED that shows Zigbee connection state. The firmware uses the ESP-IDF `led_strip` driver to display color-coded status. Note: Your board may also have a separate red power LED that stays on when powered - this is not the status LED.

## Building

### Prerequisites

- **ESP-IDF v5.5+** ([installation guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32h2/get-started/))
- Git

### Build and Flash

```bash
# Clone the repository
git clone <repository-url>
cd ld2450_zb_h2

# Set up ESP-IDF environment (do this in every new terminal session)
. $HOME/esp/esp-idf/export.sh

# Configure, build, and flash
idf.py set-target esp32h2
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

**Note**: `idf.py monitor` will trigger a device reboot on ESP32-H2 due to DTR/RTS reset behavior. This is expected. Use `idf.py monitor --no-reset` to avoid triggering a reboot when attaching.

## Zigbee2MQTT Setup

1. **Install the external converter**:
   ```bash
   cp z2m/ld2450_zb_h2.js /path/to/zigbee2mqtt/data/external_converters/
   ```

2. **Restart Zigbee2MQTT**

3. **Pair the device**:
   - Ensure the device is not already paired (LED shows amber "not joined")
   - In Z2M UI, click "Permit join (All)"
   - Power cycle the ESP32-H2 or press the reset button
   - Device will auto-pair and appear in Z2M

4. **Reconfigure** (if entities are missing):
   - In Z2M, select the device
   - Click "Reconfigure" in device settings
   - Refresh Home Assistant to see all 59 entities

## Exposed Entities

All entities are automatically discovered in Home Assistant via Zigbee2MQTT:

### Sensors (Read-only)

| Entity | Type | Description |
|--------|------|-------------|
| `sensor.ld2450_target_count` | Numeric (0-3) | Number of tracked targets |
| `sensor.ld2450_target_coords` | String | Target coordinates: "x1,y1;x2,y2;x3,y3" (mm) |
| `binary_sensor.ld2450_occupancy` | Binary | Overall occupancy (any target present) |
| `binary_sensor.ld2450_zone_1_occupancy` | Binary | Zone 1 occupancy |
| `binary_sensor.ld2450_zone_2_occupancy` | Binary | Zone 2 occupancy |
| `binary_sensor.ld2450_zone_3_occupancy` | Binary | Zone 3 occupancy |
| `binary_sensor.ld2450_zone_4_occupancy` | Binary | Zone 4 occupancy |
| `binary_sensor.ld2450_zone_5_occupancy` | Binary | Zone 5 occupancy |

### Configuration (Read-Write)

| Entity | Type | Range | Description |
|--------|------|-------|-------------|
| `number.ld2450_max_distance` | Numeric | 0-6000 mm | Max detection distance |
| `number.ld2450_angle_left` | Numeric | 0-60° | Left angle limit |
| `number.ld2450_angle_right` | Numeric | 0-60° | Right angle limit |
| `select.ld2450_tracking_mode` | Select | Multi/Single | Tracking mode |
| `switch.ld2450_coord_publishing` | Switch | ON/OFF | Enable coordinate publishing |

### Zone Vertices (8 per zone, 40 total)

Each of the 5 zones has 4 polygon vertices (X1, Y1, X2, Y2, X3, Y3, X4, Y4):

- `number.ld2450_zone_1_x1` through `number.ld2450_zone_1_y4` (8 entities)
- `number.ld2450_zone_2_x1` through `number.ld2450_zone_2_y4` (8 entities)
- ...
- `number.ld2450_zone_5_x1` through `number.ld2450_zone_5_y4` (8 entities)

### Actions

| Entity | Type | Description |
|--------|------|-------------|
| `button.ld2450_restart` | Button | Restart the device |

**Total**: 59 entities (6 occupancy sensors, 5 config controls, 40 zone vertices, 8 read-only attributes)

## Configuration

### Via Home Assistant

All writable configuration is exposed as entities in Home Assistant. Changes are automatically:
1. Sent to the device via Zigbee
2. Applied to the LD2450 sensor hardware
3. Saved to NVS (persist across reboots)

### Via Serial CLI

Connect a serial terminal (115200 baud) to see the CLI prompt:

```bash
# View sensor state
ld state

# Configure a zone (4-point polygon, coordinates in mm)
ld zone 1 -3000,-2000 3000,-2000 3000,2000 -3000,2000
ld zone 1 on

# Set max distance and angle limits (applied via zone filter)
ld maxdist 5000
ld angle 45 45

# Set tracking mode
ld mode multi  # or: ld mode single

# Enable coordinate publishing
ld coords on

# Bluetooth control
ld bt off

# View current config
ld config

# Test NVS health (diagnostics)
ld nvs

# Restart device
ld reboot

# Full factory reset (erase Zigbee network + NVS config)
ld factory-reset
```

**Note**: CLI changes are immediately saved to NVS and persist across reboots.

### Configuration Best Practices

**During Zone Setup:**
1. Enable **coordinate publishing** (`switch.ld2450_coord_publishing` → ON)
2. Enable **single target tracking** (`switch.ld2450_tracking_mode` → Single Target)
3. Walk through each zone one at a time to verify boundaries
4. Use a Plotly graph card (see examples) to visualize target position and zones

**During Normal Operation:**
1. Disable **coordinate publishing** (`switch.ld2450_coord_publishing` → OFF)
   - Reduces Zigbee traffic
   - Only needed for setup/debugging
2. Enable **multi-target tracking** (`switch.ld2450_tracking_mode` → Multi Target)
   - Tracks up to 3 people simultaneously
   - Better for real-world occupancy detection

**Why This Matters:**
- **Single target mode** during setup makes it easier to test one zone at a time (only one person tracked)
- **Coordinate publishing off** during normal operation reduces network overhead (occupancy is all you need for automations)
- **Multi-target mode** during operation allows tracking multiple people moving through different zones

## Examples

### Home Assistant Plotly Dashboard

The `examples/home-assistant/` directory includes interactive Plotly dashboard examples for visualizing sensor data and zones. These dashboards are useful during zone setup and for debugging target tracking behavior.

See [`examples/home-assistant/README.md`](examples/home-assistant/README.md) for setup instructions.

## LED Status

The built-in LED on GPIO8 indicates the current Zigbee connection state:

| Color/Pattern | Meaning |
|---------------|---------|
| **Amber blink** | Not joined to a Zigbee network |
| **Blue blink** | Pairing in progress (steering mode) |
| **Green solid** | Joined to network and operational |
| **Red blink** | Error or factory reset in progress (check serial logs) |

**Note**: RGB color indication requires an addressable RGB LED (WS2812-compatible) on GPIO8, which is standard on many ESP32-H2 development boards like the ESP32-H2-DevKitM-1. This is the programmable LED that changes colors, not the power LED. If your board only has a simple single-color LED on GPIO8, you'll see blink patterns (on/off) instead of color changes, which still provides basic status feedback.

## Factory Reset

Two levels of reset are available via the BOOT button (GPIO9):

### Zigbee Network Reset (3 seconds)
Leaves the Zigbee network but **keeps zones and configuration**. Useful for moving the sensor to a different coordinator.

1. Hold BOOT button for 3-10 seconds
2. **LED feedback**:
   - 1-3s: Fast red blink (building to reset)
   - 3-10s: Slow red blink (Zigbee reset armed)
3. Release between 3-10 seconds
4. Device resets Zigbee network, keeps config, ready to re-pair

### Full Factory Reset (10 seconds)
Erases **both** Zigbee network and NVS configuration (zones, max distance, angles, etc.). Complete reset to defaults.

1. Hold BOOT button for >10 seconds
2. **LED feedback**:
   - 1-3s: Fast red blink
   - 3-10s: Slow red blink
   - >10s: Solid red (full reset armed)
3. Release after 10 seconds
4. Device resets everything to factory defaults

**Note**: You can also trigger a Zigbee network reset from Zigbee2MQTT UI (Device → Settings → Remove). For a full reset via CLI, use `ld factory-reset`.

## Architecture

- **Sensor driver**: `components/ld2450/` - UART RX task, protocol parser, zone logic
- **Command encoder**: `components/ld2450/ld2450_cmd.c` - UART TX, config mode, ACK reader
- **Zigbee app**: `main/zigbee_app.c` - 6 endpoints, custom clusters, attribute reports
- **NVS persistence**: `main/nvs_config.c` - Load/save all config on boot/change
- **Z2M converter**: `z2m/ld2450_zb_h2.js` - Custom cluster defs, fromZigbee/toZigbee

### Zigbee Endpoints

- **EP 1**: Main device (overall occupancy + config attributes)
- **EP 2-6**: Zones 1-5 (per-zone occupancy + vertex attributes)

### Custom Clusters

- **0xFC00** (EP 1): Target count, coordinates, max distance, angle, tracking mode, coord publishing, restart
- **0xFC01** (EP 2-6): Zone vertices (8 signed 16-bit attributes: X1, Y1, X2, Y2, X3, Y3, X4, Y4)

## Acknowledgments

This project's UART binary protocol decoding logic is derived from [TillFleisch/ESPHome-HLK-LD2450](https://github.com/TillFleisch/ESPHome-HLK-LD2450) (MIT License). The code was reimplemented from scratch in C for ESP-IDF, but the protocol interpretation approach follows the upstream ESPHome component. See `NOTICE.md` and `third_party/ESPHome-HLK-LD2450/README.md` for details.

## License

MIT License - see [LICENSE](LICENSE) file for details.

Copyright (c) 2026 Shaun Foulkes
