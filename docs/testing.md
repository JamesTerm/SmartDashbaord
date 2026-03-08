# Testing Guide

This document explains the automated and manual validation workflows.

- Edit this file when test commands, targets, or expected behaviors change.

## Automated unit tests

The repository includes GoogleTest-based automated tests in:

- `ClientInterface_direct/tests/direct_publisher_tests.cpp`
- `ClientInterface_direct/tests/smartdashboard_client_tests.cpp`

These tests validate core behaviors such as:

- telemetry publish/subscribe for bool/double/string
- deterministic latest-value semantics
- SmartDashboard client facade (`TryGet*`, `Get*(default)`, callback subscriptions)
- command channel receive paths for bool/double/string

### Build and run

1. Configure with tests enabled:

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON -DCMAKE_TOOLCHAIN_FILE="D:/code/vcpkg/scripts/buildsystems/vcpkg.cmake"
```

2. Build test target:

```bash
cmake --build build --config Debug --target ClientInterface_direct_tests
```

3. Run all discovered tests:

```bash
ctest --test-dir build --build-config Debug --output-on-failure
```

### Focused test runs

Run one specific test directly:

```bash
build/ClientInterface_direct/Debug/ClientInterface_direct_tests.exe --gtest_filter=DirectPublisherTests.StreamsSineWaveDouble
```

Use isolated per-test transport channels if needed:

```bash
set SD_DIRECT_TEST_USE_ISOLATED_CHANNELS=1 && build/ClientInterface_direct/Debug/ClientInterface_direct_tests.exe --gtest_filter=DirectPublisherTests.StreamsSineWaveDouble
```

## Manual dashboard integration checks

Use these checks when validating end-to-end UX behavior.

### Live stream check

1. Start dashboard:
   - `build/SmartDashboard/Debug/SmartDashboardApp.exe`
2. Run a stream test from another terminal:
   - `build/ClientInterface_direct/Debug/ClientInterface_direct_tests.exe --gtest_filter=DirectPublisherTests.StreamsSineWaveDouble`
3. Verify in dashboard:
   - state changes to `Connected`
   - tile updates continuously

### Command roundtrip check

1. Start dashboard:
   - `build/SmartDashboard/Debug/SmartDashboardApp.exe`
2. Run:
   - `build/ClientInterface_direct/Debug/sd_command_roundtrip_sample.exe`
3. In dashboard for `Integration/Armed`:
   - `Change to...` -> `Checkbox control`
   - toggle checkbox
4. Expect sample success message.

## Validation expectations

Before merging non-trivial behavior changes:

- run automated tests relevant to changed areas
- run at least one dashboard integration check for user-facing interaction changes
- update docs if test workflow or expected behavior changed
