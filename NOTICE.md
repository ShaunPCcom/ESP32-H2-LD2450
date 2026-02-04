# NOTICE / Third-Party Attributions

This project incorporates or is derived from third-party open-source code.

## TillFleisch/ESPHome-HLK-LD2450 (MIT)

**Upstream**: https://github.com/TillFleisch/ESPHome-HLK-LD2450
**License**: MIT (Copyright 2023 Till Fleisch)
**License text**: third_party/ESPHome-HLK-LD2450/LICENCE

**What was derived**: UART binary protocol decoding logic, specifically:
- Sign handling for signed 16-bit coordinate values
- Coordinate byte interpretation and unpacking
- Presence detection approach based on target tracking

**Derived files**: `components/ld2450/ld2450_parser.c`
This is a clean-room C reimplementation of the ESPHome C++ decode logic
from `components/ld2450_ble/ld2450_ble.cpp`. The code was rewritten from
scratch for ESP-IDF with a different API, but the underlying protocol
interpretation logic follows the same approach documented in the upstream project.

**Note**: The zone polygon containment logic (`ld2450_zone.c`) is original
to this project and not derived from the upstream ESPHome component.
