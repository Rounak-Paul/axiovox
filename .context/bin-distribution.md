# Bin distribution

## Scope

All game output must be shippable by copying project-root bin/ alone, with local
assets. Tests/intermediates remain in build trees. No commits/pushes.

## Implementation

- Root bin/ output, no multi-config subdirectories; only game executable is redirected.
- Dedicated build dependency restages assets/licenses on every game build, including
  when no code relinks. Generated asset/license subtrees are replaced to remove stale files.
- macOS directly links bundled MoltenVK, with @executable_path/lib runtime search.
  GLFW receives the linked vkGetInstanceProcAddr explicitly so it cannot select a
  different system Vulkan loader. No SDK loader or ICD manifest is needed on macOS.
  Direct MoltenVK does not expose loader-owned portability enumeration extension;
  that extension/flag is omitted for the direct backend.
- Linux bundles Vulkan loader under lib/ with $ORIGIN/lib; Windows stages vulkan-1.dll
  beside executable. MSVC runtime static; MinGW GCC runtime static. OS desktop
  libraries and installed GPU drivers remain prerequisites.
- Libraries discovered from SDK paths or explicit CMake cache overrides. Shader/font
  data remain embedded; full source assets plus licenses are included locally.
- Different build trees/configurations share bin/: last game build wins. No parallel
  game builds into the same output. Build Release before shipping.

## Validation

- macOS Debug and fresh Release configure/build passed, four CTests passed for each.
- bin/ currently contains the Release executable, assets, licenses, and local MoltenVK.
- Copied only bin/ into a temporary bundle, ran from / with all VK_/VULKAN_/DYLD_
  environment overrides removed. Eight-frame UI/render smoke passed. dyld logging
  confirmed the relocated local MoltenVK was loaded, with no /usr/local dependency.
- Asset addition and removal correctly restaged on target-only builds without
  changing executable mtime (no relink). Probe and temporary bundle cleaned up.
- git diff --check passed. Windows/Linux packaging branches unverified locally.
- macOS direct-MoltenVK distribution does not load Vulkan validation layers; earlier
  loader-based renderer validation is historical evidence, not this backend's check.
