# Upstream: TillFleisch/ESPHome-HLK-LD2450

**Source**: https://github.com/TillFleisch/ESPHome-HLK-LD2450
**License**: MIT (Copyright 2023 Till Fleisch)
**Upstream commit**: Referenced at time of clean-room reimplementation (Jan 2026)

## Attribution

This project is a **clean-room reimplementation**, not a direct code import.
The UART binary protocol decoding logic was derived from studying the upstream
ESPHome component's C++ implementation.

### Derived Logic

**File**: `components/ld2450/ld2450_parser.c` (in main project)

**Source inspiration**: `components/ld2450_ble/ld2450_ble.cpp` (upstream)

**What was derived**:
- Binary frame structure (0xAA header, coordinate unpacking, 0x55 footer)
- Sign handling for signed 16-bit coordinate values
- Coordinate byte interpretation (little-endian, sign extension)
- Presence detection approach based on target position tracking

### Modifications

The implementation differs from upstream:
- **Language**: C++ ESPHome → C ESP-IDF
- **API**: Custom state machine API, not ESPHome sensors
- **Split-frame handling**: Added robust UART stream reassembly
- **Architecture**: Separate UART RX task with ring buffer
- **Zone logic**: Polygon containment algorithm is original, not from upstream

### No Direct Code Import

This is not a fork or port. No upstream source files were copied.
The binary protocol interpretation was reimplemented from scratch in C
after understanding the protocol from the upstream C++ implementation.
