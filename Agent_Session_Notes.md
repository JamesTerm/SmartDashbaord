# Agent session notes

- Keep this file short and handoff-focused.
- Move durable milestone history to `docs/project_history.md`.
- Move verbose findings, root-cause investigations, and debugging narratives to `docs/journal/<date>-<slug>.md` (descending date order, newest first).
- Once a topic is fully resolved and no longer needs to be in the foreground, move it to the journal. Do not let completed work accumulate here.

## Workflow

- **Use CRLF line endings** for all source files (`.cpp`, `.h`, `.cmake`, `.ps1`, `.py`, `.md`, `.rc`, `.gitignore`). Both repos standardized CRLF as of the NT4 transport merge.
- Read nearby `Ian:` comments before editing and add new ones where transport, protocol, lifecycle, or ownership lessons would be expensive to rediscover.
- Never mix `SetEnvironmentVariableA` (Win32 write) with `_dupenv_s` (CRT read) for the same variable. Use `GetEnvironmentVariableA` on the read side to match the Win32 write.
- `gtest_discover_tests` calls all use `DISCOVERY_MODE PRE_TEST`.
- `vcpkg.json` manifest in repo root auto-installs all C++ dependencies (qtbase, ixwebsocket) during CMake configure. No manual `vcpkg install` needed — just pass `-DCMAKE_TOOLCHAIN_FILE=...` and the manifest handles the rest.
- Solution Explorer folder grouping is in place (`USE_FOLDERS ON`).
- Debug logs only write when `--instance-tag` is passed or `SMARTDASHBOARD_INSTANCE_TAG` env var is set. Logs go to `.debug/native_link_ui_<tag>.log`.
- SmartDashboard settings are persisted in Windows Registry under `HKCU:\Software\SmartDashboard\SmartDashboardApp`.

## Build

```bash
# Configure (vcpkg.json manifest auto-installs Qt6, ixwebsocket, etc.)
cmake -G "Visual Studio 17 2022" -B build -DCMAKE_TOOLCHAIN_FILE="<your-vcpkg-root>/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows

# Build
cmake --build build --config Debug

# Test (225 tests, 2 disabled, 1 pre-existing failure)
ctest -C Debug --output-on-failure
```

## Key invariants (do not break)

- `RegisterDefaultTopics` stays minimal — robot-code telemetry keys auto-register on first server write.
- `IsHarnessFocusKey` in `main_window.cpp` is an intentional narrow allowlist for the debug log only. All keys still create tiles and receive updates.
- The alive guard (`m_alive` shared_ptr) in `PluginDashboardTransport::Stop()` must be set false before calling the plugin's stop, not after.
- TCP client `Start()` is non-blocking and always returns `true`. Failure manifests as a `Disconnected` callback. The host reconnect timer handles retries.
- `OnDisconnectTransport` must route through `OnConnectionStateChanged`, not `UpdateWindowConnectionText` directly. The full pipeline (title, menu enable states, recording event) must all fire together on any state transition.
- **Host-level auto-reconnect:** All plugin transports make a single connect attempt per `Start()` call. The reconnect timer in `MainWindow` (`m_reconnectTimer`, 1-second single-shot) drives retries via Stop()+Start() cycles. Plugins must NOT implement their own retry loops.
- **WSAStartup is deferred:** Winsock is initialized only when a WebSocket-based transport actually connects (via `ix::initNetSystem()`). Direct/NativeLink sessions never trigger it.

## Transport architecture

### Plugin system

Plugins live under `plugins/<Name>Transport/` and implement the C ABI in `dashboard_transport_plugin_api.h`. See the `Ian:` comment at the top of that file for the checklist of adding a new transport.

Current plugins:
| Plugin | ID | Protocol | Status |
|---|---|---|---|
| LegacyNtTransport | `legacy-nt` | NetworkTables v2 TCP | Stable |
| NativeLinkTransport | `native-link` | Custom TCP/SHM | Stable |
| NT4Transport | `nt4` | NT4 WebSocket | Stable, merged |

### Auto-connect

- The `auto_connect` connection field descriptor stays in each plugin's descriptor table so the UI checkbox works. The host reads it via `MainWindow::IsAutoConnectEnabled()` from `pluginSettingsJson`.
- `m_userDisconnected` flag distinguishes manual Disconnect (suppress retries) from transport-initiated drops (retry if auto-connect enabled).

### Write-back

- `PublishBool/Double/String` in the plugin ABI is fully wired for NT4. The `EnsurePublished` path in `nt4_client.cpp` uses the `/SmartDashboard/` prefix.
- `supports_chooser` returns true. This property controls whether inbound updates are assembled into chooser widgets — it does NOT gate outbound publish.
- `RememberControlValueIfAllowed` only works for Direct transport (`CurrentTransportUsesRememberedControlValues()` returns true only for `TransportKind::Direct`).

## Cross-repo sync

- This repo and `D:\code\Robot_Simulation` share the Native Link contract and the NT4 protocol layer.
- Canonical rollout strategy: `docs/native_link_rollout_strategy.md` (this repo).
- When either repo's session notes change, check the other side for consistency.

## Complete: Run Browser dock (`feature/run-browser-dock`)

Dockable tree panel for browsing signal key hierarchies. The Run Browser is an **optional navigational filter** — it never blocks tile creation or visibility by default.

### Two-mode design

| | Reading mode (Replay) | Streaming / Layout-mirror mode (Live) |
|---|---|---|
| Transport kinds | `TransportKind::Replay` | Direct, NativeLink, NT4, LegacyNT |
| Tree population | Up front from JSON parse (`AddRunFromFile`) | Driven by layout tile lifecycle (`OnTileAdded` / `OnTileRemoved`) |
| Default visibility | **Off** — user opts in via checkboxes | **On** — everything visible, user opts out |
| Top-level node | Named run from file label/metadata | Transport-labeled root node (`kNodeKindRun`) |
| Persistence | Checked keys (`runBrowser/checkedKeys`) | Hidden keys (`runBrowser/hiddenKeys`) |

MainWindow drives mode selection:
- **Reading mode:** `ClearAllRuns()` → `AddRunFromFile(path)` → groups start unchecked → user opts in
- **Streaming mode:** `ClearDiscoveredKeys()` → `SetStreamingRootLabel(name)` → `OnTileAdded()` per tile → groups start checked → user opts out

### Architecture (streaming mode)

- MainWindow emits `TileAdded`, `TileRemoved`, `TilesCleared` signals from tile lifecycle points (`GetOrCreateTile`, `OnRemoveWidgetRequested`, `OnClearWidgets`)
- Dock connects to these signals — tree is a 1:1 mirror of the layout's tile collection
- `SetStreamingRootLabel()` initializes streaming mode (always fully reinitializes — clears model, creates fresh root)
- `ClearDiscoveredKeys()` stays in streaming mode (clears tree, recreates root) — only `ClearAllRuns()` exits streaming mode
- Reading mode is fully immune to layout operations — all three signal handlers guard on `!m_streamingMode`
- No Clear button — dock content is driven entirely by layout lifecycle and transport state

### Files

| File | What |
|---|---|
| `src/widgets/run_browser_dock.h` | `RunBrowserDock` QDockWidget, structs, persistence API |
| `src/widgets/run_browser_dock.cpp` | JSON parse, tree model, checkbox propagation, layout-mirror, persistence |
| `src/app/main_window.h` | `TileAdded`, `TileRemoved`, `TilesCleared` signals |
| `src/app/main_window.cpp` | Integration — dock lifecycle, visibility filtering, persistence, signal wiring |
| `tests/run_browser_dock_tests.cpp` | 104 GTest tests |
| `CMakeLists.txt` | Sources in app + test targets |

### Known limitations

- `SignalActivated` / `RunActivated` signals have no downstream consumer (future: comparison plots)
- `runIndex` is a vector index — unstable across operations
- No `RemoveRun(int)` API — only bulk clear
- No duplicate-file detection
- No `qWarning()` on parse failures
- Slash-only keys produce no tree nodes

## Completed milestones

| Feature | Branch | Status |
|---|---|---|
| Universal show-label toggle | `feature/camera-widget` | Complete (UI housekeeping) |
| Multi-select lasso + group drag | `feature/camera-widget` | Complete (UI housekeeping) |
| Run Browser dock | `feature/run-browser-dock` | Complete, pending merge to main |
| Native Link TCP carrier | `feature/native-link-tcpip-carrier` | Merged to main |
| NT4 transport (originally "Shuffleboard") | `feature/shuffleboard-transport` | Merged to main |
| Glass verification + Shuffleboard→NT4 rename | `feature/glass-transport` | Merged to main |

Glass support details and NT4 protocol reference moved to `docs/project_history.md`.

## Complete: Universal show-label toggle

Consolidated the three per-widget-type show-label properties (`boolCheckboxShowLabel`, `boolLedShowLabel`, `stringTextShowLabel`) into a single universal `showLabel` property on `VariableTile`. Every widget type now has a "Show Label" checkbox in its Properties dialog.

**Changes:**
- `variable_tile.h`: Single `m_showLabel` member + `SetShowLabel(bool)` replaces 3 old members/setters
- `variable_tile.cpp`: Universal `SetShowLabel()`, rewritten `UpdateWidgetPresentation()` with label-hidden layout paths for all 12 widget types, all property dialogs updated, `string.chooser` added to `IsPropertiesSupported()`
- `layout_serializer.h`: Single `QVariant showLabel` replaces 3 old fields
- `layout_serializer.cpp`: Saves/loads `"showLabel"` JSON key; backward-compat migration reads old per-widget keys from older layout files
- `main_window.cpp`: Single `SetShowLabel()` call in `ApplyLayoutEntryToTile()`

Ian: `double.gauge` previously always hid its label (hardcoded). Now it respects `m_showLabel` like every other widget type. If gauge layouts from before this change look different, that's why.

## Complete: Multi-select lasso + group drag

Added rubber-band rectangle selection on the canvas in edit mode, with visual selection indicators and coordinated group drag for moving multiple tiles at once. There was no selection mechanism before this — no selected state, no multi-select, no rubber band.

**Selection mechanics:**
- Lasso: Click+drag on empty canvas draws a `QRubberBand`; on release, all tiles intersecting the rectangle become selected
- Ctrl+click on a tile toggles its selection without clearing others
- Click on an unselected tile (without Ctrl) clears selection and selects only that tile
- Escape key clears selection
- Selection auto-clears when: edit mode turns off, tiles are cleared, or a tile is removed

**Group drag mechanics:**
- The anchor tile (the one the user physically drags) moves itself via its own `mouseMoveEvent`
- `MainWindow::eventFilter` detects the anchor's `QEvent::Move` and applies the same delta to all other selected tiles
- `BeginGroupDrag` snapshots each selected tile's position; `UpdateGroupDrag` computes delta from anchor movement; `EndGroupDrag` resets state
- The anchor is tracked explicitly by pointer (`m_groupDragAnchor`), not inferred from position changes

**Visual indicator:**
- Selected tiles in edit mode get a 2px blue dashed border (`#3a9bdc`) drawn in `VariableTile::paintEvent()`

**Changes:**
- `variable_tile.h`: Added `m_selected` flag, `SetSelected(bool)`, `IsSelected()`
- `variable_tile.cpp`: `SetSelected()`/`IsSelected()` implementation, `paintEvent()` draws selection border
- `main_window.h`: Selection state (`QSet<VariableTile*> m_selectedTiles`), lasso state (`QRubberBand*`, `m_lassoOrigin`, `m_lassoActive`), group drag state (`m_groupDragActive`, `m_groupDragAnchor`, `m_groupDragUpdating`, `m_groupDragEntries`), helper method declarations, `QRubberBand` forward decl
- `main_window.cpp`: Event filter on canvas for lasso (press/move/release), tile events for group drag + Ctrl+click toggle, `ClearTileSelection()`, `SelectTilesInRect()`, `BeginGroupDrag()`, `UpdateGroupDrag()`, `EndGroupDrag()`, Escape in `keyPressEvent()`, selection cleared in `OnToggleEditable()`/`OnClearWidgets()`/`OnRemoveWidgetRequested()`

**Bugs found and fixed during manual testing:**

1. **Lasso started on tile clicks (not just empty canvas).** The tile's `mousePressEvent` calls `QFrame::mousePressEvent` which does `event->ignore()`, propagating the press up to `m_canvas`. The event filter saw `watched == m_canvas` and started the lasso even though the click was on a tile. Fix: added `m_canvas->childAt(me->pos()) == nullptr` guard in the MouseButtonPress handler, and added `m_lassoRubberBand->isVisible()` guard to the MouseButtonRelease handler so a stale rubber band from a prior lasso can't accidentally clear the selection.

2. **Group drag broke apart after first frame.** Three interrelated bugs:
   - Anchor was detected by position heuristic ("first tile that moved") — after the first frame ALL tiles had moved, so it picked the wrong tile. Fix: explicit `m_groupDragAnchor` pointer set in `BeginGroupDrag`.
   - "Skip anchor" logic used `pos() != startPos` which matched ALL tiles after first frame, so siblings stopped moving. Fix: skip by pointer identity (`entry.tile == m_groupDragAnchor`).
   - Moving siblings via `entry.tile->move()` fired `QEvent::Move` on each sibling, re-entering `UpdateGroupDrag` recursively. Fix: `m_groupDragUpdating` re-entry guard, plus event filter only triggers `UpdateGroupDrag` when the moving tile is `m_groupDragAnchor`.

Ian: The anchor tile moves itself via its own drag handler. The event filter only moves the *other* selected tiles. If you change tile drag to use a different coordinate system, update `BeginGroupDrag`/`UpdateGroupDrag` to match — they rely on `tile->pos()` in canvas coordinates.

Ian: The tiles' `mousePressEvent` calls `QFrame::mousePressEvent(event)` at the end, which does `event->ignore()`. This causes mouse presses to propagate from tiles up to the canvas. Any canvas event filter handler that assumes "if I see `watched == m_canvas`, the click was on empty space" is wrong — always check `m_canvas->childAt(pos)` first.

Ian: `string.chooser` was missing from `IsPropertiesSupported()` — fixed during the show-label work. If a new widget type is added, remember to add it to that function or its Properties menu item will be grayed out.

## In progress: Camera viewer dock (`feature/camera-widget`)

MJPEG camera stream viewer as a dockable panel.  Full design in
`docs/camera_widget_design.md`.

### Research completed

- Analyzed 2014 BroncBotz Dashboard video pipeline (FrameGrabber, Preview,
  ProcessingVision, ProcAmp, Controls, FrameWork library).
- Documented complete NT4 CameraPublisher key schema and stream URL format
  (`/CameraPublisher/{Name}/streams` -> `mjpg:http://...`, base port 1181).
- Confirmed Glass has no built-in camera viewer.
- Confirmed SmartDashboard has zero existing camera/video code.

### Architecture summary

- **MjpegStreamSource**: `QNetworkAccessManager` HTTP client that parses
  `multipart/x-mixed-replace` boundaries and decodes JPEG frames via
  `QImage::loadFromData()`.  No new dependencies.
- **CameraDisplayWidget**: Custom `QWidget::paintEvent()` with aspect-ratio
  scaling and fighter-jet style targeting reticle overlay (dashboard-side,
  QPainter, click-drag positionable).
- **CameraViewerDock**: `QDockWidget` following `RunBrowserDock` pattern --
  toolbar with camera selector combo, URL field, connect/disconnect, reticle
  toggle.  View menu "Camera" checkbox, starts hidden.
- **CameraPublisherDiscovery**: Watches NT4 `/CameraPublisher/` keys to
  auto-populate the camera selector.
- **CameraStreamSource**: Abstract interface so display widget accepts frames
  from any backend (MJPEG, future Robot_Simulation, test pattern).

Ian: Two separate overlay concepts exist and must not be conflated:
1. **Targeting reticle** (SmartDashboard): Dashboard-side QPainter overlay
   drawn on top of the video widget.  Fighter-jet crosshair + circle.
2. **Backup camera guide lines** (Robot_Simulation): Simulator-side OSG
   overlay drawn in 3D and baked into MJPEG frames.  Honda-style curved
   path lines driven by velocity/angular velocity.
These serve different purposes and live in different codebases.

### Implementation phases

1. ~~MJPEG stream reader + display widget + dock + CameraPublisher discovery + MainWindow wiring (MVP)~~ **COMPLETE**
1b. ~~Auto-connect on discovery, auto-reconnect on error, dock visibility auto-connect, 28 unit tests~~ **COMPLETE**
2. Targeting reticle overlay (dashboard-side, crosshair + circle)
3. ~~Robot Simulation MJPEG server (Robot_Simulation repo)~~ **COMPLETE** — built and verified
4. Backup camera guide lines (Robot_Simulation repo, OSG-side)

Ian: Phase 3 implementation in Robot_Simulation (`feature/camera-widget` branch):
- MjpegServer subclasses ix::SocketServer (not ix::HttpServer — that's request-response only)
- SimCameraSource generates 320x240@15fps synthetic frames via stb_image_write JPEG encoding
- NT4Backend publishes /CameraPublisher/SimCamera/streams for auto-discovery
- Stream URL: mjpg:http://127.0.0.1:1181/?action=stream

Ian: Phase 3/4 design note — the simulator's MJPEG server should support two
source modes: (a) OSG framebuffer readback (vector graphics on black, the
default) and (b) real USB camera feed with OSG guide-line overlay composited
on top.  When a USB camera is available, the simulator can grab frames from
it, draw the backup-camera guide lines over the live video, encode to JPEG,
and serve via MJPEG.  When no camera is available, fall back to the existing
pure-OSG render.  This keeps the MJPEG server API identical either way —
the dashboard doesn't care whether frames come from a real camera or a
synthetic render.  Test with a machine that has a USB camera connected.

### Current status

Phase 1 (MVP) is **complete** including auto-connect/auto-reconnect wiring and
28 unit tests.  225 total tests (224 pass, 1 pre-existing failure, 2 disabled).

**All source files created and integrated:**
- `camera_stream_source.h`, `mjpeg_stream_source.h/.cpp`, `camera_publisher_discovery.h/.cpp`
- `camera_display_widget.h/.cpp`, `camera_viewer_dock.h/.cpp`
- `main_window.h` modified (forward decls, 3 member variables)
- `main_window.cpp` modified (7 integration points: includes, View menu action, dock+discovery creation, variable update routing, StopTransport camera stop, disconnect camera clear)
- `CMakeLists.txt` modified (new sources in both app and test targets)
- `tests/camera_viewer_dock_tests.cpp` — 28 GTest tests (11 CameraPublisherDiscovery, 13 CameraViewerDock, 4 Discovery→Dock integration)

**Auto-connect / auto-reconnect wiring (Phase 1b):**
- `AddDiscoveredCamera`: auto-connects when dock is visible + idle
- Error-state auto-reconnect via 2-second single-shot `QTimer` (`m_reconnectTimer`)
- `m_userDisconnected` flag: manual Disconnect suppresses auto-reconnect; new discovery or dock re-show clears the flag (mirrors MainWindow transport reconnect pattern)
- `TryAutoConnect()` fires on dock visibility change — connects if idle and cameras are available

**Build fixes applied during integration:**
- `camera_viewer_dock.h`: Changed forward declaration of `CameraStreamSource` to full `#include "camera/camera_stream_source.h"` — the header uses `CameraStreamSource::State` enum in a slot signature, which requires the full type definition
- `mjpeg_stream_source.cpp`: Fixed Most Vexing Parse — `QNetworkRequest request(QUrl(url))` was parsed as a function declaration; changed to brace-init `QNetworkRequest request{QUrl(url)}`
- `CMakeLists.txt` (test target): Added `camera_stream_source.h` to sources so MOC generates the QObject meta-object for the abstract base class (Q_OBJECT signals need MOC even in header-only classes)

**NT4 data flow verified (7 stages):**
Robot_Simulation publishes `/CameraPublisher/SimCamera/streams` on NT4 port 5810
→ SmartDashboard NT4 plugin subscribes to all (topics `[""]`, prefix true)
→ plugin delivers TopicUpdate with `StripSmartDashboardPrefix` (pass-through for `/CameraPublisher/` keys)
→ ABI bridge converts `std::vector<std::string>` to `sd_transport_value_v1` StringArray
→ host-side `OnPluginVariableUpdate` converts to `QVariant(QStringList)`
→ MainWindow forwards to `CameraPublisherDiscovery::OnVariableUpdate`
→ Discovery emits `CameraDiscovered("SimCamera", ["http://..."])` → Dock auto-connects

**Next steps:**
- H.264 discussion (options only, no implementation)
- Targeting reticle overlay (Phase 2)
- Manual end-to-end test with running Robot_Simulation

### Files

| File | What |
|---|---|
| `src/camera/camera_stream_source.h` | Abstract frame source interface |
| `src/camera/mjpeg_stream_source.h/.cpp` | MJPEG HTTP stream reader |
| `src/camera/camera_publisher_discovery.h/.cpp` | NT4 CameraPublisher key watcher |
| `src/widgets/camera_viewer_dock.h/.cpp` | Dock widget container (auto-connect, auto-reconnect) |
| `src/widgets/camera_display_widget.h/.cpp` | Custom paint widget + HUD overlay |
| `tests/camera_viewer_dock_tests.cpp` | 28 GTest tests (discovery, dock, integration) |
| `docs/camera_widget_design.md` | Full design document |

### 2014 reference codebase (read-only, at `D:\Stuff\BroncBotz\Code\BroncBotz_DashBoard\Source`)

Key files consulted during research:

| File | What |
|---|---|
| `Dashboard/Dashboard.cpp` | Main app, video pipeline orchestration, ini parsing |
| `FFMpeg121125/FrameGrabber.h` | FrameGrabber facade with FFMpeg/HTTP/TestPattern backends |
| `FrameWork/Preview.h/.cpp` | DirectDraw 7 multi-buffer renderer |
| `FrameWork/Bitmap.h` | Templatized bitmap containers |
| `ProcessingVision/ProcessingVision.h/.cpp` | Vision processing plugin interface |
| `ProcessingVision/NI_VisionProcessing.cpp` | NI Vision particle analysis |
| `Controls/Controls.cpp/.h` | Controls DLL plugin (file controls, procamp controls) |
| `ProcAmp/procamp_matrix.h` | 4x4 color correction matrix math |

## Deferred work

- Wire a UI toolbar/status-bar Connect button
- Write-ack protocol on TCP Publish (currently fire-and-forget)
- Expand smoke test published keys from ~6 + chooser to full TeleAutonV2 (~49 keys)
