#pragma once

// Saves/restores LED mode, brightness, on/off, and color across reboots.
// Writes are debounced so rapid UI changes don't hammer flash.

namespace Persist {
void begin();
void markDirty();
void loop();
}  // namespace Persist
