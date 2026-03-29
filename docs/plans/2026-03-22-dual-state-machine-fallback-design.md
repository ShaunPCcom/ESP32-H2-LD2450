# Dual State Machine Fallback Design

## Goal

Decouple Z2M occupancy reporting (normal mode) from fallback light control (fallback mode) into two independent state machines. Each maintains its own cooldown timers. Reconciliation is one-way: fallback pushes its state to normal on clear.

## Architecture

Two state machines fed by the same raw occupancy events from `coordinator_fallback_on_occupancy_change()`:

**Normal SM (sensor_bridge):**
- Always uses normal cooldown (`occupancy_cooldown_sec`)
- Always reports to Z2M via ZCL attributes
- Never switches to fallback cooldown
- Calls `coordinator_fallback_on_occupancy_change()` on every transition

**Fallback SM (coordinator_fallback):**
- Maintains `fallback_occupied` per EP with fallback cooldown timer
- `on_occupancy_change(ep, true)` → set `fallback_occupied=true`, cancel pending off-timer
- `on_occupancy_change(ep, false)` → start fallback cooldown timer; `fallback_occupied` stays true until timer fires
- When fallback triggers: dispatch On/Off via binding based on `fallback_occupied`
- When occupancy changes during active fallback: dispatch immediately based on `fallback_occupied`

## Who Has Control

| State | Light control | Z2M reports |
|-------|--------------|-------------|
| Normal | HA via Z2M | sensor_bridge, normal cooldown |
| Soft fallback | Fallback SM via binding | sensor_bridge continues (may not reach coordinator) |
| Hard fallback | Fallback SM via binding | sensor_bridge continues (unreachable) |

## Reconciliation

One-way: fallback → normal on clear. Normal never writes to fallback.

**Fallback activates:** Fallback SM dispatches based on its own `fallback_occupied` (tracked continuously). Normal SM is unaffected.

**Fallback clears (ACK during soft, or HA clears hard):**
1. Cancel all fallback cooldown timers
2. Push all EP `fallback_occupied` states to Z2M attributes (force report)
3. Normal SM takes over immediately — evaluates real sensor state against normal cooldown on next poll cycle (100ms)

The pushed state is a starting point. Normal mode corrects it naturally. Example: fallback pushes occ=1, real sensor says occ=0, normal cooldown=0s → next poll reports occ=0. Normal cooldown=1s → waits 1s then reports occ=0.

## Example Trace

Settings: normal_cooldown=0s, fallback_cooldown=60s, soft_timeout=2s

| Time | Event | Normal SM (Z2M) | Fallback SM |
|------|-------|-----------------|-------------|
| t=0 | occ=1 | Reports occ=1, probe sent | fallback_occupied=1 |
| t=0.5s | occ=0 | Reports occ=0 (cooldown=0s) | Starts 60s timer, fallback_occupied stays 1 |
| t=2s | No ACK → soft | Z2M shows 0 | fallback_occupied=1 → sends On via binding |
| t=3.5s | ACK arrives | — | Clears soft. Cancels timers. Pushes occ=1 to Z2M. |
| t=3.5s+ | Normal resumes | Real sensor occ=0, cooldown=0s → reports occ=0 on next poll | Inactive |

## Changes Required

### coordinator_fallback.c

1. **Add to struct:** `fallback_occupied` bool per EP, plus timer fields for fallback cooldown
2. **Fallback cooldown timer callback:** When timer fires, set `fallback_occupied=false`. If in active fallback, send Off via binding.
3. **`on_occupancy_change` updates fallback SM:** Always update `fallback_occupied` and manage fallback cooldown timer, regardless of whether fallback is active.
4. **Dispatch logic:** When fallback triggers or occupancy changes during fallback, use `fallback_occupied` (not the raw `occupied` param) for On/Off decisions.
5. **On fallback clear:** Cancel all fallback cooldown timers, push `fallback_occupied` states to Z2M attributes with force report, reset fallback state.
6. **On ACK clearing soft:** Same as above — push states, cancel timers.

### sensor_bridge.c

7. **Remove fallback cooldown switching:** Remove the `coordinator_fallback_is_active() || coordinator_fallback_ep_session_active()` ternary. Always use `cfg.occupancy_cooldown_sec[ep]`.

### coordinator_fallback.h

8. **Revert `ep_session_active`:** Back to only checking `fallback_session_active` (or remove entirely if sensor_bridge no longer needs it).

### Previous changes retained

- `fallback_light_on` removal: already done, stays removed
- `fallback_off_cb` removal: replaced by new fallback cooldown timer callback
- Simplified hard/soft dispatch: stays, but uses `fallback_occupied` instead of raw param
- Soft fallback dispatch in `on_occupancy_change`: stays, uses `fallback_occupied`
- Z2M label renames: already committed, stays
- Header comment updates: already committed, may need minor update
