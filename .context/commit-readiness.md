# Commit readiness

User requested a readiness/cleanup check; no commit, push, or index changes made.

- Release build and all four CTests passed again on macOS.
- Both submodule working trees are clean; gitlinks already staged by the user.
- bin/ and build directories are ignored. Source/assets/tests/context are mostly
  untracked and must be included when the user stages the commit.
- Removed local-machine paths from distributed font/stb provenance README files.
- Staged/unstaged whitespace checks passed; first-party untracked source whitespace
  inspected separately because git diff does not cover untracked files.
- Windows/Linux execution remains unverified; this is local commit readiness,
  not cross-platform release certification.
