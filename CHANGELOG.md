# Changelog

## v2.1.3 - 2026-03-26

Boot reliability fix — the sensor could freeze on power-up, locking zone occupancy
at whatever state was captured during initialization.

### Fixed

- **Boot freeze caused by premature config commands.** The LD2450 sensor and ESP32-H2
  share a power rail, so both boot simultaneously. Config commands (Bluetooth disable,
  distance/angle filter) were sent after only 200ms — before the sensor was ready.
  If exit-config failed silently, the sensor stayed in config mode and stopped streaming
  coordinate data. Now waits for the first valid data frame + 1 second settle time before
  entering config mode.

- **Config mode exit had no error handling.** `exit_config()` previously ignored failures.
  Now retries 3 times, and if all attempts fail, restarts the sensor as a last resort.

- **Duplicate sensor bridge poll loops on boot.** `sensor_bridge_start()` could be called
  from two different Zigbee signal handlers during initialization, creating two racing
  poll loops on shared state. Added a re-entry guard.
