# Voxel game UI direction

User clarified this is a voxel-world game and requested better UI.
Subsequent controls/scale/rows refinements supersede details below: see hotbar-icons-rows.md. No commits/pushes.

Implement world-first HUD: crosshair, nine empty selectable slots (no inventory
contents invented), low-key wordmark, compact slash hint. Opening commands hides
crosshair/hotbar and shows a content-width popup above a floating input bar,
with moss highlight, cream/muted text, borders, selection count, scrollbar, hints.
Feedback uses separate translucent backplates and fades while console is closed.

CPU draw list and HUD layout separated from Vulkan resource/node code. HUD update
is called after input polling, before rendering. Keys 1-9 and wheel select empty
slots only when UI does not capture input. Font baked at 28px and displayed at
half scale with nearest sampling for Departure Mono's pixel grid. The font's
hhea span is 700 units. Pixel alignment and nearest sampling were checked in
the layout preview.

Validation: all five Release CTests passed, including GPU presentation. Added
GPU-independent layout tests for 1280x720, 800x600, 320x200, and 64x64, checking
finite/clipped vertices, buffer exhaustion, real font baking, hotbar wraparound,
fractional wheel accumulation, and console capture. UI and console tests passed
with AddressSanitizer/UBSan. Two previews rasterized from the actual draw-list
vertices/font atlas were inspected (HUD and open console); they are layout previews,
not GPU screenshots. bin/axiovox remains Release; no alternate game copies created.
git diff --check passed. No voxel world/inventory simulation implemented.
