# PitchGraph

A cross-platform real-time audio pitch visualization application built with Qt and aubio.

## Overview

PitchGraph captures audio from your system's playback devices and displays a real-time graph of detected pitch frequencies. It works on both Linux and Windows platforms.

## Features

- Real-time pitch detection using aubio library
- Interactive Qt-based graphical interface
- Cross-platform audio capture (WASAPI on Windows, PipeWire/PulseAudio on Linux)
- Configurable pitch detection parameters
- Scrolling time-series graph visualization
- Audio device selection

## Architecture

### Component Overview

```
┌─────────────────────────────────────────────────────────────┐
│                     Qt Main Window                          │
│  ┌────────────────────────────────────────────────────┐     │
│  │           PitchGraphWidget (QCustomPlot)           │     │
│  │         Real-time pitch visualization              │     │
│  └────────────────────────────────────────────────────┘     │
│  ┌─────────────┐  ┌──────────────┐  ┌──────────────┐      │
│  │ Device      │  │ Start/Stop   │  │ Settings     │      │
│  │ Selector    │  │ Controls     │  │ Panel        │      │
│  └─────────────┘  └──────────────┘  └──────────────┘      │
└─────────────────────────────────────────────────────────────┘
                            │
                            ↓
┌─────────────────────────────────────────────────────────────┐
│                   AudioCaptureManager                       │
│  - Platform abstraction layer                               │
│  - Device enumeration                                       │
│  - Audio stream management                                  │
└─────────────────────────────────────────────────────────────┘
            │                              │
            ↓                              ↓
┌─────────────────────┐      ┌──────────────────────────┐
│ WindowsAudioCapture │      │   LinuxAudioCapture      │
│   (WASAPI Loopback) │      │ (PipeWire/PulseAudio)    │
└─────────────────────┘      └──────────────────────────┘
            │                              │
            └──────────────┬───────────────┘
                           ↓
┌─────────────────────────────────────────────────────────────┐
│                   PitchDetectionEngine                      │
│  - aubio pitch detection (YIN algorithm)                    │
│  - Circular buffer management                               │
│  - Frequency analysis                                       │
│  - Confidence scoring                                       │
└─────────────────────────────────────────────────────────────┘
                           ↓
┌─────────────────────────────────────────────────────────────┐
│                   PitchDataModel                            │
│  - Thread-safe pitch data storage                           │
│  - Signal emission for UI updates                           │
│  - Historical data management                               │
└─────────────────────────────────────────────────────────────┘
```

### Key Components

#### 1. **MainWindow** (Qt)
- Main application window
- Menu bar (File, Settings, Help)
- Central widget containing pitch graph
- Device selector dropdown
- Start/Stop capture controls

#### 2. **PitchGraphWidget** (QCustomPlot or custom QWidget)
- Real-time scrolling graph
- X-axis: Time (seconds)
- Y-axis: Frequency (Hz)
- Displays pitch confidence via color/opacity
- Grid lines for musical notes (A440, etc.)

#### 3. **AudioCaptureManager** (Abstract interface)
- Platform-agnostic audio capture interface
- Implementations:
  - `WindowsAudioCapture`: Uses WASAPI loopback recording
  - `LinuxAudioCapture`: Uses PipeWire or PulseAudio monitor sources
- Emits audio buffers via Qt signals

#### 4. **PitchDetectionEngine**
- Wraps aubio pitch detection (aubio_pitch_t)
- Uses YIN or YINFFT algorithm
- Configurable parameters:
  - Buffer size (e.g., 2048 samples)
  - Hop size (e.g., 512 samples)
  - Sample rate (e.g., 44100 Hz)
- Outputs pitch frequency and confidence

#### 5. **PitchDataModel**
- Thread-safe data storage using QMutex
- Stores recent pitch measurements (e.g., last 60 seconds)
- Emits signals when new data arrives
- Provides data access for graph widget

#### 6. **AudioProcessingThread** (QThread)
- Runs audio capture and pitch detection in background
- Prevents UI blocking
- Continuously processes audio buffers

## Dependencies

### Required Libraries

- **Qt 6** (or Qt 5.15+)
  - Qt Core
  - Qt Widgets
  - Qt Multimedia (used by batch MP3 decoding)

- **aubio** (>= 0.4.9)
  - Pitch detection library

- **Platform-specific:**
  - **Windows**: Windows SDK (WASAPI)
  - **Linux**:
    - PipeWire development libraries (`libpipewire-0.3-dev`)
    - OR PulseAudio development libraries (`libpulse-dev`)

- **Optional:**
  - **QCustomPlot** (for advanced graphing)

## Building

### Prerequisites

#### Linux (Ubuntu/Debian)
```bash
# For PipeWire (recommended for modern systems)
sudo apt-get install qt6-base-dev qt6-multimedia-dev libaubio-dev libpipewire-0.3-dev

# OR for PulseAudio (older systems)
sudo apt-get install qt6-base-dev qt6-multimedia-dev libaubio-dev libpulse-dev
```

#### Windows
- Install Qt 6 from qt.io
- Build or download aubio binaries
- Windows SDK (comes with Visual Studio)

### Build Instructions

```bash
# Create build directory
mkdir build && cd build

# Configure with CMake
cmake ..

# Build
cmake --build .

# Run
./PitchGraph
```

### CMake Configuration

The project uses CMake with platform detection:
- Automatically links WASAPI on Windows
- Detects and prefers PipeWire, falls back to PulseAudio on Linux
- Finds Qt and aubio libraries

## Usage

1. Launch the application
2. Select an audio device from the dropdown (loopback/monitor device)
3. Click "Start Capture"
4. Play audio on your system
5. Watch the real-time pitch graph

### Batch Export (CLI)

PitchGraph supports headless batch analysis/export from WAV and MP3 files:

```bash
PitchGraph --batch-config path/to/batch_manifest.json
```

Manifest format:

```json
{
  "conversation_files": [
    "audio/conversation_01.wav",
    "audio/conversation_02.mp3"
  ],
  "music_files": [
    "audio/music_a.mp3",
    "audio/music_b.wav"
  ],
  "output_dir": "exports",
  "mix_gain_db": -18,
  "conversation_gain_db": 0
}
```

Behavior:
- `conversation_files`: baseline exports (`conv_<name>__baseline.json`)
- `conversation_files x music_files`: mixed exports (`conv_<name>__music_<name>.json`)
- Supported input formats for batch files: `.wav`, `.mp3`
- output files are written inside a timestamped run folder under `output_dir`
- batch metadata includes conversation/music file paths, gain settings, baseline flag, and run index

## Configuration

Settings available in the Settings dialog:
- Audio buffer size
- Pitch detection algorithm (YIN, YINFFT, etc.)
- Sample rate
- Graph refresh rate
- Time window (amount of history to display)
- Pitch range (min/max frequency to display)

## Project Structure

```
PitchGraph/
├── CMakeLists.txt
├── README.md
├── ARCHITECTURE.md
├── src/
│   ├── main.cpp
│   ├── MainWindow.h/cpp
│   ├── PitchGraphWidget.h/cpp
│   ├── audio/
│   │   ├── AudioCaptureManager.h
│   │   ├── WindowsAudioCapture.h/cpp
│   │   └── LinuxAudioCapture.h/cpp
│   ├── processing/
│   │   ├── PitchDetectionEngine.h/cpp
│   │   └── AudioProcessingThread.h/cpp
│   └── model/
│       └── PitchDataModel.h/cpp
├── include/
└── resources/
    └── icons/
```

## Technical Details

### Audio Capture

**Windows (WASAPI Loopback):**
- Captures system audio playback
- Uses `IMMDeviceEnumerator` to find devices
- Requires "What U Hear" or loopback recording

**Linux (PipeWire/PulseAudio):**
- **PipeWire** (preferred): Uses PipeWire stream API to capture from monitor nodes
  - Automatically detects system audio sinks
  - Native support for modern Linux audio stack
- **PulseAudio** (fallback): Uses PulseAudio monitor sources
  - Captures from `.monitor` sink
  - Uses `pa_simple` or `pa_mainloop` API
- Runtime detection automatically selects available backend

### Pitch Detection

- aubio's YIN algorithm (recommended for musical pitch)
- Processes audio in overlapping windows
- Outputs fundamental frequency (F0) and confidence level
- Only displays pitch when confidence exceeds threshold (e.g., 0.7)

### Threading Model

- Main thread: Qt UI event loop
- Audio thread: Captures audio and runs pitch detection
- Communication via Qt signals/slots (thread-safe)

## Future Enhancements

- [ ] Musical note display (C4, A440, etc.)
- [ ] Multiple pitch detection (polyphonic)
- [ ] Audio recording/export
- [ ] Spectrogram view
- [ ] MIDI output
- [ ] Plugin support for different visualizations
- [ ] Microphone input mode
- [ ] Tuner mode with cents deviation

## License

[Specify your license here]

## Credits

- **Qt Framework**: https://www.qt.io/
- **aubio**: https://aubio.org/
- **QCustomPlot** (optional): https://www.qcustomplot.com/
