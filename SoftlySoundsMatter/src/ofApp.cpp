#include "ofApp.h"

//--------------------------------------------------------------
ofApp::~ofApp() {
	soundStream.close();
}

//--------------------------------------------------------------
void ofApp::setup() {
	ofSetFrameRate(60);
	ofBackground(0);

	// GUI setup
	gui.setup();
	gui.add(contrast.setup("Contrast", 1.0f, 0.2f, 3.0f));
	gui.add(exposure.setup("Exposure", 0.0f, -1.0f, 1.0f));
	gui.add(sobelStrength.setup("Sobel Kernel", 1.0f, 0.1f, 5.0f));
	gui.add(playheadSpeed.setup("Playhead Speed", 120.0f, -600.0f, 600.0f));
	gui.add(volume.setup("Volume", 0.5f, 0.0f, 1.0f));
	gui.add(minFreqSlider.setup("Min Frequency", 100.0f, 20.0f, 1000.0f));
	gui.add(maxFreqSlider.setup("Max Frequency", 4000.0f, 1000.0f, 10000.0f));

	// Audio setup
	soundStream.printDeviceList();
	ofSoundStreamSettings settings;
	settings.setOutListener(this);
	settings.sampleRate = sampleRate;
	settings.numOutputChannels = 2;
	settings.numInputChannels = 0;
	settings.bufferSize = bufferSize;
	soundStream.setup(settings);

	audioBuffer.assign(bufferSize, 0.0f);

	loadImage("c03.jpeg");
}

//--------------------------------------------------------------
void ofApp::loadImage(const std::string & path) {
	if (!original.load(path)) {
		ofLogError() << "No se pudo cargar la imagen";
		return;
	}
	int w = original.getWidth() * scaleFactor;
	int h = original.getHeight() * scaleFactor;
	graySmall.allocate(w, h, OF_IMAGE_GRAYSCALE);
	sobelImg.allocate(w, h, OF_IMAGE_GRAYSCALE);
	imageDirty = true;
}

//--------------------------------------------------------------
void ofApp::processImage() {
	ofPixels resized;
	resized.allocate(graySmall.getWidth(), graySmall.getHeight(), OF_PIXELS_RGB);
	original.getPixels().resizeTo(resized);
	graySmall.setFromPixels(resized);
	graySmall.setImageType(OF_IMAGE_GRAYSCALE);

	ofPixels & pix = graySmall.getPixels();
	for (auto & p : pix) {
		float v = p / 255.0f;
		v += exposure;
		v = (v - 0.5f) * contrast + 0.5f;
		p = ofClamp(v * 255.0f, 0, 255);
	}
	graySmall.update();

	applySobel(graySmall.getPixels(), sobelImg.getPixels());
	sobelImg.update();
}

//--------------------------------------------------------------
void ofApp::applySobel(const ofPixels & src, ofPixels & dst) {
	dst.set(0);
	int w = src.getWidth();
	int h = src.getHeight();

	for (int y = 1; y < h - 1; y++) {
		for (int x = 1; x < w - 1; x++) {
			int i = y * w + x;
			int gx = -src[i - w - 1] + src[i - w + 1] + -2 * src[i - 1] + 2 * src[i + 1] + -src[i + w - 1] + src[i + w + 1];
			int gy = -src[i - w - 1] - 2 * src[i - w] - src[i - w + 1] + src[i + w - 1] + 2 * src[i + w] + src[i + w + 1];
			float mag = (abs(gx) + abs(gy)) * sobelStrength;
			dst[i] = ofClamp(mag, 0, 255);
		}
	}
}

//--------------------------------------------------------------
void ofApp::audioOut(ofSoundBuffer & buffer) {
	if (!sobelImg.isAllocated()) {
		buffer.getBuffer().assign(buffer.getNumFrames() * buffer.getNumChannels(), 0.0f);
		return;
	}

	// Get the column of pixels at playhead position
	int imgX = (int)((playheadX / ofGetWidth()) * sobelImg.getWidth() * drawScale);
	imgX = ofClamp(imgX, 0, sobelImg.getWidth() - 1);

	// Resize phases vector if needed
	int height = sobelImg.getHeight();
	if (phases.size() != height) {
		phases.resize(height, 0.0f);
	}

	// Clear audio buffer
	audioBuffer.assign(bufferSize, 0.0f);

	// Read the vertical line of pixels and synthesize
	ofPixels & pixels = sobelImg.getPixels();
	int activeFrequencies = 0;

	for (int y = 0; y < height; y++) {
		// Get pixel brightness (0-255)
		int pixelIndex = y * sobelImg.getWidth() + imgX;
		float brightness = pixels[pixelIndex] / 255.0f;

		// Only synthesize if pixel is bright enough (edge detected)
		if (brightness > 0.1f) {
			activeFrequencies++;

			// Map Y position to frequency (inverted - top = high freq, bottom = low freq)
			float normalizedY = 1.0f - (float)y / (float)(height - 1);
			float freq = ofMap(normalizedY, 0.0f, 1.0f, minFreqSlider, maxFreqSlider);

			// Generate sine wave for this frequency
			float phaseInc = (freq / sampleRate) * TWO_PI;

			for (int i = 0; i < bufferSize; i++) {
				audioBuffer[i] += sin(phases[y]) * brightness * volume;
				phases[y] += phaseInc;
				if (phases[y] >= TWO_PI) phases[y] -= TWO_PI;
			}
		}
	}

	// Normalize output to prevent clipping
	if (activeFrequencies > 0) {
		float normalizationFactor = 1.0f / sqrt(activeFrequencies);
		for (int i = 0; i < bufferSize; i++) {
			audioBuffer[i] *= normalizationFactor;
		}
	}

	// Fill the sound buffer (stereo)
	for (size_t i = 0; i < buffer.getNumFrames(); i++) {
		float sample = audioBuffer[i];
		buffer.getBuffer()[i * 2] = sample; // Left channel
		buffer.getBuffer()[i * 2 + 1] = sample; // Right channel
	}
}

//--------------------------------------------------------------
void ofApp::update() {
	if (contrast != lastContrast || exposure != lastExposure || sobelStrength != lastSobel) {
		imageDirty = true;
		lastContrast = contrast;
		lastExposure = exposure;
		lastSobel = sobelStrength;
	}

	if (imageDirty && original.isAllocated()) {
		processImage();
		imageDirty = false;
	}

	// Update frequency range
	minFreq = minFreqSlider;
	maxFreq = maxFreqSlider;

	playheadX += playheadSpeed * ofGetLastFrameTime();
	if (playheadX > ofGetWidth()) playheadX = 0;
	if (playheadX < 0) playheadX = ofGetWidth();
}

//--------------------------------------------------------------
void ofApp::draw() {
	if (!sobelImg.isAllocated()) return;

	float sx = (float)ofGetWidth() / sobelImg.getWidth();
	float sy = (float)ofGetHeight() / sobelImg.getHeight();
	drawScale = std::min(sx, sy);

	// MOSTRAMOS SOBEL (BORDES)
	ofPushMatrix();
	ofScale(drawScale, drawScale);
	ofSetColor(255);
	sobelImg.draw(0, 0);
	ofPopMatrix();

	// Playhead rojo
	ofSetColor(255, 0, 0);
	ofDrawLine(playheadX, 0, playheadX, ofGetHeight());

	// Visual feedback for active frequencies
	if (sobelImg.isAllocated()) {
		int imgX = (int)((playheadX / ofGetWidth()) * sobelImg.getWidth() * drawScale);
		imgX = ofClamp(imgX, 0, sobelImg.getWidth() - 1);

		ofPixels & pixels = sobelImg.getPixels();
		for (int y = 0; y < sobelImg.getHeight(); y++) {
			int pixelIndex = y * sobelImg.getWidth() + imgX;
			float brightness = pixels[pixelIndex] / 255.0f;

			if (brightness > 0.1f) {
				float screenY = y * drawScale;
				ofSetColor(0, 255, 0, brightness * 255);
				ofDrawCircle(playheadX, screenY, 3);
			}
		}
	}

	gui.draw();
}

//--------------------------------------------------------------
void ofApp::windowResized(int w, int h) {
	gui.setPosition(w - gui.getWidth() - 20, 20);
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key) {
	switch (key) {
	case 'r':
	case 'R':
		contrast = 1.0f;
		exposure = 0.0f;
		sobelStrength = 1.0f;
		imageDirty = true;
		break;
	case 'o':
	case 'O': {
		ofFileDialogResult res = ofSystemLoadDialog("Cargar imagen");
		if (res.bSuccess) {
			loadImage(res.getPath());
		}
		break;
	}
	case ' ':
		// Toggle playback with spacebar
		playheadSpeed = (playheadSpeed == 0) ? 120.0f : 0.0f;
		break;
	}
}
