#pragma once
#include "ofMain.h"

#include "AudioEngine.h"
#include "ColumnSonifier.h"
#include "ImageProcessor.h"
#include "VideoCaptureManager.h"
#include "AnalogKnob.h"
#include "Mcp3008Spi.h"
#include "GpioButton.h"

#include <array>
#include <cstdint>


class ofApp : public ofBaseApp {
public:
	ofApp();
	~ofApp();
	void setup();
	void update();
	void draw();
	void keyPressed(int key);

private:
	// Former GUI-controlled parameters (now headless / no on-screen widgets).
	struct Params {
		float contrast = 1.0f;
		float exposure = 0.0f;
		float sobelStrength = 1.0f;

		float playheadSpeed = 120.0f;
		float volume = 0.5f;
		float minFreq = 100.0f;
		float maxFreq = 4000.0f;
	};

	struct DrawTransform {
		float scale = 1.0f;
		float offsetX = 0.0f;
		float offsetY = 0.0f;
	};

	DrawTransform getProcessedTransform() const;

	void updatePlayheadPosition();
	int getImageXFromPlayhead() const;

	void drawVideoPreview();
	void drawProcessedView();
	void drawStatusOverlay();

	void resetImageParameters();
	void resetAllParametersToDefaults();
	void togglePlayback();

	// Subsystems
	AudioEngine audio;
	VideoCaptureManager video;
	ImageProcessor image;
	ColumnSonifier sonifier;

	float sampleRate = 44100;
	int bufferSize = 512;

	Params params;
	float lastPlayheadSpeed = 120.0f;

	// Playhead
	float playheadX = 0.0f;

	// Drawing
	float drawScale = 1.0f;

	// MCP3008 (shared SPI device) + 6 knob instances (CH0..CH5)
	Mcp3008Spi mcp3008;
	std::array<AnalogKnob, 6> knobs = {
		AnalogKnob(0, 0.2f, 0.01f, 3.0f, 1.0f),         // Contrast
		AnalogKnob(1, -1.0f, 0.01f, 1.0f, 0.0f),        // Exposure
		AnalogKnob(2, 0.1f, 0.01f, 5.0f, 1.0f),         // Sobel Strength
		AnalogKnob(3, -600.0f, 1.0f, 600.0f, 120.0f),   // Playhead Speed
		AnalogKnob(4, 0.0f, 0.01f, 1.0f, 0.5f),         // Volume
		AnalogKnob(5, 1000.0f, 10.0f, 10000.0f, 4000.0f) // Max Frequency
	};

	// After resetting to defaults, we "latch" each parameter until the physical knob moves
	// far enough from its reset position (prevents immediate snap-back).
	static constexpr int kKnobLatchDeadbandRaw = 8; // MCP3008 raw units (0..1023)
	std::array<int, 6> knobLatchRaw = {0, 0, 0, 0, 0, 0};
	std::array<bool, 6> knobUnlatched = {true, true, true, true, true, true};

	// Two direct GPIO buttons (Pi header) - configured at runtime via env vars.
	GpioButton btn1;
	GpioButton btn2;
};
