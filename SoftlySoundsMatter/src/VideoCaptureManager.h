#pragma once

#include "ofMain.h"

#include "ofGstVideoGrabber.h"

#include <cstdint>
#include <string>

// Handles camera device selection, robust initialization, frame updates and status reporting.
// Keeps the same "GUI overlay" signals ofApp expects: status text, device index/id, frame counters, etc.
class VideoCaptureManager {
public:
	/// Initialize capture subsystem and attempt to start the current `deviceIndex`.
	void setup();
	/// Close the underlying grabber/pipeline if initialized.
	void close();

	// Device selection is by index into listDevices() (matches GUI slider semantics).
	/// Set the requested device index (index into `ofVideoGrabber::listDevices()`), with fallback to the first available.
	bool setDeviceIndex(int requestedIndex);
	/// Get the current device index (index into `listDevices()`).
	int getDeviceIndex() const { return deviceIndex; }

	// Capture control
	/// True when capturing is enabled (preview mode).
	bool isCapturing() const { return capturing; }
	/// Resume capture (preview). Resets timing counters.
	void resume();
	/// Pause capture (playback mode). `update()` will stop pulling frames.
	void pause();

	// Call each frame while capturing to pull frames from the grabber.
	/// Update the grabber. On Linux, may auto-fallback to a forced raw pipeline when the device initializes but yields no frames.
	void update();

	// Snapshot: copies the latest frame to RGB pixels (prefers texture readback).
	// Returns false if no new frame was available.
	/// Copy the latest frame to RGB pixels. Prefers texture readback for deterministic RGB conversion.
	bool captureFrameToRGB(ofPixels & outRgb) const;

	// Access for drawing
	/// Mutable access to the underlying `ofVideoGrabber` for drawing/inspection.
	ofVideoGrabber & grabber() { return vidGrabber; }
	/// Const access to the underlying `ofVideoGrabber` for drawing/inspection.
	const ofVideoGrabber & grabber() const { return vidGrabber; }

	// Status helpers (used by GUI overlay)
	/// True when the grabber reports it has an initialized pipeline/backend.
	bool isGrabberPipelineUp() const;
	/// True when the grabber texture is ready (often only after the first received frame).
	bool isGrabberTextureReady() const;
	/// Count of frames received since last `resume()` / init.
	uint64_t getFrameCount() const { return frameCount; }
	/// Timestamp (ms) when the last new frame was received (0 if none yet).
	uint64_t getLastFrameMs() const { return lastFrameMs; }
	/// Active OS device id (often V4L2 `/dev/video{id}` on Linux), or -1 if not set.
	int getActiveVideoDeviceId() const { return activeVideoDeviceId; }
	/// Current capture width (reported by the grabber when available).
	int getWidth() const { return camWidth; }
	/// Current capture height (reported by the grabber when available).
	int getHeight() const { return camHeight; }

private:
	// Defaults tuned for Linux/V4L2
	int camWidth = 640;
	int camHeight = 480;
	int camFps = 30;

	// GUI uses index into listDevices()
	int deviceIndex = 0;
	int activeVideoDeviceId = -1; // V4L2 id (often /dev/video{id})

	bool capturing = true;

	uint64_t camInitMs = 0;
	uint64_t lastFrameMs = 0;
	uint64_t frameCount = 0;

	ofVideoGrabber vidGrabber;

	/// Reset timing counters used for frame-timeouts and UI reporting.
	void resetTiming();
	/// Initialize capture from a device list index, with availability checks.
	bool initFromIndex(int requestedIndex);
	/// Initialize capture by OS device id (e.g. V4L2 id on Linux).
	bool initFromDeviceId(int deviceId);
	/// Linux-only: attempt a forced raw YUY2 pipeline when normal initialization succeeds but no frames arrive.
	bool setupForcedRawYUY2(int deviceId, int w, int h, int fps);
};



