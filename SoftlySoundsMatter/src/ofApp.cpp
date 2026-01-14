/**
 * @file ofApp.cpp
 * @brief Image-to-Sound Synthesizer Application
 * 
 * This application converts images into sound by:
 * 1. Processing images with Sobel edge detection
 * 2. Scanning columns of pixels with a moving playhead
 * 3. Converting pixel brightness values to audio frequencies
 * 4. Synthesizing audio in real-time based on the scanned data
 * 
 * The brighter the pixel, the louder the corresponding frequency.
 * Vertical position maps to frequency (top = high freq, bottom = low freq).
 * 
 * @author [Your Name]
 * @date [Current Date]
 */

#include "ofApp.h"

//--------------------------------------------------------------
/**
 * @brief Destructor - ensures audio stream is properly closed
 */
ofApp::~ofApp() {
	soundStream.close();
}

//--------------------------------------------------------------
/**
 * @brief Main setup function - initializes all components
 * 
 * Sets up graphics, GUI controls, audio system, and loads default image
 */
void ofApp::setup() {
	setupGraphics();
	setupGUI();
	setupAudio();
	loadImage("c03.jpeg");
}

//--------------------------------------------------------------
/**
 * @brief Configure graphics settings
 * 
 * Sets frame rate to 60fps and black background
 */
void ofApp::setupGraphics() {
	ofSetFrameRate(60);
	ofBackground(0);
}

//--------------------------------------------------------------
/**
 * @brief Initialize GUI controls
 * 
 * Creates sliders for:
 * - Contrast: Adjusts image contrast (0.2 to 3.0)
 * - Exposure: Adjusts image brightness (-1.0 to 1.0)
 * - Sobel Kernel: Controls edge detection strength (0.1 to 5.0)
 * - Playhead Speed: Controls scanning speed (-600 to 600 pixels/sec)
 * - Volume: Master volume control (0.0 to 1.0)
 * - Min/Max Frequency: Frequency range for synthesis (20Hz to 10kHz)
 */
void ofApp::setupGUI() {
	gui.setup();
	gui.add(contrast.setup("Contrast", 1.0f, 0.2f, 3.0f));
	gui.add(exposure.setup("Exposure", 0.0f, -1.0f, 1.0f));
	gui.add(sobelStrength.setup("Sobel Kernel", 1.0f, 0.1f, 5.0f));
	gui.add(playheadSpeed.setup("Playhead Speed", 120.0f, -600.0f, 600.0f));
	gui.add(volume.setup("Volume", 0.5f, 0.0f, 1.0f));
	gui.add(minFreqSlider.setup("Min Frequency", 100.0f, 20.0f, 1000.0f));
	gui.add(maxFreqSlider.setup("Max Frequency", 4000.0f, 1000.0f, 10000.0f));
}

//--------------------------------------------------------------
/**
 * @brief Configure audio output system
 * 
 * Sets up stereo output with specified sample rate and buffer size
 */
void ofApp::setupAudio() {
	soundStream.printDeviceList();

	ofSoundStreamSettings settings;
	settings.setOutListener(this);
	settings.sampleRate = sampleRate;
	settings.numOutputChannels = 2;
	settings.numInputChannels = 0;
	settings.bufferSize = bufferSize;

	soundStream.setup(settings);
	audioBuffer.assign(bufferSize, 0.0f);
}

//--------------------------------------------------------------
/**
 * @brief Load and prepare an image file
 * 
 * @param path File path to the image
 * 
 * Loads image, allocates processing buffers, and marks for processing
 */
void ofApp::loadImage(const std::string & path) {
	if (!original.load(path)) {
		ofLogError() << "Could not load image: " << path;
		return;
	}

	allocateProcessedImages();
	imageDirty = true;
}

//--------------------------------------------------------------
/**
 * @brief Allocate memory for processed image buffers
 * 
 * Creates scaled grayscale and edge-detected versions
 */
void ofApp::allocateProcessedImages() {
	int w = original.getWidth() * scaleFactor;
	int h = original.getHeight() * scaleFactor;

	graySmall.allocate(w, h, OF_IMAGE_GRAYSCALE);
	sobelImg.allocate(w, h, OF_IMAGE_GRAYSCALE);
}

//--------------------------------------------------------------
/**
 * @brief Main image processing pipeline
 * 
 * Converts to grayscale, applies adjustments, and detects edges
 */
void ofApp::processImage() {
	resizeToGrayscale();
	applyImageAdjustments();
	applySobelFilter();
}

//--------------------------------------------------------------
/**
 * @brief Convert color image to scaled grayscale
 * 
 * Resizes image by scaleFactor and converts to grayscale
 */
void ofApp::resizeToGrayscale() {
	ofPixels resized;
	resized.allocate(graySmall.getWidth(), graySmall.getHeight(), OF_PIXELS_RGB);
	original.getPixels().resizeTo(resized);

	graySmall.setFromPixels(resized);
	graySmall.setImageType(OF_IMAGE_GRAYSCALE);
}

//--------------------------------------------------------------
/**
 * @brief Apply contrast and exposure adjustments
 * 
 * Modifies pixel values based on GUI parameters
 */
void ofApp::applyImageAdjustments() {
	ofPixels & pix = graySmall.getPixels();

	for (auto & p : pix) {
		float v = p / 255.0f;
		v += exposure; // Apply exposure
		v = (v - 0.5f) * contrast + 0.5f; // Apply contrast
		p = ofClamp(v * 255.0f, 0, 255);
	}

	graySmall.update();
}

//--------------------------------------------------------------
/**
 * @brief Apply Sobel edge detection filter
 * 
 * Detects edges in the grayscale image
 */
void ofApp::applySobelFilter() {
	applySobel(graySmall.getPixels(), sobelImg.getPixels());
	sobelImg.update();
}

//--------------------------------------------------------------
/**
 * @brief Core Sobel edge detection algorithm
 * 
 * @param src Source grayscale pixels
 * @param dst Destination edge-detected pixels
 * 
 * Applies 3x3 Sobel kernels for X and Y gradients
 */
void ofApp::applySobel(const ofPixels & src, ofPixels & dst) {
	dst.set(0);

	int w = src.getWidth();
	int h = src.getHeight();

	for (int y = 1; y < h - 1; y++) {
		for (int x = 1; x < w - 1; x++) {
			int magnitude = calculateSobelMagnitude(src, x, y, w);
			dst[y * w + x] = ofClamp(magnitude, 0, 255);
		}
	}
}

//--------------------------------------------------------------
/**
 * @brief Calculate Sobel gradient magnitude at a pixel
 * 
 * @param src Source pixels
 * @param x X coordinate
 * @param y Y coordinate
 * @param width Image width
 * @return Gradient magnitude (0-255)
 * 
 * Combines horizontal and vertical gradients
 */
int ofApp::calculateSobelMagnitude(const ofPixels & src, int x, int y, int width) {
	int i = y * width + x;

	// Sobel X kernel: detects vertical edges
	int gx = -src[i - width - 1] + src[i - width + 1] + -2 * src[i - 1] + 2 * src[i + 1] + -src[i + width - 1] + src[i + width + 1];

	// Sobel Y kernel: detects horizontal edges
	int gy = -src[i - width - 1] - 2 * src[i - width] - src[i - width + 1] + src[i + width - 1] + 2 * src[i + width] + src[i + width + 1];

	return (abs(gx) + abs(gy)) * sobelStrength;
}

//--------------------------------------------------------------
/**
 * @brief Audio callback - generates sound from image data
 * 
 * @param buffer Output audio buffer to fill
 * 
 * Called by audio system to generate real-time audio
 */
void ofApp::audioOut(ofSoundBuffer & buffer) {
	if (!sobelImg.isAllocated()) {
		clearAudioBuffer(buffer);
		return;
	}

	int imgX = getImageXFromPlayhead();
	ensurePhasesVectorSize();
	synthesizeAudioFromColumn(imgX);
	fillSoundBuffer(buffer);
}

//--------------------------------------------------------------
/**
 * @brief Clear audio buffer with silence
 * 
 * @param buffer Buffer to clear
 */
void ofApp::clearAudioBuffer(ofSoundBuffer & buffer) {
	buffer.getBuffer().assign(buffer.getNumFrames() * buffer.getNumChannels(), 0.0f);
}

//--------------------------------------------------------------
/**
 * @brief Convert playhead screen position to image coordinates
 * 
 * @return X coordinate in image space
 */
int ofApp::getImageXFromPlayhead() {
	float currentDrawScale = calculateDrawScale();
	int imgX = (int)(playheadX / currentDrawScale);
	return ofClamp(imgX, 0, sobelImg.getWidth() - 1);
}

//--------------------------------------------------------------
/**
 * @brief Calculate scale factor to fit image in window
 * 
 * @return Scale factor maintaining aspect ratio
 */
float ofApp::calculateDrawScale() {
	float sx = (float)ofGetWidth() / sobelImg.getWidth();
	float sy = (float)ofGetHeight() / sobelImg.getHeight();
	return std::min(sx, sy);
}

//--------------------------------------------------------------
/**
 * @brief Ensure phase array matches image height
 * 
 * Each row needs its own phase accumulator for synthesis
 */
void ofApp::ensurePhasesVectorSize() {
	int height = sobelImg.getHeight();
	if (phases.size() != height) {
		phases.resize(height, 0.0f);
	}
}

//--------------------------------------------------------------
/**
 * @brief Generate audio from a single column of pixels
 * 
 * @param columnX Column index to synthesize
 * 
 * Each bright pixel generates a sine wave at a specific frequency
 */
void ofApp::synthesizeAudioFromColumn(int columnX) {
	audioBuffer.assign(bufferSize, 0.0f);

	ofPixels & pixels = sobelImg.getPixels();
	int activeFrequencies = 0;
	int height = sobelImg.getHeight();

	for (int y = 0; y < height; y++) {
		float brightness = getPixelBrightness(pixels, columnX, y);

		if (brightness > 0.1f) { // Threshold to reduce noise
			activeFrequencies++;
			addFrequencyToBuffer(y, brightness, height);
		}
	}

	normalizeAudioBuffer(activeFrequencies);
}

//--------------------------------------------------------------
/**
 * @brief Get normalized brightness value at pixel
 * 
 * @param pixels Pixel data
 * @param x X coordinate
 * @param y Y coordinate
 * @return Brightness value (0.0 to 1.0)
 */
float ofApp::getPixelBrightness(ofPixels & pixels, int x, int y) {
	int pixelIndex = y * sobelImg.getWidth() + x;
	return pixels[pixelIndex] / 255.0f;
}

//--------------------------------------------------------------
/**
 * @brief Add a sine wave to the audio buffer
 * 
 * @param y Row index (determines frequency)
 * @param brightness Amplitude multiplier
 * @param totalHeight Image height for frequency mapping
 * 
 * Generates sine wave with frequency based on vertical position
 */
void ofApp::addFrequencyToBuffer(int y, float brightness, int totalHeight) {
	float freq = calculateFrequencyFromY(y, totalHeight);
	float phaseInc = (freq / sampleRate) * TWO_PI;

	for (int i = 0; i < bufferSize; i++) {
		audioBuffer[i] += sin(phases[y]) * brightness * volume;
		phases[y] += phaseInc;

		if (phases[y] >= TWO_PI) phases[y] -= TWO_PI; // Wrap phase
	}
}

//--------------------------------------------------------------
/**
 * @brief Map vertical position to frequency
 * 
 * @param y Row index
 * @param totalHeight Image height
 * @return Frequency in Hz
 * 
 * Top of image = high frequency, bottom = low frequency
 */
float ofApp::calculateFrequencyFromY(int y, int totalHeight) {
	float normalizedY = 1.0f - (float)y / (float)(totalHeight - 1);
	return ofMap(normalizedY, 0.0f, 1.0f, minFreqSlider, maxFreqSlider);
}

//--------------------------------------------------------------
/**
 * @brief Normalize audio to prevent clipping
 * 
 * @param activeFrequencies Number of active sine waves
 * 
 * Scales amplitude based on number of simultaneous frequencies
 */
void ofApp::normalizeAudioBuffer(int activeFrequencies) {
	if (activeFrequencies > 0) {
		float normalizationFactor = 1.0f / sqrt(activeFrequencies);
		for (int i = 0; i < bufferSize; i++) {
			audioBuffer[i] *= normalizationFactor;
		}
	}
}

//--------------------------------------------------------------
/**
 * @brief Copy mono audio to stereo buffer
 * 
 * @param buffer Stereo output buffer
 */
void ofApp::fillSoundBuffer(ofSoundBuffer & buffer) {
	for (size_t i = 0; i < buffer.getNumFrames(); i++) {
		float sample = audioBuffer[i];
		buffer.getBuffer()[i * 2] = sample; // Left channel
		buffer.getBuffer()[i * 2 + 1] = sample; // Right channel
	}
}

//--------------------------------------------------------------
/**
 * @brief Main update loop
 * 
 * Processes image changes, updates parameters, and moves playhead
 */
void ofApp::update() {
	checkAndProcessImageChanges();
	updateFrequencyRange();
	updatePlayheadPosition();
}

//--------------------------------------------------------------
/**
 * @brief Check if image needs reprocessing
 * 
 * Reprocesses when parameters change
 */
void ofApp::checkAndProcessImageChanges() {
	if (hasImageParametersChanged()) {
		imageDirty = true;
		updateLastParameters();
	}

	if (imageDirty && original.isAllocated()) {
		processImage();
		imageDirty = false;
	}
}

//--------------------------------------------------------------
/**
 * @brief Check if image processing parameters changed
 * 
 * @return true if any parameter changed
 */
bool ofApp::hasImageParametersChanged() {
	return contrast != lastContrast || exposure != lastExposure || sobelStrength != lastSobel;
}

//--------------------------------------------------------------
/**
 * @brief Store current parameters
