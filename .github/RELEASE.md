# Release Process

This project uses automated releases triggered by git tags.

## Quick Start

**To release a new version:**

```bash
# 1. Update version in code (single source of truth)
vim main/version.h         # Update FW_VERSION_PATCH (or MINOR/MAJOR)

# 2. Commit the version bump
git commit -am "chore: bump version to v1.1.2"
git push

# 3. Create and push tag (triggers automation)
git tag -a v1.1.2 -m "Release v1.1.2 - Description"
git push origin v1.1.2

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

The project uses a **single source of truth** for version management in `main/version.h`.

### Update `main/version.h`

Simply change the three version numbers:

```c
#define FW_VERSION_MAJOR 1
#define FW_VERSION_MINOR 1
#define FW_VERSION_PATCH 2  // ← Increment this for bug fixes
```

**That's it!** All other version representations are automatically derived:
- Hex version for OTA: `FIRMWARE_VERSION` (auto-calculated as `0x00MMNNPP`)
- String version: `FIRMWARE_VERSION_STRING` (auto-generated as `"v1.1.2"`)
- ZCL string: `FIRMWARE_SW_BUILD_ID` (auto-generated with length byte)

**Version bump guidelines:**
- **PATCH** (1.1.1 → 1.1.2): Bug fixes, minor improvements
- **MINOR** (1.1.2 → 1.2.0): New features, backward compatible
- **MAJOR** (1.2.0 → 2.0.0): Breaking changes

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
❌ ERROR: Version mismatch in main/version.h
Expected: FW_VERSION_PATCH = 2
```

**Fix**: Update the version in code to match your tag, then recreate the tag:
```bash
# Delete the tag
git tag -d v1.1.2
git push origin :refs/tags/v1.1.2

# Fix version in code
vim main/version.h
git commit -am "fix: correct version numbers"
git push

# Recreate tag
git tag -a v1.1.2 -m "Release v1.1.2"
git push origin v1.1.2
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
- [ ] Version updated in `main/version.h` (MAJOR/MINOR/PATCH)
- [ ] Version bump committed
- [ ] Tag created and pushed
- [ ] GitHub Actions workflow succeeded (check Actions page)
- [ ] Release visible on Releases page
- [ ] OTA file attached to release
- [ ] `z2m/ota_index.json` updated automatically
- [ ] Test update on a device (optional but recommended)
