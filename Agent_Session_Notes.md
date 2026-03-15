# Agent session notes

- Edit this file for short, high-signal context that helps the next session start quickly.
- Keep this file lean; move long milestone history to `docs/project_history.md`.
- Commit-note convention: when the user says "update notes", keep this file to handoff-critical context only; put durable feature/change history in `docs/project_history.md`.
- `docs/project_history.md` ordering rule: keep milestone sections in descending chronological order (newest first) so current changes are always at the top.

## Workflow note

- `apply_patch` expects workspace-relative paths (forward slashes). Avoid absolute Windows paths to prevent separator errors.
- Code style uses ANSI/Allman indentation; keep brace/indent alignment consistent with existing blocks to avoid drift.
- Use Windows CRLF line endings for C++ source files in this repo.

## Documentation and teaching comments rule

- Treat this codebase as both production code and a learning reference.
- Add concise, high-value comments in `.cpp` files when logic is non-trivial (timing behavior, concurrency, transport semantics, state handling, etc.).
- For advanced algorithms/patterns, include the concept name directly in comments where implemented (for example: ring buffer, round-robin, coalescing/latest-value cache, debounce, backoff).
- Keep comments practical and instructional: explain *why* a pattern is used and what trade-off it makes, not just what the line does.
- Avoid noisy comments on obvious code paths; focus comments on places likely to confuse first-time readers.

## Design docs

- Primary design document: `design/SmartDashboard_Design.md`
- Durable milestone history: `docs/project_history.md`
- Replay operator reference: `docs/replay_user_manual.md`
- Replay status/roadmap reference: `docs/replay_parity_roadmap.md`

## Quick context for next session

- Repository baseline is local `main`; `feature/replay-dockable-workspace` has been merged.
- Core dashboard architecture is stable: transport-agnostic main window (`Direct`, `NetworkTables`, `Replay`) + `VariableStore` + Qt widget tiles.
- Editable mode is layout-only; non-editable mode restores live writable controls.
- Layout workflows are in place: file-dialog save/load, dirty tracking, startup apply, and close prompt.
- Direct transport includes retained latest-value fallback for cross-run config/state retrieval.
- Replay stack is broadly in place on `main`:
  - recording to newline-delimited JSON events under `logs/session_<timestamp>.json`
  - replay load path accepts both event-stream and capture-session JSON shapes
  - timeline scrub/zoom/pan, adaptive tick labels, cursor/window readouts, marker jumps, marker dock, keyboard stepping, bookmarks, anomaly markers, and visible-window marker summary
  - dockable `Replay Controls`, `Replay Timeline`, and `Replay Markers` panels with persisted visibility and `Reset Replay Layout`
- `docs/project_history.md` is the authoritative milestone log; keep this file to current-state handoff only.

## Known constraints / active considerations

- Current direct ring transport is effectively single-consumer due to shared read cursor.
- Deployment remains vcpkg/Qt-DLL based; static Qt distribution is not a current goal.
- Event-bus decoupling (topic subscriptions + rate limiting + coalescing) is documented as future work, not implemented.
- If startup false-dirty (`*`) behavior regresses, add a focused startup regression test that validates initial title/dirty state before any editable interaction.

## Next-session checklist

1. Keep `Agent_Session_Notes.md` lean; record durable milestones in `docs/project_history.md`.
2. If replay docs change, keep `docs/replay_parity_roadmap.md`, `docs/replay_user_manual.md`, and `docs/project_history.md` in sync.
3. Pick one focused roadmap item from `README.md` and `docs/requirements.md`.
4. Define acceptance criteria first, then implement in a small slice.
5. Run automated tests (`docs/testing.md`) plus one targeted manual validation loop.
6. Record durable milestone details in `docs/project_history.md`.
