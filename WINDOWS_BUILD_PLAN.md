# Windows Build Plan (Keep Linux PulseAudio + aubio)

## Goal
Make the app build and run on Windows with WASAPI loopback capture, while retaining Linux PulseAudio capture, and keep using aubio for pitch tracking.

## Environment Inputs
- Qt installed at `K:/sw/Qt` (detected toolchain: `K:/sw/Qt/6.11.0/mingw_64`)
- aubio installed at `K:/sw/aubio-0.4.6-win64`

## Plan
1. Define a platform-neutral capture interface and keep app flow unchanged.
   - Files: `AudioCapture.h`, `MainWindow.cpp`.
   - Introduce an interface-style API (`start/stop`, `audioDataReady`, `error`) so UI and `PitchDetector` remain backend-agnostic.

2. Split Linux PulseAudio code into a Linux-only backend.
   - Files: move current implementation from `AudioCapture.cpp` into new `PulseAudioCapture.h/.cpp`.
   - Keep existing `pa_simple` monitor-source logic (`@DEFAULT_MONITOR@`) unchanged.
   - Compile only on Linux.

3. Add a Windows WASAPI loopback backend.
   - New files: `WasapiAudioCapture.h/.cpp`.
   - Implement default render-device loopback using:
     - `IMMDeviceEnumerator`
     - `IAudioClient`
     - `IAudioCaptureClient`
   - Convert captured frames to mono `float` buffers matching the existing aubio input contract.

4. Add backend selection/factory wiring.
   - New file: `AudioCaptureFactory.h/.cpp` (or keep `AudioCapture` as a thin platform wrapper).
   - Select backend at compile time:
     - `_WIN32` -> WASAPI backend
     - `__linux__` -> PulseAudio backend

5. Refactor CMake to be platform-conditional.
   - File: `CMakeLists.txt`.
   - Linux:
     - Keep `pkg-config` + `libpulse` + `aubio`.
   - Windows:
     - Do not require PulseAudio or pkg-config.
     - Link WASAPI/system libs (`ole32`, `uuid`, `avrt`; add `ksuser` if needed by toolchain).
   - aubio:
     - Keep required on all platforms.
     - Add Windows fallback detection via `AUBIO_ROOT` (`find_path` + `find_library`).

6. Handle Windows runtime DLLs.
   - File: `CMakeLists.txt`.
   - Add a post-build step to copy `K:/sw/aubio-0.4.6-win64/bin/libaubio-5.dll` to the executable output directory (or document `PATH` requirement).

7. Preserve aubio pitch tracking behavior.
   - Files: `PitchDetector.h/.cpp`.
   - No algorithm changes; keep current YIN-based flow and thresholds.
   - Ensure capture backends emit the same `QVector<float>` payload semantics.

8. Validate with a 2-OS build/test checklist.
   - Windows configure:
     - `cmake -S . -B build-win -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH=K:/sw/Qt/6.11.0/mingw_64 -DAUBIO_ROOT=K:/sw/aubio-0.4.6-win64`
   - Linux configure:
     - Existing pkg-config flow with PulseAudio + aubio.
   - Smoke tests:
     - Start capture
     - Play system audio
     - Verify graph updates and no backend-specific crashes

## Assumption
The current Windows stack uses MinGW-compatible Qt/aubio artifacts (`mingw_64` and `.dll.a`), so Windows build steps target MinGW.
