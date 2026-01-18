/**
 * @file ofApp.cpp
 * @brief Image-to-Sound Synthesizer Application with Video Capture
 *
 * This application converts images/video frames into sound by:
 * 1. Capturing video from external camera or processing static images
 * 2. Processing frames with Sobel edge detection
 * 3. Scanning columns of pixels with a moving playhead
 * 4. Converting pixel brightness values to audio frequencies
 * 5. Synthesizing audio in real-time based on the scanned data
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
 * @brief Destructor - ensures audio stream and video are properly closed
 */
ofApp::~ofApp() {
	soundStream.close();
	if (vidGrabber.isInitialized()) {
		vidGrabber.close();
	}
}

//--------------------------------------------------------------
/**
 * @brief Main setup function - initializes all components
 *
 * Sets up graphics, GUI controls, audio system, video capture, and loads
 * default image
 */
void ofApp::setup() {
	setupGraphics();
	setupGUI();
	setupAudio();
	setupVideoCapture();
	// Don't load default image if we're using camera
	// loadImage("c03.jpeg");
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
 * Creates sliders for image processing and video capture controls
 */
void ofApp::setupGUI() {
	gui.setup();
	gui.add(videoDeviceId.setup("Camera Device ID", 0, 0, 10));
	gui.add(showVideo.setup("Show Video Preview", true));
	gui.add(captureStatus.setup("Status", "Capturing"));
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
 * @brief Setup video capture from external camera
 *
 * Initializes camera at 640x480 30fps and allocates OpenCV buffers
 */
void ofApp::setupVideoCapture() {
	// List available devices
	vidGrabber.listDevices();

	// Setup video grabber
	vidGrabber.setDeviceID(videoDeviceId);
	vidGrabber.setDesiredFrameRate(camFps);
	vidGrabber.setup(camWidth, camHeight);

	// Allocate OpenCV images
	cvColorImg.allocate(camWidth, camHeight, OF_IMAGE_COLOR);
	cvGrayImg.allocate(camWidth, camHeight, OF_IMAGE_GRAYSCALE);

	isCapturing = true;
	captureStatus = "Capturing";
}

//--------------------------------------------------------------
/**
 * @brief Update video capture and grab new frames
 *
 * Only updates when capturing is enabled
 */
void ofApp::updateVideoCapture() {
	if (isCapturing && vidGrabber.isInitialized()) {
		vidGrabber.update();

		if (vidGrabber.isFrameNew()) {
			// Convert to OpenCV format
			cvColorImg.setFromPixels(vidGrabber.getPixels());
			cvGrayImg = cvColorImg; // Automatic conversion to grayscale
		}
	}
}

//--------------------------------------------------------------
/**
 * @brief Capture current video frame and process it
 *
 * Stops video capture and processes the frozen frame
 */
void ofApp::captureCurrentFrame() {
	if (vidGrabber.isInitialized() && vidGrabber.isFrameNew()) {
		// Create an ofImage from the current frame
		original.setFromPixels(vidGrabber.getPixels());
		original.update();

		// Allocate and trigger processing
		allocateProcessedImages();
		imageDirty = true;

		// Stop capturing to save resources
		isCapturing = false;
		captureStatus = "Paused - Processing Frame";
	}
}

//--------------------------------------------------------------
/**
 * @brief Toggle video capture on/off
 *
 * Switches between live capture and frame processing modes
 */
void ofApp::toggleVideoCapture() {
	if (isCapturing) {
		// Capture current frame and stop
		captureCurrentFrame();
	} else {
		// Resume capturing
		isCapturing = true;
		captureStatus = "Capturing";
	}
}

//--------------------------------------------------------------
/**
 * @brief Change video capture device
 *
 * Reinitializes video grabber with new device ID
 */
void ofApp::changeVideoDevice() {
	if (vidGrabber.isInitialized()) {
		vidGrabber.close();
	}

	vidGrabber.setDeviceID(videoDeviceId);
	vidGrabber.setDesiredFrameRate(camFps);
	vidGrabber.setup(camWidth, camHeight);

	isCapturing = true;
	captureStatus = "Capturing - Device " + ofToString((int)videoDeviceId);
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

	// Stop video capture when loading static image
	isCapturing = false;
	captureStatus = "Static Image Loaded";
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
		v += (float)exposure; // Apply exposure
		v = (v - 0.5f) * (float)contrast + 0.5f; // Apply contrast
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
int ofApp::calculateSobelMagnitude(const ofPixels & src, int x, int y,
	int width) {
	int i = y * width + x;
	// Sobel X kernel: detects vertical edges
	int gx = -src[i - width - 1] + src[i - width + 1] + -2 * src[i - 1] + 2 * src[i + 1] + -src[i + width - 1] + src[i + width + 1];
	// Sobel Y kernel: detects horizontal edges
	int gy = -src[i - width - 1] - 2 * src[i - width] - src[i - width + 1] + src[i + width - 1] + 2 * src[i + width] + src[i + width + 1];
	return (abs(gx) + abs(gy)) * (float)sobelStrength;
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
	if (isCapturing) {
		clearAudioBuffer(buffer);
		return;
	}
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
	buffer.getBuffer().assign(buffer.getNumFrames() * buffer.getNumChannels(),
		0.0f);
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
	std::vector<float> scale = { 0, 3, 5, 7, 10, 12 };
	float normalizedY = 1.0f;
	if (totalHeight > 1) {
		normalizedY = 1.0f - (float)y / (totalHeight - 1);
	}
	int octaveCount = 4;
	int totalNotes = scale.size() * octaveCount;
	int noteIndex = (int)(normalizedY * (totalNotes - 1));
	int octave = noteIndex / scale.size();
	int scaleNote = scale[noteIndex % scale.size()];
	float midiNote = 48 + octave * 12 + scaleNote; // C3 base
	float baseFreq = 440.0f * pow(2.0f, (midiNote - 69) / 12.0f);
	return ofMap(baseFreq, 130.8128f, 2093.0045f, minFreq, maxFreq, true);
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
 * Processes video capture, image changes, parameters, and playhead
 */
void ofApp::update() {
	// Check if video device changed
	if (videoDeviceId != lastVideoDeviceId) {
		changeVideoDevice();
		lastVideoDeviceId = videoDeviceId;
	}

	// Update video capture if active
	updateVideoCapture();

	// Update show video preview toggle
	showVideoPreview = showVideo;

	// Process image if needed
	checkAndProcessImageChanges();
	updateFrequencyRange();

	// Only update playhead if we have a processed image
	if (sobelImg.isAllocated() && !isCapturing) {
		updatePlayheadPosition();
	}
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
 * @brief Store current parameters for change detection
 *
 * Updates cached values to detect parameter changes
 */
void ofApp::updateLastParameters() {
	lastContrast = contrast;
	lastExposure = exposure;
	lastSobel = sobelStrength;
}

//--------------------------------------------------------------
/**
 * @brief Update frequency range from GUI sliders
 *
 * Copies slider values to working variables
 */
void ofApp::updateFrequencyRange() {
	minFreq = minFreqSlider;
	maxFreq = maxFreqSlider;
}

//--------------------------------------------------------------
/**
 * @brief Update playhead position based on speed and time
 *
 * Moves playhead across screen, wrapping at edges
 * Negative speeds move playhead backwards
 */
void ofApp::updatePlayheadPosition() {
	playheadX += playheadSpeed * ofGetLastFrameTime();
	if (playheadX > ofGetWidth()) {
		playheadX = 0; // Wrap to left edge
	} else if (playheadX < 0) {
		playheadX = ofGetWidth(); // Wrap to right edge
	}
}

//--------------------------------------------------------------
/**
 * @brief Main drawing function
 *
 * Renders video preview or processed image, playhead, and GUI
 */
void ofApp::draw() {
	if (isCapturing && showVideoPreview) {
		drawVideoPreview();
	} else if (sobelImg.isAllocated()) {
		drawScale = calculateDrawScale();
		drawProcessedImage();
		drawPlayhead();
		drawActiveFrequencies();
	}

	gui.draw();
}

//--------------------------------------------------------------
/**
 * @brief Draw live video preview
 *
 * Shows current camera feed when capturing
 */
void ofApp::drawVideoPreview() {
	if (vidGrabber.isInitialized()) {
		// Calculate scale to fit video in window
		float videoScale = std::min((float)ofGetWidth() / camWidth,
			(float)ofGetHeight() / camHeight);

		ofPushMatrix();
		ofScale(videoScale, videoScale);
		ofSetColor(255);
		vidGrabber.draw(0, 0);
		ofPopMatrix();

		// Draw status text
		ofSetColor(0, 255, 0);
		ofDrawBitmapString("LIVE VIDEO - Press SPACE to capture frame", 20,
			ofGetHeight() - 20);
	}
}

//--------------------------------------------------------------
/**
 * @brief Draw the edge-detected image scaled to fit window
 *
 * Maintains aspect ratio while fitting in window
 */
void ofApp::drawProcessedImage() {
	ofPushMatrix();
	ofScale(drawScale, drawScale);
	ofSetColor(255);
	sobelImg.draw(0, 0);
	ofPopMatrix();
}

//--------------------------------------------------------------
/**
 * @brief Draw vertical playhead line
 *
 * Red line indicates current scan position
 */
void ofApp::drawPlayhead() {
	ofSetColor(255, 0, 0); // Red
	ofDrawLine(playheadX, 0, playheadX, ofGetHeight());
}

//--------------------------------------------------------------
/**
 * @brief Visualize active frequencies at playhead position
 *
 * Green dots show which pixels are generating sound
 * Brightness indicates amplitude
 */
void ofApp::drawActiveFrequencies() {
	int imgX = getImageXFromPlayhead();
	ofPixels & pixels = sobelImg.getPixels();
	for (int y = 0; y < sobelImg.getHeight(); y++) {
		float brightness = getPixelBrightness(pixels, imgX, y);
		if (brightness > 0.1f) { // Only show active frequencies
			float screenY = y * drawScale;
			ofSetColor(0, 255, 0, brightness * 255); // Green with alpha
			ofDrawCircle(playheadX, screenY, 3);
		}
	}
}

//--------------------------------------------------------------
/**
 * @brief Handle window resize events
 *
 * @param w New window width
 * @param h New window height
 *
 * Repositions GUI to stay in top-right corner
 */
void ofApp::windowResized(int w, int h) {
	gui.setPosition(w - gui.getWidth() - 20, 20);
}

//--------------------------------------------------------------
/**
 * @brief Handle keyboard input
 *
 * @param key Key code pressed
 *
 * Keyboard shortcuts:
 * - Space: Toggle video capture / capture current frame
 * - 'R': Reset image parameters to defaults
 * - 'O': Open file dialog to load new image
 * - 'P': Play/pause playhead movement
 * - 'V': Toggle video preview visibility
 */
void ofApp::keyPressed(int key) {
	switch (key) {
	case ' ':
		toggleVideoCapture();
		break;
	case 'r':
	case 'R':
		resetImageParameters();
		break;
	case 'o':
	case 'O':
		openImageDialog();
		break;
	case 'p':
	case 'P':
		togglePlayback();
		break;
	case 'v':
	case 'V':
		showVideoPreview = !showVideoPreview;
		showVideo = showVideoPreview;
		break;
	}
}

//--------------------------------------------------------------
/**
 * @brief Reset all image processing parameters to defaults
 *
 * Restores contrast, exposure, and edge detection to initial values
 */
void ofApp::resetImageParameters() {
	contrast = 1.0f;
	exposure = 0.0f;
	sobelStrength = 1.0f;
	imageDirty = true; // Trigger reprocessing
}

//--------------------------------------------------------------
/**
 * @brief Open file dialog for image selection
 *
 * Allows user to browse and select image files
 */
void ofApp::openImageDialog() {
	ofFileDialogResult res = ofSystemLoadDialog("Load image");
	if (res.bSuccess) {
		loadImage(res.getPath());
	}
}

//--------------------------------------------------------------
/**
 * @brief Toggle playhead movement on/off
 *
 * Pauses scanning while preserving speed setting
 */
void ofApp::togglePlayback() {
	if (playheadSpeed != 0) {
		lastPlayheadSpeed = playheadSpeed; // Store current speed
		playheadSpeed = 0.0f; // Stop
	} else {
		// Resume with previous speed or default
		playheadSpeed = (lastPlayheadSpeed != 0) ? lastPlayheadSpeed : 120.0f;
	}
}
