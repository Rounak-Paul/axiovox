# Slash console input fix

User reported pressing / did nothing, then requested removal of other executable
copies. Do not commit/push.

- Removed stale game executables from build/, build-vendored/, build-sanitize/;
  only project-root bin/axiovox is retained. Temporary unlaunched UI test bundle
  created during investigation removed as requested. Current CMake cannot regenerate
  the old game paths; its executable output is bin/ for every configuration.
- Console previously opened only on a Unicode text event; physical AX_KEY_SLASH
  had no open action. Added unmodified non-repeat slash key binding, retaining
  text-input fallback for keyboard layouts producing slash through another key.
- Suppresses the opening key's duplicate slash text only; release/new key clears
  suppression so subsequent characters are preserved. Normal editor behavior unchanged.
- Regression covers physical slash with and without char callback, duplicate text,
  modifier shortcut exclusion, and existing text-only path.
- Previous unit tests covered only text events; smoke test programmatically opened
  the console. Neither established physical key binding correctness.

Release rebuild and all four CTests passed, including new slash regression coverage.
Executable inventory confirms bin/axiovox is the only game binary in the project.
git diff --check passed. Native OS keyboard interaction not independently verified.
