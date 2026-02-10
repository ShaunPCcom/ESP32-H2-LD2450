# Home Assistant Dashboard Examples

This directory contains Home Assistant dashboard examples for visualizing and configuring the LD2450 Zigbee sensor.

## Prerequisites

1. **Plotly Graph Card** installed via HACS:
   - In Home Assistant, go to **HACS** → **Frontend**
   - Search for "Plotly Graph Card"
   - Install and restart Home Assistant

2. **Sensor paired with Zigbee2MQTT**:
   - Device should be paired and showing all 59 entities in Home Assistant
   - Note your device's Zigbee IEEE address (e.g., `0x1234567890abcdef`)

## Files

### `plotly-card.yaml`

Standalone Plotly graph card showing:
- **3 target positions** (real-time X/Y coordinates)
- **5 zone boundaries** (configurable polygons)
- **Coverage area** (sensor detection range overlay)

**Usage:**
1. Open the file and replace `YOUR_DEVICE_ID` with your device's Zigbee IEEE address
2. In Home Assistant, create a new manual Lovelace card
3. Paste the modified YAML content

### `zone-dashboard.yaml`

Complete dashboard tab with:
- **Zone coordinate inputs** (8 number inputs per zone for 4 vertices)
- **Plotly graph visualization** (embedded in the dashboard)
- **Occupancy badges** (quick status for all 5 zones)
- **Control buttons** (restart device, toggle tracking mode/coordinates)

**Usage:**
1. Open the file and replace all instances of `YOUR_DEVICE_ID` with your Zigbee IEEE address
2. In Home Assistant, go to **Settings** → **Dashboards** → **Add Dashboard**
3. Choose "New dashboard from scratch"
4. Edit dashboard → Three dots menu → "Raw configuration editor"
5. Paste the modified YAML content
6. Customize zone names and headings as needed

## Configuration Workflow

### Initial Zone Setup

1. **Enable coordinate publishing**:
   - Toggle `switch.YOUR_DEVICE_ID_coord_publishing` to ON

2. **Enable single target mode**:
   - Toggle `switch.YOUR_DEVICE_ID_tracking_mode` to Single Target
   - This makes it easier to test one zone at a time

3. **Configure zones**:
   - Use the Plotly graph to visualize the sensor coverage area
   - For each zone, enter 4 vertex coordinates (X1,Y1 through X4,Y4)
   - Walk through each zone and verify the occupancy binary sensor triggers
   - Adjust coordinates as needed

4. **Return to normal operation**:
   - Toggle `switch.YOUR_DEVICE_ID_coord_publishing` to OFF (reduces Zigbee traffic)
   - Toggle `switch.YOUR_DEVICE_ID_tracking_mode` to Multi Target (track up to 3 people)

### Coordinate System

- **Origin (0,0)**: Sensor location
- **X-axis**: Left/right (-6000mm to +6000mm)
  - Negative X = Left of sensor
  - Positive X = Right of sensor
- **Y-axis**: Forward distance (0mm to 6000mm)
  - 0mm = Sensor location
  - 6000mm = 6 meters forward

Coordinates are in **millimeters**. Example zone covering a couch 2-4 meters forward, 1 meter wide:
- X1: -500 (left edge, 50cm left of center)
- Y1: 2000 (front edge, 2m forward)
- X2: 500 (right edge, 50cm right of center)
- Y2: 2000 (front edge, 2m forward)
- X3: 500 (right edge at back)
- Y3: 4000 (back edge, 4m forward)
- X4: -500 (left edge at back)
- Y4: 4000 (back edge, 4m forward)

## Attribution

Plotly graph visualization adapted from **Anthony Hua's LD2450-map-chart.yaml**:
https://github.com/athua/ha-utils/blob/main/LD2450-map-chart.yaml

Modified for:
- Zigbee entity naming (vs ESPHome)
- 5 zones (vs 4 zones)
- 6000mm coverage (vs 7500mm)

## Tips

- **Zone overlap**: Zones can overlap. A target in multiple zones will trigger all overlapping zone occupancy sensors.
- **Zone disable**: Set all coordinates to 0 to effectively disable a zone.
- **Testing**: Stand still in a zone for 2-3 seconds to ensure stable detection before moving to test the next zone.
- **Automation delays**: Add a small delay (1-2 seconds) in automations to avoid triggering on brief passes through zones.

## Troubleshooting

**Graph not showing targets:**
- Ensure coordinate publishing is enabled (`switch.YOUR_DEVICE_ID_coord_publishing`)
- Check that targets are detected (`sensor.YOUR_DEVICE_ID_target_count` > 0)
- Verify Plotly Graph Card is installed via HACS

**Zone not triggering:**
- Verify zone coordinates form a valid quadrilateral
- Check that you're within the 6-meter detection range
- Ensure the zone is enabled (at least one non-zero coordinate)
- Try increasing the zone size slightly

**Entities not found:**
- Verify device is paired with Zigbee2MQTT
- Hit "Reconfigure" on the device in Z2M settings
- Check that all entity IDs use your actual Zigbee IEEE address
