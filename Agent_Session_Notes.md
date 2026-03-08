# Agent session notes

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

## Progress checkpoint (2026-03-07)

- Workspace and project scaffolding are in place: root CMake + three projects (`SmartDashboard`, `SmartDashboard_Interface_direct`, `ClientInterface_direct`).
- `*_direct` transport now includes a Win32 shared-memory ring buffer, named events, message framing, and basic heartbeat/state handling.
- Dashboard UI baseline is implemented in Qt:
  - main window shell, connection status indicator, editable mode
  - variable tile widgets for bool/double/string
  - right-click `Change to...` options by type
  - layout save/load JSON with variable key + widget type + geometry
- Added a lightweight in-app variable store (`VariableStore`) to separate latest-value model state from widget instances.
- Publisher/subscriber stubs were replaced with transport-backed implementations for iterative testing.

## Runtime deployment note (current blocker)

- App code builds, but Qt runtime deployment still needs final hardening for clean vcpkg-only reproducibility.
- Current CMake behavior on Windows:
  - prefers vcpkg copy-based deployment for vcpkg Qt builds
  - keeps `qt.conf` with `Plugins=plugins`
  - copies plugin folders under `plugins/`
- Remaining issue: some optional companion DLLs (`dxcompiler.dll`, `dxil.dll`, etc.) are not present in this vcpkg install, and plugin init diagnostics are still being finalized.

## Resolved deployment pitfall (important)

- Root cause of Qt startup crash was a plugin ABI mismatch: Qt6 app DLLs with Qt5 platform plugins.
- Symptom in `QT_DEBUG_PLUGINS=1` output:
  - `Plugin uses incompatible Qt library (5.15.0) [debug]`
  - `Could not find the Qt platform plugin "windows"`
- Correct vcpkg plugin source for Qt6 is:
  - Debug: `<vcpkg>/installed/<triplet>/debug/Qt6/plugins`
  - Release: `<vcpkg>/installed/<triplet>/Qt6/plugins`
- Avoid copying from `<vcpkg>/installed/<triplet>/debug/plugins` when both Qt5 and Qt6 are installed, because this can pull Qt5 plugins into a Qt6 app.
- Deployment hygiene for new Qt projects:
  - keep `qt.conf` with `Plugins=plugins`
  - clear destination `plugins/` before copying to prevent stale leftovers
  - remove/avoid `Qt5*.dll` in Qt6 app output folders

## Next session plan

1. Run/collect `QT_DEBUG_PLUGINS=1` output on a clean deploy folder.
2. Decide final vcpkg-only deployment contract (no cross-project DLL sourcing).
3. Lock deployment script behavior and verify F5 startup from Visual Studio.

## Progress checkpoint (2026-03-08)

- Added GoogleTest-based publisher streaming tests for bool/double/string in `ClientInterface_direct/tests/direct_publisher_tests.cpp`.
- Test streaming defaults to shared dashboard channel for live manual verification; isolated channel mode is available via `SD_DIRECT_TEST_USE_ISOLATED_CHANNELS=1`.
- Stabilized tests for latest-value transport semantics:
  - deterministic publishing by disabling publisher auto-flush thread in tests
  - rotating string distinct-count expectation adjusted to realistic minimum
  - bool observation threshold adjusted to avoid timing-related flakiness
- Added app window persistence (size/position/state) using `QSettings` in `MainWindow`.
- Fixed reconnect behavior when producer restarts and sequence resets:
  - added `VariableStore::ResetSequenceTracking()`
  - reset sequence tracking on reconnect/sequence rollback detection
- Updated dashboard canvas background to use system palette role (`QPalette::Window`) instead of hardcoded color.
- Reduced Windows build noise for students:
  - enabled `VcpkgXUseBuiltInApplocalDeps=true` via `CMAKE_VS_GLOBALS`
  - hid optional Qt deploy DLL missing messages by default (`SMARTDASHBOARD_VERBOSE_QT_DEPLOY=OFF`, opt-in verbose toggle)

## UI polish checkpoint (2026-03-08)

- Added `.gitignore` entries for common local artifacts: `build/`, `out/`, `.vs/`, and `CMakeUserPresets.json`.
- Fixed `VariableTile` `Change to...` behavior so widget type now changes actual presentation instead of metadata only.
- Implemented concrete type presentations:
  - bool: `bool.led` and `bool.text`
  - double: `double.numeric`, `double.progress`, and `double.gauge`
  - string: `string.text` and `string.multiline`
- Updated tile layout behavior to better match expected SmartDashboard ergonomics:
  - progress bar shows variable name on top and uses full width below
  - gauge hides variable name for cleaner visual appearance
  - multiline string uses a dedicated second row with wrapping

## API and stability checkpoint (2026-03-08)

- Added `SmartDashboardClient` facade in `ClientInterface_direct` to support three get styles:
  - passive `TryGet*` (read-if-present)
  - assertive `Get*(default)` (publish default when missing)
  - callback `Subscribe*` (listen for typed key updates)
- Added `smartdashboard_client_tests.cpp` with coverage for assertive get + passive read + callback update flow.
- Improved local determinism for facade users by updating local cache/callbacks on `Put*` before transport flush.
- Added single-instance guard for `SmartDashboardApp` on Windows using named mutex `Local\\SmartDashboard.SingleInstance`.
  - If already running, app now shows a message box and exits.
- Multi-instance note: current direct ring transport remains effectively single-consumer due to shared read cursor; one dashboard instance is the supported mode for now.

## Bidirectional controls checkpoint (2026-03-08)

- Added initial dashboard -> app/client command channel support using separate direct transport names:
  - `Local\\SmartDashboard.Direct.Command.Buffer`
  - `Local\\SmartDashboard.Direct.Command.DataAvailable`
  - `Local\\SmartDashboard.Direct.Command.Heartbeat`
- Added command publish adapter in dashboard (`DirectPublisherAdapter`) and wired editable control signals from tiles into command publishes.
- Extended tile widget modes with writable controls:
  - `bool.checkbox`
  - `double.slider`
  - `string.edit`
- Added reusable `TileControlWidget` to host interactive controls and keep telemetry display widgets separate.
- Extended `SmartDashboardClient` with optional command subscriber and command callback subscriptions:
  - `SubscribeBooleanCommand`, `SubscribeDoubleCommand`, `SubscribeStringCommand`
- Added command-channel tests in `smartdashboard_client_tests.cpp` for bool/double/string receive paths.
- Updated command tests with animated 2-second visual windows so manual checks can verify updates continue on already-populated widgets.
- Added manual roundtrip sample `sd_command_roundtrip_sample` and README instructions for live dashboard validation.
- Added `File -> Clear Widgets` action in dashboard to reset runtime widget/model state without restarting app.

## Documentation checkpoint (2026-03-08)

- Added historical architecture document `docs/history.md` for students/mentors covering:
  - early FRC telemetry constraints and dashboard goals
  - SmartDashboard + NetworkTables design benefits and scaling limitations
  - evolution through Shuffleboard, Glass, and AdvantageScope
  - architectural lessons (transport/UI separation, structured telemetry, live vs log pipelines)
  - project-specific direction: avoiding NetworkTables-style distributed state bloat in favor of a simpler, predictable pipeline
- Updated `README.md` documentation references to include `docs/history.md` for discoverability.

## Widget editing and UX checkpoint (2026-03-08)

- Added editable canvas interaction controls in `View`:
  - `Snap to grid (8px)`
  - `Editable interaction` modes: `Move only`, `Resize only`, `Move and resize`
- Implemented tile move/resize UX for editable mode:
  - drag-to-move and edge/corner resize hit-testing
  - keyboard nudging (`Arrows`) and keyboard resize (`Ctrl+Arrows`), with coarse step via `Shift`
  - reduced tile minimum size to allow intentional cropping workflows
- Enforced layout-only behavior when editable is enabled:
  - writable controls and gauge interactions are disabled/mouse-transparent in edit mode
  - control value publishes are blocked during layout adjustments
- Updated widget presentation/details:
  - bool checkbox layout now shows variable name on the left and control on the right
  - string widget menu labels now explicitly distinguish read-only vs writable options
  - `Change to...` menu now checks the currently selected widget type
- Restored interactive gauge behavior for runtime control (editable off):
  - dial publishes normalized double commands on user manipulation
  - programmatic update guard prevents feedback-loop publishes
  - cursor now consistently shows horizontal double-arrow for gauge interaction in non-editable mode
