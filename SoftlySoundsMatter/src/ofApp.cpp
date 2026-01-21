// ofApp is the orchestration layer:
// - input
// - delegates camera capture to VideoCaptureManager
// - delegates Sobel processing to ImageProcessor
// - delegates audio synthesis to ColumnSonifier
#include "ofApp.h"

#include "ofBitmapFont.h"

#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>

// Hardcoded GPIO buttons (Raspberry Pi GPIO BCM numbers).
// Wiring assumption: button between GPIO and GND (active-low) with pull-up enabled.
static constexpr const char *kGpioChipPath = "/dev/gpiochip0";
static constexpr int kBtn1Gpio = 17;
static constexpr int kBtn2Gpio = 27;
static constexpr bool kBtnActiveLow = true;
static constexpr bool kBtnPullUp = true;

ofApp::ofApp() {
	// Ensure runtime params start at the configured defaults (even before the first knob read).
	resetAllParametersToDefaults();
}

ofApp::~ofApp() {
	audio.close();
	video.close();
}

void ofApp::setup() {
	// Formerly owned by AppGui::setup() when the on-screen controls existed.
	ofSetFrameRate(60);
	ofBackground(0);
	// Avoid noisy subsystems (camera / GStreamer) spamming the console on embedded targets.
	ofSetLogLevel(OF_LOG_NOTICE);

	// Force "fill the desktop" behavior even on window managers that ignore the initial fullscreen request.
	ofSetFullscreen(true);
	ofSetWindowPosition(0, 0);
	// Try to match the primary monitor resolution.
	ofSetWindowShape(ofGetScreenWidth(), ofGetScreenHeight());
	ofHideCursor();
	ofLogNotice() << "Screen " << ofGetScreenWidth() << "x" << ofGetScreenHeight()
	              << " | Window " << ofGetWidth() << "x" << ofGetHeight()
	              << " | Mode " << (int)ofGetWindowMode();

	video.setup();
	image.setScaleFactor(0.25f);
	sonifier.setup(sampleRate, bufferSize);

	// Audio callback is owned by AudioEngine for consistency with Video/Image classes.
	// We provide a render function that uses current app state.
	audio.setup(sampleRate, bufferSize, [&](ofSoundBuffer & buffer) {
		if (video.isCapturing() || !image.hasProcessed()) {
			buffer.getBuffer().assign(buffer.getNumFrames() * buffer.getNumChannels(), 0.0f);
			return;
		}
		sonifier.setParams(params.volume, params.minFreq, params.maxFreq);
		const int imgX = getImageXFromPlayhead();
		sonifier.renderColumnToBuffer(image.getSobelPixels(), image.getWidth(), image.getHeight(), imgX, buffer);
	});

	mcp3008.setup("/dev/spidev0.0", /*speedHz*/ 1000000, /*runGpiodSmokeTest*/ true);
	for (int i = 0; i < (int)knobs.size(); i++) {
		knobs[(size_t)i].setup(&mcp3008);
		knobs[(size_t)i].setReadPeriodMs(200);
	}

	// Direct GPIO buttons (hardcoded pins).
	(void)btn1.setup(kGpioChipPath, kBtn1Gpio, kBtnActiveLow, kBtnPullUp);
	(void)btn2.setup(kGpioChipPath, kBtn2Gpio, kBtnActiveLow, kBtnPullUp);
}

void ofApp::update() {
	// Update capture status + frames
	video.update();

	const uint64_t nowMs = ofGetElapsedTimeMillis();
	for (auto & k : knobs) k.update(nowMs);
	btn1.update(nowMs);
	btn2.update(nowMs);

	// Optional: one-line terminal debug output for raw knob values.
	// Enable by compiling with -DSSM_DEBUG_KNOBS=1.
#if defined(SSM_DEBUG_KNOBS) && SSM_DEBUG_KNOBS
	std::cout << "\033[2K\r[mcp3008] ";
	for (int i = 0; i < (int)knobs.size(); i++) {
		std::cout << "CH" << i << "=" << knobs[(size_t)i].getRaw();
		if (i != (int)knobs.size() - 1) std::cout << "  ";
	}
	std::cout << std::flush;
#endif

	// Former GUI sliders → physical knobs (MCP3008 CH0..CH5).
	// Each mapping is defined as (min, step, max) and uses stepped quantization.
	//
	// CH0: Contrast        [0.2 .. 3.0], step 0.01
	// CH1: Exposure        [-1.0 .. 1.0], step 0.01
	// CH2: Sobel Strength  [0.1 .. 5.0], step 0.01
	// CH3: Playhead Speed  [-600 .. 600], step 1
	// CH4: Volume          [0 .. 1], step 0.01
	// CH5: Max Frequency   [1000 .. 10000], step 10
	//
	// Note: minFreq stays fixed; only maxFreq is knob-controlled to match the old GUI.
	// Only apply knob→parameter updates when we're NOT in live preview.
	// Rationale: during preview (camera feed), audio is muted and there is no processed image to "play".
	if (!video.isCapturing()) {
		// Each knob is configured at construction; after a reset we latch until the knob moves.
		for (int i = 0; i < (int)knobs.size(); i++) {
			const int raw = knobs[(size_t)i].getRaw();
			if (!knobUnlatched[(size_t)i] && raw >= 0) {
				if (std::abs(raw - knobLatchRaw[(size_t)i]) > kKnobLatchDeadbandRaw) {
					knobUnlatched[(size_t)i] = true;
				}
			}
		}

		if (knobUnlatched[0]) params.contrast = knobs[0].getValue();           // Contrast
		if (knobUnlatched[1]) params.exposure = knobs[1].getValue();           // Exposure
		if (knobUnlatched[2]) params.sobelStrength = knobs[2].getValue();      // Sobel Strength
		if (knobUnlatched[3]) params.playheadSpeed = knobs[3].getValue();      // Playhead Speed
		if (knobUnlatched[4]) params.volume = knobs[4].getValue();             // Volume
		if (knobUnlatched[5]) params.maxFreq = knobs[5].getValue();            // Max Frequency
	}

	// Button mappings (edge-triggered):
	// - BTN1 pressed: same as Space (toggle preview/playback)
	// - BTN2 pressed: reset all params (same as 'R')
	if (btn1.consumePressed()) {
		if (video.isCapturing()) {
			ofPixels rgb;
			if (video.captureFrameToRGB(rgb)) {
				image.setSourceRGB(rgb);
				video.pause();
			}
		} else {
			video.resume();
		}
	}
	if (btn2.consumePressed()) {
		resetAllParametersToDefaults();
	}

	// Update processing params and process if dirty
	image.setParams(params.contrast, params.exposure, params.sobelStrength);
	image.update();

	// Update playhead only when scanning a processed image (not while live capture)
	if (image.hasProcessed() && !video.isCapturing()) {
		updatePlayheadPosition();
	}
}

void ofApp::updatePlayheadPosition() {
	const float canvasW = std::max(1.0f, (float)ofGetWidth());
	playheadX += params.playheadSpeed * ofGetLastFrameTime();
	if (playheadX > canvasW) {
		playheadX = 0;
	} else if (playheadX < 0) {
		playheadX = canvasW;
	}
}

ofApp::DrawTransform ofApp::getProcessedTransform() const {
	DrawTransform t;
	if (!image.hasProcessed()) return t;

	const float windowW = std::max(1.0f, (float)ofGetWidth());
	const float windowH = std::max(1.0f, (float)ofGetHeight());
	t.scale = image.calculateDrawScale(windowW, windowH);

	const float contentW = image.getWidth() * t.scale;
	const float contentH = image.getHeight() * t.scale;
	t.offsetX = (windowW - contentW) * 0.5f;
	t.offsetY = (windowH - contentH) * 0.5f;
	return t;
}

int ofApp::getImageXFromPlayhead() const {
	if (!image.hasProcessed()) return 0;
	const auto t = getProcessedTransform();
	const int imgX = (int)((playheadX - t.offsetX) / std::max(1e-6f, t.scale));
	return ofClamp(imgX, 0, image.getWidth() - 1);
}

void ofApp::draw() {
	if (video.isCapturing()) {
		drawVideoPreview();
	} else if (image.hasProcessed()) {
		drawProcessedView();
		drawStatusOverlay();
	}
}

void ofApp::drawVideoPreview() {
	if (video.isGrabberPipelineUp()) {
		const float windowW = std::max(1.0f, (float)ofGetWidth());
		const float windowH = std::max(1.0f, (float)ofGetHeight());

		const float vw = std::max(1.0f, (float)video.grabber().getWidth());
		const float vh = std::max(1.0f, (float)video.grabber().getHeight());
		// Cover the window (fill + crop) to avoid letterboxing gaps.
		const float videoScale = std::max(windowW / vw, windowH / vh);
		const float offsetX = (windowW - vw * videoScale) * 0.5f;
		const float offsetY = (windowH - vh * videoScale) * 0.5f;

		ofPushMatrix();
		ofTranslate(offsetX, offsetY);
		ofScale(videoScale, videoScale);
		ofSetColor(255);
		video.grabber().draw(0, 0);
		ofPopMatrix();

		ofSetColor(0, 255, 0);
		return;
	}

	ofSetColor(255, 80, 80);
}

void ofApp::drawProcessedView() {
	const auto t = getProcessedTransform();
	drawScale = t.scale;

	// Draw processed image
	ofPushMatrix();
	ofTranslate(t.offsetX, t.offsetY);
	ofScale(drawScale, drawScale);
	ofSetColor(255);
	image.getSobelImage().draw(0, 0);
	ofPopMatrix();

	// Playhead
	ofSetColor(255, 0, 0);
	ofDrawLine(playheadX, 0, playheadX, ofGetHeight());

	// Visualize active frequencies at current column
	const int imgX = getImageXFromPlayhead();
	const ofPixels & pix = image.getSobelPixels();
	for (int y = 0; y < image.getHeight(); y++) {
		const float b = pix[y * image.getWidth() + imgX] / 255.0f;
		if (b > 0.1f) {
			const float screenY = t.offsetY + y * drawScale;
			ofSetColor(0, 255, 0, b * 255);
			ofDrawCircle(playheadX, screenY, 3);
		}
	}
}

void ofApp::drawStatusOverlay() {
	// Bottom-right parameter HUD (always visible).
	std::ostringstream ss;
	ss << std::fixed << std::setprecision(2);
	ss << "contrast: " << params.contrast << "\n";
	ss << "exposure: " << params.exposure << "\n";
	ss << "sobel:    " << params.sobelStrength << "\n";
	ss << std::setprecision(0) << "speed:    " << params.playheadSpeed << "\n";
	ss << std::setprecision(2) << "volume:   " << params.volume << "\n";
	ss << std::setprecision(0) << "maxFreq:  " << params.maxFreq << "\n";
	ss << "mode:     " << (video.isCapturing() ? "preview" : "playback");

	const std::string text = ss.str();
	const int pad = 12;
	static ofBitmapFont font;
	const ofRectangle bb = font.getBoundingBox(text, 0, 0);
	const float x = std::max(0.0f, (float)ofGetWidth() - pad - bb.width);
	const float y = std::max(0.0f, (float)ofGetHeight() - pad - bb.height);

	ofSetColor(255);
	ofDrawBitmapStringHighlight(text, x, y);
}

void ofApp::keyPressed(int key) {
	switch (key) {
	case ' ':
		// Toggle capture vs scanning a frozen frame
		if (video.isCapturing()) {
			ofPixels rgb;
			if (video.captureFrameToRGB(rgb)) {
				image.setSourceRGB(rgb);
				video.pause();
			}
		} else {
			video.resume();
		}
		break;
	case 'r':
	case 'R':
		resetAllParametersToDefaults();
		break;
	case 'p':
	case 'P':
		togglePlayback();
		break;
	}
}

void ofApp::resetImageParameters() {
	params.contrast = 1.0f;
	params.exposure = 0.0f;
	params.sobelStrength = 1.0f;
	image.setParams(params.contrast, params.exposure, params.sobelStrength);
}

void ofApp::resetAllParametersToDefaults() {
	params.contrast = knobs[0].getDefaultValue();
	params.exposure = knobs[1].getDefaultValue();
	params.sobelStrength = knobs[2].getDefaultValue();
	params.playheadSpeed = knobs[3].getDefaultValue();
	params.volume = knobs[4].getDefaultValue();
	params.maxFreq = knobs[5].getDefaultValue();

	// Also keep dependent state consistent.
	lastPlayheadSpeed = params.playheadSpeed;
	image.setParams(params.contrast, params.exposure, params.sobelStrength);

	// Latch knobs at their current positions to prevent immediate snap-back to the pre-reset state.
	for (int i = 0; i < (int)knobs.size(); i++) {
		knobLatchRaw[(size_t)i] = knobs[(size_t)i].getRaw();
		knobUnlatched[(size_t)i] = false;
	}
}

void ofApp::togglePlayback() {
	if (params.playheadSpeed != 0.0f) {
		lastPlayheadSpeed = params.playheadSpeed;
		params.playheadSpeed = 0.0f;
	} else {
		params.playheadSpeed = (lastPlayheadSpeed != 0.0f) ? lastPlayheadSpeed : 120.0f;
	}
}


