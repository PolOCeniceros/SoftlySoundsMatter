# SoftlySoundsMatter — Class Documentation

This document describes the core classes in `SoftlySoundsMatter/src` and how they work together.

## High-level architecture

Runtime dataflow is:

- **Capture**: `VideoCaptureManager` pulls frames from a camera (`ofVideoGrabber` / GStreamer on Linux).
- **Process**: `ImageProcessor` downsamples, converts to grayscale, applies exposure/contrast, then Sobel edge magnitude.
- **Playhead**: `ofApp` advances a horizontal playhead across the processed image.
- **Sonify**: `ColumnSonifier` converts the current image column into mono audio (sine bank) and writes stereo.
- **Output**: `AudioEngine` owns the OF sound stream and calls the app-provided render callback.
- **Controls (Linux)**: `Mcp3008Spi` reads raw ADC values; `AnalogKnob` maps those values into stepped parameters.

## Class: `ofApp`

**Location**: `src/ofApp.h`, `src/ofApp.cpp`  
**Role**: Main orchestration layer (OpenFrameworks `ofBaseApp`).

### Responsibilities

- Owns and wires together the subsystems:
  - `VideoCaptureManager video`
  - `ImageProcessor image`
  - `ColumnSonifier sonifier`
  - `AudioEngine audio`
- Owns runtime parameters (`Params`) and maps them from knobs (Linux) into processing/audio behavior.
- Manages the playhead position and rendering (preview vs processed mode).
- Implements the main OF lifecycle callbacks: `setup`, `update`, `draw`, `keyPressed`.

### Modes of operation

- **Preview mode** (`video.isCapturing() == true`):
  - The live camera feed is drawn fullscreen (cover scaling).
  - Audio callback outputs silence.
  - Knob-to-parameter updates are intentionally *not* applied in this mode.
- **Playback mode** (`video.isCapturing() == false` and `image.hasProcessed()`):
  - A processed Sobel image is displayed, and a vertical playhead line scans across it.
  - The current image column under the playhead is synthesized into audio.

### Key structs

- `Params`
  - `contrast`, `exposure`, `sobelStrength` → image processing.
  - `playheadSpeed` → playhead motion (pixels/second in screen space).
  - `volume`, `minFreq`, `maxFreq` → audio synthesis range and gain.
- `DrawTransform`
  - `scale`, `offsetX`, `offsetY` for drawing the processed image in “cover” mode.

### Notable methods (private helpers)

- `updatePlayheadPosition()`
  - Advances `playheadX` by `playheadSpeed * dt` and wraps at window edges.
- `getProcessedTransform()`
  - Computes cover scale/offset for drawing the processed image centered fullscreen.
- `getImageXFromPlayhead()`
  - Converts the *screen* playhead X into an *image-space* X column index.
- `drawVideoPreview()`, `drawProcessedView()`, `drawStatusOverlay()`
  - Render the current mode and a parameter HUD.
- `resetAllParametersToDefaults()`
  - Loads defaults from the `AnalogKnob` objects and “latches” knobs to avoid snap-back.
- `togglePlayback()`
  - Pauses/unpauses playback by toggling `playheadSpeed` between 0 and the last non-zero value.

### Inputs (keyboard)

- **Space**: toggles between preview and playback.
  - When leaving preview: captures an RGB frame (`video.captureFrameToRGB`) and sends it to `image.setSourceRGB`, then pauses capture.
  - When returning to preview: resumes capture.
- **R / r**: reset parameters to defaults (with knob latch).
- **P / p**: toggle playback (playhead speed 0 vs last speed).

### Linux knob mapping (MCP3008 CH0..CH5)

Each knob is configured as `(min, step, max, default)` and quantized to `step`:

- **CH0** Contrast: `[0.2 .. 3.0]`, step `0.01`, default `1.0`
- **CH1** Exposure: `[-1.0 .. 1.0]`, step `0.01`, default `0.0`
- **CH2** Sobel Strength: `[0.1 .. 5.0]`, step `0.01`, default `1.0`
- **CH3** Playhead Speed: `[-600 .. 600]`, step `1`, default `120`
- **CH4** Volume: `[0 .. 1]`, step `0.01`, default `0.5`
- **CH5** Max Frequency: `[1000 .. 10000]`, step `10`, default `4000`

**Latch behavior**: after reset, each parameter is held until its corresponding knob moves by more than
`kKnobLatchDeadbandRaw` (raw ADC units) from the raw value recorded at reset.

### Ownership/lifecycle

- `ofApp::~ofApp()` calls `audio.close()` and `video.close()` to stop streams cleanly.
- Audio callback is installed in `setup()` via `AudioEngine::setup()` and references `video`, `image`, and `sonifier`.

## Class: `ImageProcessor`

**Location**: `src/ImageProcessor.h`, `src/ImageProcessor.cpp`  
**Role**: Owns the current source image and generates a processed Sobel image.

### Responsibilities

- Stores:
  - `original` (RGB source, typically captured from the camera)
  - `graySmall` (downscaled grayscale working image)
  - `sobelImg` (final Sobel-magnitude grayscale image)
- Performs a small pipeline when `dirty == true`:
  1. Resize original to `scaleFactor`
  2. Convert to grayscale
  3. Apply exposure/contrast adjustments
  4. Apply Sobel filter and write into `sobelImg`

### Public API

- `setScaleFactor(float s)`
  - Controls internal downscaling; marks processing as dirty.
- `setSourceRGB(const ofPixels& rgb)`
  - Sets a new source image and allocates processing buffers.
- `loadFromFile(const std::string& path)`
  - Declared but not implemented in the current source; if needed, implement similarly to `setSourceRGB`.
- `setParams(float contrast, float exposure, float sobelStrength)`
  - Stores new parameters and marks dirty on change.
- `update()`
  - Runs processing only when dirty and source is available.
- Getters: `hasSource`, `hasProcessed`, `getSobelImage`, `getSobelPixels`, `getWidth`, `getHeight`.
- `calculateDrawScale(float windowW, float windowH) const`
  - Returns “cover” scale to fill the window (may crop).

### Implementation notes

- Exposure is treated as an additive offset in normalized \([0..1]\) space, then contrast is applied around 0.5:
  - `v += exposure; v = (v - 0.5) * contrast + 0.5`
- Sobel is implemented manually over grayscale pixels:
  - magnitude uses \(|gx| + |gy|\) scaled by `sobelStrength`, then clamped to \([0..255]\).
- Edges (border pixels) are set to 0 because the Sobel loop skips 0 and width-1/height-1.

### Ownership/lifecycle

All images are owned by the class. `getSobelPixels()` returns a reference to `sobelImg`’s pixels.

## Class: `VideoCaptureManager`

**Location**: `src/VideoCaptureManager.h`, `src/VideoCaptureManager.cpp`  
**Role**: Robust camera initialization + frame update + frame snapshotting.

### Responsibilities

- Initializes `ofVideoGrabber` and selects a device by **index** (matching old GUI semantics).
- On Linux, prefers `ofGstVideoGrabber` so it can:
  - be quiet (`setVerbose(false)`)
  - fall back to a forced raw capture pipeline if frames never arrive.
- Tracks capture status:
  - `capturing`, `frameCount`, `lastFrameMs`, and the active V4L2 device id.

### Public API

- `setup()` / `close()`
  - `setup()` also sets `GST_V4L2_USE_LIBV4L2=1` to avoid problematic DMA formats.
- `setDeviceIndex(int requestedIndex)`
  - Picks the first available device if the requested one is not available.
- `resume()` / `pause()`
  - `resume()` resets timing counters and allows frame updates.
- `update()`
  - Calls `vidGrabber.update()` and increments `frameCount` when frames arrive.
  - Linux fallback: if initialized but no frames arrive for >2s, tries a forced raw YUY2 pipeline.
- `captureFrameToRGB(ofPixels& outRgb) const`
  - Copies the latest frame to RGB pixels:
    - prefers `vidGrabber.getTexture().readToPixels()` for guaranteed RGB conversion
    - falls back to CPU pixels and coerces to RGB if needed

### Linux/GStreamer fallback pipeline

`setupForcedRawYUY2()` builds a pipeline roughly equivalent to:

- `v4l2src device=/dev/videoX io-mode=2 ! video/x-raw,format=YUY2,width=...,height=...,framerate=.../1 ! videoconvert`

This avoids needing JPEG/H264 decoder plugins in constrained environments.

### Ownership/lifecycle

Owns `ofVideoGrabber vidGrabber`. `close()` shuts it down if initialized.

## Class: `AudioEngine`

**Location**: `src/AudioEngine.h`, `src/AudioEngine.cpp`  
**Role**: Owns the OF sound stream and bridges to an app-provided render callback.

### Responsibilities

- Configures `ofSoundStream` and selects an output device.
- Calls a user-supplied `std::function<void(ofSoundBuffer&)>` each audio callback.
- Provides device enumeration and switching (helpful on Linux/Pulse/Bluetooth sinks).

### Public API

- `setup(float sampleRate, int bufferSize, std::function<void(ofSoundBuffer&)> renderFn)`
  - Prints device list.
  - Tries to open an output stream with this API preference order:
    1. `ofSoundDevice::Api::PULSE`
    2. `ofSoundDevice::Api::DEFAULT`
    3. UNSPECIFIED fallback with no explicit device.
- `close()`
  - Closes the stream.
- `setRenderFn(...)`
  - Replaces the callback.
- Device helpers:
  - `getOutputDevices()`
  - `getOutputDeviceOptions()` (map of deviceID → human label)
  - `setOutputDeviceById(int deviceId)`
- `audioOut(ofSoundBuffer& buffer)` (override)
  - Calls the render callback, or fills silence when none exists.

### Ownership/lifecycle

Owns the `ofSoundStream stream`. The callback is invoked from the audio thread; app code should avoid heavy allocations or locking.

## Class: `ColumnSonifier`

**Location**: `src/ColumnSonifier.h`, `src/ColumnSonifier.cpp`  
**Role**: Converts one image column into audio via a bank of sine oscillators.

### Responsibilities

- For a given `columnX`, scans all rows `y`:
  - pixel brightness above `brightnessThreshold` activates an oscillator
  - `y` maps to a musical scale repeated across octaves, then mapped to `[minFreq..maxFreq]`
- Maintains per-row oscillator phases (`phases[y]`) so tones are continuous frame-to-frame.
- Writes audio as mono internally, then duplicates to stereo in the output buffer.

### Public API

- `setup(float sampleRate, int bufferSize)`
  - Sets the audio configuration and allocates the internal buffer.
- `setParams(float volume, float minFreq, float maxFreq)`
  - Controls loudness and frequency range mapping.
- `renderColumnToBuffer(const ofPixels& pixels, int imgWidth, int imgHeight, int columnX, ofSoundBuffer& out)`
  - Synthesizes audio into `out` (stereo).
  - If inputs are invalid, outputs silence.

### Frequency mapping

- A 6-note scale: `{0, 3, 5, 7, 10, 12}` semitones, repeated across 4 octaves
- `y` is normalized top→bottom, so top pixels map to higher pitches.
- Frequencies are computed from a MIDI base (C3-ish) and then mapped into `[minFreq..maxFreq]`.

### Normalization

- The sum of oscillators is normalized by \(1/\sqrt{N}\) where \(N\) is the number of active frequencies,
  to keep perceived loudness more stable as the number of active pixels changes.

## Class: `AnalogKnob`

**Location**: `src/AnalogKnob.h`, `src/AnalogKnob.cpp`  
**Role**: Represents one ADC channel as a periodically-polled, stepped parameter control.

### Responsibilities

- Holds a pointer to the shared `Mcp3008Spi` device and a `channel` \([0..7]\).
- Polls the ADC at a configurable period (`readPeriodMs`) and caches:
  - `lastRaw` (0..1023) or -1 if not read / error
- Converts raw readings to a mapped value:
  - linear interpolation from `[minValue..maxValue]`
  - optional quantization by `stepValue`
  - clamps to the range

### Public API

- Construction:
  - `AnalogKnob(int channel, float min, float step, float max, float defaultValue)`
- `setup(Mcp3008Spi* adc, int channel)` / `setup(Mcp3008Spi* adc)`
- `setReadPeriodMs(uint64_t ms)`
- `update(uint64_t nowMs)`
  - If enough time elapsed, reads raw via `adc->readChannelRaw(channel)`.
- `getRaw()`, `getChannel()`
- Mapping:
  - `setMapping(min, step, max, defaultValue)`
  - `getValue()` returns the mapped/quantized value (or default when raw is invalid).
  - `getDefaultValue()`

### Threading note

This is designed to be polled from the main thread (as `ofApp::update()` does).

## Class: `Mcp3008Spi`

**Location**: `src/Mcp3008Spi.h`, `src/Mcp3008Spi.cpp`  
**Role**: Linux SPI wrapper for the MCP3008 10-bit ADC.

### Responsibilities

- Opens and configures `/dev/spidevX.Y` once.
- Performs MCP3008 SPI transactions to read raw channel values.
- Optionally logs a “smoke test” using `libgpiod` to show whether SPI0 pins appear in use.

### Public API

- `bool setup(const std::string& spidevPath="/dev/spidev0.0", uint32_t speedHz=1000000, bool runGpiodSmokeTest=true)`
  - On non-Linux targets, logs a warning and returns false.
  - On Linux:
    - opens the device
    - sets SPI mode 0, 8 bits, max speed
    - returns true when ready.
- `void close()`
  - Closes the file descriptor on Linux.
- `bool isOpen() const`
- `int readChannelRaw(int channel)`
  - Returns 0..1023, or -1 on error/invalid channel.

### MCP3008 protocol summary (single-ended)

Transfers 3 bytes:

- `tx[0]=0x01` start bit
- `tx[1]=0x80 | (channel<<4)` single-ended + channel select
- `tx[2]=0x00`

And extracts the 10-bit result as:

- `((rx[1] & 0x03) << 8) | rx[2]`

## File: `main.cpp` (entry point)

**Location**: `src/main.cpp`  
**Role**: Configures environment + window settings, then starts OpenFrameworks with `ofApp`.

### Notes

- Sets several GStreamer environment variables intended to improve V4L2 behavior on ARM systems.
- Creates a fullscreen, undecorated, kiosk-like window and runs `ofApp`.


