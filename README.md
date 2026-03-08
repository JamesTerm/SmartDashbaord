# SmartDashboard (C++)

Lightweight C++ dashboard for FRC, inspired by WPILib SmartDashboard.

## Why this project

- Built as a community-friendly path forward as legacy SmartDashboard approaches end-of-life (2027).
- Focused scope: fast live values (`bool`, `double`, `string`), editable widgets, and saved layouts.
- Uses a direct local transport layer (`*_direct`) instead of NetworkTables for v1.

## What's in this repo

- `SmartDashboard` - Qt desktop app
- `SmartDashboard_Interface_direct` - subscriber/consumer transport layer
- `ClientInterface_direct` - publisher/producer transport layer + sample publisher
- Design + notes: `design/SmartDashboard_Design.md`, `docs/history.md`, `Agent_Session_Notes.md`

## Quick start (Windows + MSVC + vcpkg)

1. Install Qt6 in vcpkg (at minimum):
   - `vcpkg install qtbase --triplet x64-windows`
2. Configure:
   - `cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE="D:/code/vcpkg/scripts/buildsystems/vcpkg.cmake"`
3. Build:
   - `cmake --build build --config Debug`
4. Run:
   - `build/SmartDashboard/Debug/SmartDashboardApp.exe`

Optional sample publisher:

- `build/ClientInterface_direct/Debug/sd_direct_publisher_sample.exe`
- Command roundtrip sample (manual dashboard writeback check):
  - `build/ClientInterface_direct/Debug/sd_command_roundtrip_sample.exe`
- Tests now publish to the same default direct channel used by the dashboard (good for live manual testing).
- To force isolated per-test channels instead:
  - `set SD_DIRECT_TEST_USE_ISOLATED_CHANNELS=1 && build/ClientInterface_direct/Debug/ClientInterface_direct_tests.exe --gtest_filter=DirectPublisherTests.StreamsSineWaveDouble`

## Live test loop

Use this loop to validate end-to-end publisher/subscriber behavior:

1. Start dashboard:
   - `build/SmartDashboard/Debug/SmartDashboardApp.exe`
2. In a second terminal, run one stream test (shared channel default):
   - `build/ClientInterface_direct/Debug/ClientInterface_direct_tests.exe --gtest_filter=DirectPublisherTests.StreamsSineWaveDouble`
3. Confirm behavior in the app:
   - connection state transitions to `Connected`
   - tiles are created/updated for the streamed key(s)
   - values continue updating during the test window
4. Re-run the same test to confirm reconnect behavior:
   - existing tiles should continue updating after publisher restart

## Manual command roundtrip check

Use this check for dashboard -> app/client command flow:

1. Start dashboard:
   - `build/SmartDashboard/Debug/SmartDashboardApp.exe`
2. Run roundtrip sample:
   - `build/ClientInterface_direct/Debug/sd_command_roundtrip_sample.exe`
3. In dashboard, for key `Integration/Armed`:
   - right-click tile -> `Change to...` -> `Checkbox control`
   - check the box
4. Expected result:
   - sample exits with success (`Roundtrip command received. Success.`)

## Windows build note (vcpkg + PowerShell)

- This repo sets Visual Studio global `VcpkgXUseBuiltInApplocalDeps=true` in `CMakeLists.txt` to avoid recurring `pwsh.exe` lookup noise on machines that only have Windows PowerShell.
- If you copy this setup into other projects, you can reuse the same `CMAKE_VS_GLOBALS` setting for cleaner MSBuild output.
- Optional Qt runtime companion DLL misses (`dxcompiler.dll`, `dxil.dll`, `opengl32sw.dll`) are hidden by default to keep build output clean.
- Enable deploy diagnostics only when needed with `-DSMARTDASHBOARD_VERBOSE_QT_DEPLOY=ON`.

## Current status

- Core app + direct transport baseline is implemented.
- Runtime deployment is being stabilized for fully reproducible vcpkg-only startup.

For architecture and implementation details, see `design/SmartDashboard_Design.md`.
For FRC dashboard/telemetry background and architectural evolution, see `docs/history.md`.
