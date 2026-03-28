# SmartDashboard Roadmap

Actionable future work for this repository.

- Items move here from `requirements.md` (evaluation/planning) or `Agent_Session_Notes.md` (in-progress).
- When an item is completed, move it to `docs/project_history.md` and remove it from this file.
- Cross-repo items that involve Robot_Simulation are marked with **(cross-repo)**.

## Need / Want / Dream

Use these buckets to keep roadmap discussions grounded in product identity.

- `Need`
  - adoption blockers, migration essentials, and daily-driver basics
  - if these are missing, teams will hesitate to switch or will bounce off quickly
- `Want`
  - high-value improvements and differentiators that strengthen the product once the baseline is dependable
  - these should improve real workflows, but are not allowed to destabilize the foundation
- `Dream`
  - interesting future ideas, larger specialty surfaces, or ambitious analysis/polish work
  - these stay intentionally deprioritized until the product is already trusted for everyday use

### Need

1. **Compatibility first**
   - Teams can leave robot code as-is, or very nearly as-is, when adopting this dashboard.
   - Existing `SmartDashboard`/`Shuffleboard`/`NetworkTables` publishing workflows are supported directly or through a clear adapter/translation layer.
   - Acceptance: a team can connect an existing robot project with little or no code churn and see expected values/widgets.

2. **Strong live dashboard baseline**
   - Reliable live telemetry, clear connection state, practical writable controls, and stable layout workflows.
   - Acceptance: teams can use the dashboard confidently during regular robot development and testing.

3. **NetworkTables interoperability and migration smoothness**
   - NetworkTables behavior should feel solid enough that teams do not see this dashboard as a special-case tool.
   - Acceptance: common FRC keys and update patterns behave as teams expect.
   - Architecture direction: legacy ecosystem compatibility should be packaged as optional per-ecosystem transport plugins so teams can keep robot code patterns while deploying only the bridge they need.
   - Current baseline: `Legacy NT` is the first real compatibility plugin and should remain the stable comparison oracle while broader Shuffleboard-oriented additions are layered carefully on top.

4. **High-value everyday widget coverage**
   - Prioritize the widgets teams most commonly need before adding niche analysis surfaces.
   - Include graph/plot support that satisfies normal day-to-day telemetry visibility needs.
   - Include `SendableChooser`-class support as a compatibility requirement.

5. **Dependable migration and operator workflow**
   - Layout/edit/save/load behavior, reconnect handling, and operator-controlled value survival should feel trustworthy.
   - Acceptance: a normal team can accomplish everyday dashboard tasks without first asking what is missing.

6. **Command/Subsystem status display**
   - Teams using command-based programming expect to see the Scheduler widget (running commands list), subsystem "required by" display, and command start/cancel buttons via `putData`.
   - This is a core SmartDashboard feature that command-based teams rely on for debugging and development.
   - Acceptance: `SmartDashboard.putData(CommandScheduler)`, `putData(subsystem)`, and `putData("name", command)` produce the expected interactive widgets.

7. **Test Mode / LiveWindow support**
   - SmartDashboard's Test Mode displays sensors and actuators grouped by subsystem with interactive sliders, plus PID tuning with live P/I/D parameter editing.
   - Teams use this for mechanism bring-up, finding setpoints, and tuning PID controllers without writing throwaway code.
   - Acceptance: entering Test Mode on the robot causes the dashboard to display LiveWindow sensor/actuator widgets with interactive controls.

### Want

1. **Replay as a differentiator**
   - Keep recording/replay and timeline analysis improving, but in service of practical team debugging rather than tool sprawl.
   - Acceptance: replay remains a clear reason to choose this dashboard over older baseline tools, not a separate product direction.

2. **Enhanced multi-trace plotting**
   - After basic graph compatibility is solid, add a stronger plot experience that can show multiple related signals together when that meaningfully improves debugging.
   - Acceptance: users can inspect several related signals in one plotting surface without losing readability.

3. **High-value operational additions**
   - Add practical features that directly support common team workflows once the foundation is stable.
   - Current likely candidates:
      - ~~camera stream support~~ (complete — all phases closed, MJPEG viewer dock with reticle overlay, auto-discovery, auto-reconnect)
     - lightweight alerts/notifications
     - a few more visual control variants where they materially improve migration comfort

### Dream

1. **Deeper analytics only when justified**
   - Add advanced analysis helpers only when they clearly help normal team workflows.
   - Acceptance: each addition saves real time during incident review instead of adding novelty.

2. **Dedicated high-performance telemetry panel**
   - A single panel widget with multiple traces/axes, shared timeline controls, and high-scale rendering — distinct from the per-tile multi-trace line plot.
   - Evaluate how this affects buffer ownership, decimation strategy, rendering batching, and UI layout workflow.
   - Only justified once the per-tile multi-trace line plot is proven in practice and teams need more.

3. **Broader specialty widget surfaces**
   - Explore richer FRC-specific views only after core adoption is healthy.
   - Examples:
     - field/mechanism-style views
     - command/subsystem-oriented panels
     - other specialty semantic widgets that are useful but not foundational

4. **Major UX polish layers**
   - Consider broader visual/design-system sophistication only after the product is already trusted for reliability and workflow fit.

---

## Foundation before enabling NetworkTables broadly

Treat this as the readiness gate before presenting NetworkTables support as a core team-facing feature.

### Must-have before broad NT rollout

- [x] Compatibility transport architecture stays optional and ecosystem-scoped (plugin ABI, host-rendered settings UI)
- [x] `SendableChooser`-style autonomous selection is supported
- [x] Layout/edit/save/load workflow feels dependable enough for daily use
- [x] Connection/reconnect/status behavior is trustworthy and unsurprising
- [x] Existing common scalar widgets feel complete for normal team use (bool indicators/text/control, numeric text/bar/slider/dial, string text/edit views)
- [ ] Common `SmartDashboard`/`Shuffleboard` publishing patterns are documented as: works unchanged / works through adapter / not yet supported
- [ ] Legacy compatibility baseline is explicit (preserve `legacy-smartdashboard-baseline` behavior profile for validation; allow `shuffleboard-additive` behaviors only when they do not break legacy baseline)
- [ ] Key migration policy is explicit for operator-controlled values (canonical scoped keys preferred, legacy flat aliases remain supported during migration)
- [x] Dashboard-owned control values replay/re-publish correctly across simulator reconnects in direct mode
- [ ] Command/Subsystem status display (`putData` for Scheduler, subsystems, and commands produces expected interactive widgets)
- [ ] Test Mode / LiveWindow support (sensor/actuator display grouped by subsystem, interactive sliders, PID tuning)

### High-priority near-foundation items

- [x] Graph/plot support that covers normal `Shuffleboard` expectations for numeric telemetry
- [x] Camera stream support for teams that rely on driver/diagnostic video in dashboard workflows (MJPEG MVP complete)
- [ ] Visual control variants where they materially improve migration comfort (`Toggle Button`, `Toggle Switch`, voltage-view-style presentation if needed)

### Not required to unblock NT rollout

- ~~Enhanced multi-trace plotting beyond normal `Shuffleboard` graph expectations~~ (active — see "Multi-trace line plot" section below)
- Compass widget
- Deep replay-analysis additions beyond the current practical workflow
- Broader specialty `Shuffleboard`/WPILib widgets such as `Field2d`, `Mechanism2d`, command/subsystem panels, or other advanced sendable surfaces

### Readiness question

If a typical FRC team points an existing robot project at this dashboard, can they accomplish their everyday dashboard tasks without first asking what is missing? If the honest answer is no, keep the focus on foundation work before advertising NT support broadly.

---

## Active: Abstract camera discovery from transport

Camera auto-discovery currently piggy-backs on the transport variable stream — `MainWindow` forwards every key update to `CameraPublisherDiscovery`, which filters for `/CameraPublisher/` keys. This only works when the active transport happens to deliver those keys as variable updates (NT4 does; Direct and Native Link do not).

Camera discovery is not part of the transport contract and should not be. A Direct-connected session should still be able to discover and connect to camera streams. The fix is to extract camera discovery into its own abstracted service that works independently of which transport plugin is active.

- [ ] Define an `ICameraDiscoverySource` interface (or equivalent) that camera discovery providers implement
- [ ] Move `CameraPublisherDiscovery` behind that interface as one concrete provider (NT4-style `/CameraPublisher/` key monitoring)
- [ ] Wire `CameraViewerDock` to consume the abstract interface instead of being fed by `MainWindow` variable forwarding
- [ ] Camera discovery works regardless of active transport plugin (Direct, Native Link, NT4, or none)

---

## Active: Multi-trace line plot

Extend the existing `LinePlotWidget` to support multiple named traces in a single tile. The current widget accepts a single `AddSample(double)` call and draws one trace. Teams commonly need to overlay related signals (e.g. setpoint vs. actual, left vs. right motor output) to compare behavior at a glance.

Architecture direction: option A (many independent lightweight line-plot widgets, extended to multi-trace). A dedicated high-performance telemetry panel (option B) is a separate Dream-tier feature for later.

- [ ] `LinePlotWidget` accepts multiple named traces, each with its own color and sample buffer
- [ ] Trace assignment UI — operator can add/remove traces and assign each to a variable key
- [ ] Per-trace color selection (auto-assigned defaults, user-overridable)
- [ ] Shared x-axis and configurable y-axis (auto-range across all visible traces, or per-trace manual limits)
- [ ] Legend or trace labels visible on the plot for readability
- [ ] Existing single-trace `double.lineplot` behavior is preserved as the default (one trace, no regression)
- [ ] Properties dialog updated for multi-trace configuration and persistence through layout save/load

---

## Open evaluation items

These are architectural directions that need discussion and decisions before implementation begins.

- Define the compatibility/migration contract for teams coming from `SmartDashboard`/`Shuffleboard` publishing patterns:
  - what works unchanged
  - what requires an adapter/bridge
  - what remains intentionally unsupported
- Evaluate a future telemetry event bus layer that decouples ingestion from UI rendering:
  - topic-based pub/sub subscription model
  - per-subscriber rate-limited delivery with coalescing
  - central latest-value cache for immediate subscriber bootstrap
  - explicit non-UI-thread ingestion/processing with safe Qt-thread handoff
- Clarify final deployment contract and reproducibility expectations
- Expand test coverage around UI interaction state transitions

---

## Deferred work

Lower-priority items parked for future consideration.

- Wire a UI toolbar/status-bar Connect button
- Write-ack protocol on TCP Publish (currently fire-and-forget)
- Expand smoke test published keys from ~6 + chooser to full TeleAutonV2 (~49 keys)
- Dedicated recorder-to-replay roundtrip test and replay seek correctness test (timeline widget and transport parity tests exist, but no end-to-end record → file → replay-load → seek → verify-state coverage)
