# Release Process

This project uses automated releases triggered by git tags.

## Quick Start

**To release a new version:**

```bash
# 1. Update version in code
vim main/zigbee_app.c      # Change current_file_version
vim main/zigbee_defs.h     # Change ZB_SW_BUILD_ID

# 2. Commit the version bump
git commit -am "chore: bump version to v1.0.0.2"
git push

# 3. Create and push tag (triggers automation)
git tag -a v1.0.0.2 -m "Release v1.0.0.2 - Description"
git push origin v1.0.0.2

# 4. Wait 5-10 minutes - automation handles everything else
```

## What Happens Automatically

When you push a tag matching `v*.*.*.*` or `v*.*.*`:

1. ✅ **Validates** version in code matches tag
2. ✅ **Builds** firmware with ESP-IDF
3. ✅ **Creates** OTA image (Zigbee format)
4. ✅ **Publishes** GitHub release with OTA file
5. ✅ **Updates** `z2m/ota_index.json` with new version
6. ✅ **Commits** and pushes the updated index

**Result**: Users see update notification in Home Assistant within 24 hours.

## Version Format

Tags must match: `v{major}.{minor}.{patch}.{build}`

Examples:
- `v1.0.0.0` - Initial release
- `v1.0.0.1` - Patch release
- `v1.1.0.0` - Minor update
- `v2.0.0.0` - Major version

**Important**: The build number (4th digit) is optional:
- `v1.0.0` is equivalent to `v1.0.0.0`
- `v1.2.3` is equivalent to `v1.2.3.0`

## Version Update Checklist

Before creating a tag, ensure version is updated in **both** files:

### 1. `main/zigbee_app.c`

```c
zigbee_ota_config_t ota_cfg = ZIGBEE_OTA_CONFIG_DEFAULT();
ota_cfg.manufacturer_code = 0x131B;
ota_cfg.image_type = 0x0001;
ota_cfg.current_file_version = 0x00010002;  // ← Update this (hex)
```

**Hex conversion**:
- v1.0.0.2 = `0x00010002`
- v1.2.3.4 = `0x01020304`

### 2. `main/zigbee_defs.h`

```c
#define ZB_SW_BUILD_ID  "\x07""1.0.0.2"  // ← Update this (string)
```

**String format**: Length byte + version string
- Length = number of characters (e.g., "1.0.0.2" = 7 chars = `\x07`)

### 3. Optional: Update version banner

If you added a version banner in `zigbee_app.c`:
```c
ESP_LOGI(TAG, "LD2450 Firmware Version: v1.0.0.2");  // ← Update
```

## Monitoring the Release

1. **GitHub Actions**: https://github.com/ShaunPCcom/ESP32-H2-LD2450/actions
   - Watch the workflow run in real-time
   - Check for any errors

2. **Releases Page**: https://github.com/ShaunPCcom/ESP32-H2-LD2450/releases
   - New release appears automatically
   - OTA file is attached as asset

3. **OTA Index**: Check `z2m/ota_index.json` was updated
   - New commit from `github-actions[bot]`
   - File contains latest version

## Troubleshooting

### Error: Version mismatch

```
❌ ERROR: Version mismatch in main/zigbee_app.c
Expected: current_file_version = 0x00010002
```

**Fix**: Update the version in code to match your tag, then recreate the tag:
```bash
# Delete the tag
git tag -d v1.0.0.2
git push origin :refs/tags/v1.0.0.2

# Fix version in code
vim main/zigbee_app.c main/zigbee_defs.h
git commit -am "fix: correct version numbers"
git push

# Recreate tag
git tag -a v1.0.0.2 -m "Release v1.0.0.2"
git push origin v1.0.0.2
```

### Error: Build failed

Check the GitHub Actions logs for ESP-IDF build errors. Fix the code, commit, and create a new tag with an incremented version.

### Release created but no OTA index update

Check if the `github-actions[bot]` has write access to the repository. The workflow uses `${{ github.token }}` which should have sufficient permissions by default.

## Testing a Release

To test the automation without affecting production:

```bash
# Create a test tag
git tag -a v1.0.0.1-test -m "Test release automation"
git push origin v1.0.0.1-test

# Watch it run, then delete the test release
gh release delete v1.0.0.1-test --yes
git tag -d v1.0.0.1-test
git push origin :refs/tags/v1.0.0.1-test
```

## Manual Release (Emergency)

If automation fails and you need to release manually:

```bash
# Build firmware
idf.py build

# Create OTA image
python $ESP_ZIGBEE_SDK/tools/mfg_tool/image_builder_tool.py \
  -mn "LD2450Z" -mc 0x131B -mt 0x0001 -fv 0x00010002 \
  -in build/ld2450_zb_h2.bin -o ld2450_zb_h2_v1.0.0.2.ota

# Create release
gh release create v1.0.0.2 ld2450_zb_h2_v1.0.0.2.ota \
  --title "v1.0.0.2" --notes "Manual release"

# Update OTA index (see workflow for format)
vim z2m/ota_index.json
git commit -am "chore: update OTA index for v1.0.0.2"
git push
```

## Release Checklist

- [ ] All changes committed and pushed
- [ ] Version updated in `main/zigbee_app.c` (hex format)
- [ ] Version updated in `main/zigbee_defs.h` (string format)
- [ ] Version bump committed
- [ ] Tag created and pushed
- [ ] GitHub Actions workflow succeeded (check Actions page)
- [ ] Release visible on Releases page
- [ ] OTA file attached to release
- [ ] `z2m/ota_index.json` updated automatically
- [ ] Test update on a device (optional but recommended)
