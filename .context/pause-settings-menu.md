# Pause and settings menus

## Scope / completion

User requested a full pause menu with game name and settings resembling normal
voxel/factory games. Implemented. Prior preference preserved: no generic keyboard
instruction captions. No commits, pushes, or index changes.

Device/worker live changes now supersede restart-only handling below: see live-device-workers.md.

## Architecture

- `ui/menu.*`: GPU-independent menu state, hit tests, keyboard navigation, clipped
  layouts, isolated config draft. HUD owns pause and emits pause transitions;
  Vulkan UI node draws the same CPU canvas. Pause: AXIOVOX, Resume, Settings, Quit.
- Settings categories: Interface (scale/rows), Video (window resolution, compatible
  real GPU catalog), System (workers/logging). Only implemented settings exposed.
  Apply/Done persist+commit; Cancel discards; Reset all edits the draft. Numeric
  steppers and bounded choices share existing config validation. No fake audio,
  input rebinding, fullscreen, or world-save controls.
- Menu actions use release-inside semantics; drag outside cancels. Arrow/Tab focus,
  Enter/Space activation, Escape back/resume. OS repeats supported for arrows.
  Menu content scrolls at small logical sizes and uses explicit canvas clipping.
  Large headings use scaled font rendering; Departure Mono and palette retained.
- Input consumption prevents console Enter/Escape from also activating pause menu.
  Focus loss cancels mouse press activation. Opening console suspends menu click
  state; closing it retains pause/draft. Bottom-left HUD icons hidden under menus.
- `core/config.*`: staged batch apply validates before committing, then notifies
  observers after all values are visible. Restart status compares desired settings
  to startup-applied values, rather than remaining sticky after reverting.
  Untouched draft fields merge changes made through runtime commands while open.
- `core/files.*`: UTF-8 file paths (wide Windows I/O), unique sibling temp files,
  flush + fsync/_commit + atomic replacement. Failed writes leave destination intact.
  Per-user config path creates the preferences directory. Menu apply persists before
  changing live state; save failure retains draft and displays an error.
- Main autoloads per-user settings before services, binds menus after UI creation,
  marks applied service values, then runs CLI commands. Last explicit --config path
  overrides the menu save path. Defaults: macOS Application Support/Axiovox,
  Windows LOCALAPPDATA/Axiovox, Linux XDG_CONFIG_HOME/axiovox or ~/.config/axiovox.
  No mutable settings placed into shipped bin/.
- Renderer retains a pure-C AxDeviceInfo catalog (including compatibility) until
  shutdown; menu borrows this for valid device selection. All settings/main UI
  callbacks remain on the main thread.

## Validation

- Release build clean; six CTests passed (core, console, UI, menu, renderer,
  presentation). GPU smoke traverses console, pause, interface, and video settings.
- Core/console/UI/menu tests passed under AddressSanitizer + UBSan.
- New menu regression tests cover release-inside/cancel, pending drafts, apply/save,
  reload, partial-write failure preserving old file, defaults/cancel, compatible GPU
  filtering, restart reversion, resolution pairs, external command merges, consumed
  Enter, focus-loss click cancellation, and quit via direct command dispatch.
- UI bounds tests cover pause and all settings categories at all five scale levels
  and four viewport sizes, including clipped tiny layouts and buffer backpressure.
- CPU previews from actual draw lists/font atlas inspected for pause and settings;
  these are layout previews, not native GPU screenshots. Default settings sheet
  subsequently tightened from 496 to 360 logical px to reduce empty space.
- Native mouse/keyboard interaction and Windows/Linux execution not independently
  verified. No actual voxel-world simulation exists yet; pause event remains its hook.
