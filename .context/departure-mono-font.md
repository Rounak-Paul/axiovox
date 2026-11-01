# Departure Mono Nerd Font Mono

User chose downloaded Departure Mono Nerd Font Mono as the game's font.

- Source: /Users/duke/Downloads/DepartureMono/DepartureMonoNerdFontMono-Regular.otf.
  Copied unchanged into assets/fonts/. Nerd Fonts 3.4.0, Departure Mono 1.422 per
  accompanying download README. Chose explicitly monospaced Mono variant.
- Replaced Roboto asset/build dependencies and font license with accompanying
  DepartureMono/LICENSE. Removed obsolete Roboto/Apache font-only files.
- Font remains embedded by CMake; staging puts the original OTF/license in bin/assets.
  Existing glyph coverage and stb_truetype rendering remain. Updated pixel-grid
  metrics/sampling and layout are documented in voxel-game-ui.md.
- Updated public font comment and active README/third-party notices. Older context
  records Roboto historically; this file is the current font choice.
- No commits or pushes.

Validation: Release build and all four CTests passed, including real GPU atlas/UI
smoke test. Download, source, and staged font/license bytes match exactly. Old Roboto
font and font-only Apache license are absent from bin/. git diff --check passed.
