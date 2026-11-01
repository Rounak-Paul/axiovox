# Console command suggestions

User wants normal command-console UX: / immediately lists commands, typing filters,
arrow keys select, completion accepts names before values. No commits/pushes.

Implementation: sorted matches belong to console state, UI only renders them.
Up/Down wrap selection while matches are visible. Alt+Up/Down recalls history;
plain arrows recall history when editing values/no matches. Tab accepts selected
name, Shift+Tab selects previous. Enter accepts a partial/selected name without
executing; exact names or commands with values execute. Escape closes. Completion
preserves existing argument text when editing a command name. UI list scrolls to
keep the selected row visible and shows current keyboard hints.

Validation: Release rebuild and all four CTests passed. Console regressions cover
initial/sorted matches, live filtering, wraparound navigation, Enter selection vs
execution, Tab/Shift+Tab, no-match recovery, history, and preserving values during
completion. Focused console tests also passed with AddressSanitizer/UBSan.
UI was exercised by the eight-frame presentation smoke test; native keyboard/visual
interaction was not independently verified. bin/axiovox remains the sole game binary.
git diff --check passed.
