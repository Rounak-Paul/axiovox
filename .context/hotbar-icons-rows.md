# Minimal HUD controls and hotbar rows

User requests removing general key-help text, bottom-left slash/pause icons only,
smaller hotbar, integer-configured size/row count, and Tab cycling hotbar rows when
not editing console. No commits or pushes.

User clarified: no independent hotbar size setting. Use five global UI scale
levels plus configurable row count. ui.scale=1..5 maps to 0.75/1/1.25/1.5/2x;
default 2. ui.hotbar.rows=1..9, default 3. Fixed compact nine-slot active row. Implementation:
remove help captions and wordmark; add pointer transitions and consumed-key state
so console Escape cannot also trigger pause. Pause changes emit an event for
future world simulation, while rendering remains active.

- Five-level global transform is applied in Vulkan UI canvas/push constants and
  inversely in pointer hit testing. No independent hotbar size setting was added.
- Only active hotbar row shown; nine slots at 26 logical px, small row markers,
  per-row selections retained. Tab/Shift+Tab cycle rows only outside UI capture.
- Icon hover/clicks use raw pointer transitions. Slash toggles console, pause toggles
  pause state and emits AX_EVENT_PAUSE_CHANGED. Escape handled by console is consumed
  so closing console cannot accidentally also pause. No game simulation exists yet.
- Removed wordmark, COMMANDS caption beside icon, hotbar numbers, all key-help text.
  Popup retains its command-list title/count; pause shows a status label when active.
- On narrow scaled viewports, hotbar shifts above icon row to avoid overlap.
- Config loops use AX_CONFIG_SETTING_COUNT rather than repeated literal counts.

Validation: five Release CTests passed. Core/console/UI tests also passed with
AddressSanitizer/UBSan. Coverage includes config persistence/bounds, row cycling
and selection retention, pause vs console Escape consumption, clickable icons,
all five scale hit-test transforms, nine rows, clipped vertices at four viewport
sizes, and vertex-capacity exhaustion. GPU smoke also passed with ui.scale=5 and
ui.hotbar.rows=9. bin/axiovox is the only game executable. git diff --check passed.
Windows/Linux execution and native mouse/keyboard UI interaction remain unverified.

Full pause/settings UI and preferences now supersede the simple pause status: see pause-settings-menu.md.
