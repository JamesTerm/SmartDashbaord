# Project History

Curated milestone history for this repository.

- Edit this file for durable project milestones and outcomes.
- Keep lean handoff context in `Agent_Session_Notes.md`.

## 2026-03-10 - Legacy XML import parity and widget property refinements

- Added File menu action `Import Legacy XML...` to load legacy SmartDashboard `.xml` layouts directly into the current dashboard session.
- Implemented legacy XML parser and widget mapping for common classes:
  - `SimpleDial` -> `double.gauge`
  - `ProgressBar` -> `double.progress`
  - `CheckBox` -> `bool.checkbox`
  - `BooleanBox` -> `bool.led`
  - `TextBox` / `FormattedField` -> text/numeric mappings based on type
- Added import issue reporting that appears only when unsupported classes/properties are encountered.
- Extended legacy-property handling and parity:
  - progress bar `Foreground` / `Background` color import support
  - `Font Size` import support for text-oriented widgets
  - removed false-positive import warnings for now-supported progress bar colors and font size
- Refined bool checkbox presentation:
  - default hide label behavior
  - added checkbox `Show Label` property toggle
  - compact checkbox tile width when label is hidden
- Refined bool LED behavior:
  - default off-state now renders visibly (no blank appearance before first update)
- Expanded progress bar behavior and properties:
  - default hide percentage text
  - added `Show Percentage` property toggle
  - added `Foreground` and `Background` color properties with color picker dialogs
  - applied custom `QProgressBar` styling so fill/chunk renders with configured colors and visible minimum thickness
  - reduced label-to-bar vertical spacing to match legacy compact look and allow bar area to expand with tile height
- Added text font-size configurability in properties for:
  - numeric text widgets (`double.numeric`, including editable mode)
  - non-edit text widgets (`bool.text`, `string.text`, `string.multiline`)
  - editable text widget (`string.edit`)
- Extended layout persistence/load-apply for new properties:
  - `progressBarShowPercentage`
  - `progressBarForegroundColor`
  - `progressBarBackgroundColor`
  - `boolCheckboxShowLabel`
  - `textFontPointSize`

## 2026-03-09 - Gauge editing workflow and layout persistence

- Finalized line-plot axis behavior updates:
  - x-axis tick labels are now tied to fixed time values and scroll left with advancing time (no frame-to-frame relabel jitter)
  - reset graph state now preserves visible default `0..1` x/y axes so number lines and grid lines remain visible with no samples
- Added gauge-style properties for non-gauge numeric widgets:
  - `double.progress`: `Upper Limit`, `Lower Limit`, `Tick Interval`, `Show Tick Marks`
  - `double.slider`: `Upper Limit`, `Lower Limit`, `Tick Interval`, `Show Tick Marks`
- Extended slider/control behavior to respect configured slider range for both UI->command mapping and value->UI normalization.
- Added layout save/load coverage for new properties:
  - `progressBarLowerLimit`
  - `progressBarUpperLimit`
  - `sliderLowerLimit`
  - `sliderUpperLimit`
  - `sliderTickInterval`
  - `sliderShowTickMarks`
- Clarified `double.progress` capabilities: Qt `QProgressBar` does not support rendered tick marks, so progress-bar properties are now limited to upper/lower range bounds.

- Refined `double.lineplot` behavior and readability:
  - manual Y-axis bounds now clip out-of-range sample rendering
  - added optional axis rendering controls:
    - `linePlotShowNumberLines`
    - `linePlotShowGridLines`
  - improved tick generation to endpoint-inclusive, pixel-driven spacing on both axes when number lines are enabled
  - improved tick label formatting with adaptive decimal precision (tenths by default, hundredths when needed)
  - aligned grid-line positions with rendered tick positions
  - improved Y auto-range responsiveness for smaller buffer sizes
- Updated `StreamsSineWaveDouble` timing semantics so `SweepSeconds` edits apply on the active run.
- Added direct retained-table semantics to `SmartDashboardClient` for authoritative latest-value ownership in direct mode:
  - shared-memory retained store with named mutex for cross-process coordination
  - optional retained file persistence path (default under `%LOCALAPPDATA%`)
  - `TryGet/Get` now fall back to retained store when local cache misses
  - local `Put*` writes retained entries with sequence/timestamp metadata
- Added automated restart regression coverage:
  - `SmartDashboardClientTests.RetainedStoreRestoresValuesAcrossClientRestart`
  - validates cross-client restart recovery for bool/double/string values
- Added new `double.lineplot` widget type with custom Qt paint rendering and rolling sample buffer.
- Implemented line-plot behavior for telemetry visualization:
  - starts with normalized `[0..1]` style axis baseline
  - x-axis head tracks elapsed time and slides once sample buffer is full
  - y-axis auto-expands to observed min/max including negatives
- Added line-plot properties and persistence:
  - `linePlotBufferSizeSamples` (default `5000`)
  - `linePlotAutoYAxis`
  - `linePlotYLowerLimit`
  - `linePlotYUpperLimit`
- Added line-plot non-edit mode context menu action: `Reset Graph`.
- Added numeric double widget property `Editable` with direct text-entry command publishing.
- Added value persistence for bool/double/string tile values in layout JSON.
- Expanded layout save/load UX and lifecycle behavior:
  - save/load now uses file chooser dialogs (`.json`)
  - remembers last-used layout path in app settings
  - tracks dirty layout changes and prompts on close (`Yes/No/Cancel`)
  - load now applies to existing tiles and creates missing saved tiles at startup/load
  - load behavior was adjusted to merge/apply (clear remains explicit action via menu)
- Updated `StreamsSineWaveDouble` test flow for iterative UI tuning:
  - publishes config keys for amplitude min/max and sweep seconds
  - supports command-path updates while streaming
  - keeps stress/repopulate workflow stable under repeated runs

- Expanded editable tile context menu behavior (editable-only):
  - `Change to...`
  - `Properties...`
  - `Send To Back`
  - `Reset Size` (disabled when already at default)
  - `Remove`
- Added removable tile workflow by wiring tile-level remove action to `MainWindow` tile map/layout state cleanup.
- Added gauge-specific properties dialog and behavior:
  - upper/lower limit
  - tick interval
  - show tick marks toggle
- Updated gauge normalization and publish mapping to use configured lower/upper limits instead of fixed `-1..1`.
- Added layout persistence for gauge properties in serializer/load path:
  - `gaugeLowerLimit`
  - `gaugeUpperLimit`
  - `gaugeTickInterval`
  - `gaugeShowTickMarks`
- Refined editable visual affordances:
  - no always-on edit chrome; outline appears only when hovering active tile
  - kept cursor-based directional resize/move hints
  - active drag/resize outline brightens and thickens for immediate feedback
  - gauge no longer appears disabled/inverted in editable mode while still blocking value manipulation

## 2026-03-08 - Reliability, UX, tests, and docs maturation

- Added automated GoogleTest coverage for direct publisher streaming and SmartDashboard client facade behavior.
- Stabilized latest-value semantics and reconnect handling (sequence reset tracking and rollback recovery).
- Implemented bidirectional command channel support and writable controls:
  - `bool.checkbox`
  - `double.slider`
  - `string.edit`
- Added command roundtrip sample and documented manual validation loop.
- Added single-instance app guard and clear-widgets action for iterative testing.
- Expanded widget presentation options and improved layout ergonomics.
- Added editable canvas move/resize workflows with snap-to-grid and interaction mode controls.
- Enforced layout-only safety while editable is on (no value writes from controls).
- Restored runtime interaction in non-editable mode, including gauge command writes and interaction cursor hints.
- Added educational and process documentation:
  - `docs/history.md`
  - `docs/requirements.md`
  - `docs/testing.md`
  - `docs/development_workflow.md`
  - `docs/ai_development_guidelines.md`
- Clarified README structure with architecture overview, status, development approach, and roadmap.

## 2026-03-07 - Baseline architecture and app scaffold

- Established root CMake workspace with three projects:
  - `SmartDashboard`
  - `SmartDashboard_Interface_direct`
  - `ClientInterface_direct`
- Implemented direct transport foundation with shared-memory ring buffer, events, framing, and heartbeat/state handling.
- Built initial Qt dashboard shell with variable tiles, type-based widget switching, and JSON layout save/load.
- Added in-app `VariableStore` to separate latest-value model state from widget instances.

## Forward-looking considerations (documented, not implemented)

- Optional NetworkTables adapter via transport interface extension.
- Optional telemetry event bus between ingestion and UI with:
  - topic-based subscriptions
  - rate-limited delivery and coalescing
  - latest-value cache bootstrap for new subscribers
  - explicit non-UI-thread processing with safe Qt-thread handoff
- Optional telemetry recording/playback pipeline for analysis and debugging:
  - recorder subscribes to telemetry events and writes timestamp/topic/value logs
  - playback engine re-injects recorded events through the same event path as live data
  - timeline model tracks playback position, duration, and speed
  - future UI controls can bind to timeline state (play/pause/scrub) without changing widget data semantics

## Retained store follow-up checklist (short)

- Define conflict policy for retained writes when multiple producers publish the same key (source priority vs seq/timestamp precedence).
- Specify retained-schema behavior for type changes on existing keys (reject, migrate, or last-write-wins with type replacement).
- Add explicit retained GC/limits policy (max entries, eviction strategy, and diagnostics when evictions occur).
- Add transport-parity contract tests so direct/NT/TCP adapters share the same `TryGet/Get` retained semantics.
- Clarify stream fan-out roadmap: retained latest-value ownership is shared now, but ring payload consumption is still single-consumer.
