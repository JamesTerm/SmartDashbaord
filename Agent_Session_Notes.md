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

# Test (197 tests, 2 disabled)
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
| Run Browser dock | `feature/run-browser-dock` | Complete, pending merge to main |
| Native Link TCP carrier | `feature/native-link-tcpip-carrier` | Merged to main |
| NT4 transport (originally "Shuffleboard") | `feature/shuffleboard-transport` | Merged to main |
| Glass verification + Shuffleboard→NT4 rename | `feature/glass-transport` | Merged to main |

Glass support details and NT4 protocol reference moved to `docs/project_history.md`.

## Deferred work

- Wire a UI toolbar/status-bar Connect button
- Write-ack protocol on TCP Publish (currently fire-and-forget)
- Expand smoke test published keys from ~6 + chooser to full TeleAutonV2 (~49 keys)
