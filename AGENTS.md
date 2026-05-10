# AGENTS.md

## Mission
PitchGraph is a real-time desktop app for detecting and visualizing human voice pitch (F0) in Japanese conversation, including conditions with background music or other environmental sounds.

## Product Priorities
1. Prefer speech pitch reliability over generic music pitch tracking.
2. Minimize false voiced detections caused by music/noise.
3. Keep pitch contours stable (low octave jumps/jitter) with low UI latency.

## Code Map
- `MainWindow.*`: app UI and signal wiring.
- `AudioCapture*`, `WasapiAudioCapture*`, `PulseAudioCapture*`: platform capture backends.
- `PitchDetector.*`: aubio-based pitch estimation, gating, and continuity logic.
- `PitchGraphWidget.*`: real-time visualization and export.

## Engineering Guardrails
- Keep speech/noise discrimination logic in `PitchDetector` unless interface changes are required.
- Make tuning values explicit constants and keep defaults conservative.
- Preserve cross-platform behavior (Windows WASAPI, Linux PulseAudio).
- Avoid adding heavy DSP dependencies without clear accuracy gains.

## Definition of Done (for changes)
- Project builds with CMake on the active platform.
- Start/Stop capture and graph rendering still work.
- No increase in false pitch detections from background audio.
- Update docs briefly when detection behavior or tuning knobs change.
