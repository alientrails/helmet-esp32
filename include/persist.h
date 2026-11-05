#pragma once

// Saves and restores every LED setting across reboots: mode, brightness,
// on/off, colour, speed, the strip geometry and the Bass tuning. Changes are
// noticed by polling, and written once they have held still for 600 ms, so
// dragging a slider costs one flash write rather than one per step.

namespace Persist {
void begin();
void markDirty();
void loop();
}  // namespace Persist
