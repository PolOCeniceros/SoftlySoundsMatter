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

#include <algorithm>
#include <cmath>
#include <cstdlib>

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
	ofSetLogLevel(OF_LOG_VERBOSE);
}

//--------------------------------------------------------------
/**
 * @brief Initialize GUI controls
 *
 * Creates sliders for image processing and video capture controls
 */
void ofApp::setupGUI() {
	gui.setup();
	// This is an index into vidGrabber.listDevices(); the actual V4L2 id is devices[index].id.
	gui.add(videoDeviceId.setup("Camera Device (index)", 0, 0, 10));
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
	// On some ARM/GStreamer setups, force libv4l2 conversion to avoid DMA_DRM formats
	setenv("GST_V4L2_USE_LIBV4L2", "1", 1);
	camInitMs = ofGetElapsedTimeMillis();
	lastFrameMs = 0;
	frameCount = 0;

#ifdef TARGET_LINUX
	// Ensure we are using the GStreamer grabber on Linux so we can fallback to a forced pipeline.
	if (!vidGrabber.getGrabber<ofGstVideoGrabber>()) {
		vidGrabber.setGrabber(std::make_shared<ofGstVideoGrabber>());
	}
	vidGrabber.setVerbose(true);
#endif

	// Enumerate devices with logging
	auto devices = vidGrabber.listDevices();
	if (devices.empty()) {
		ofLogWarning() << "No video devices found.";
		captureStatus = "No camera found";
		isCapturing = false;
		return;
	}
	for (size_t i = 0; i < devices.size(); ++i) {
		ofLogNotice() << "Device idx " << i << " (id " << devices[i].id << "): '" << devices[i].deviceName
			<< "' available=" << (devices[i].bAvailable ? "yes" : "no");
	}
	// Update slider bounds to valid indices
	videoDeviceId.setMin(0);
	videoDeviceId.setMax((int)devices.size() - 1);

	// Choose target device: prefer user-selected if available, else first available
	int targetIndex = (int)videoDeviceId;
	if (targetIndex < 0 || targetIndex >= (int)devices.size() || !devices[targetIndex].bAvailable) {
		for (size_t i = 0; i < devices.size(); ++i) {
			if (devices[i].bAvailable) { targetIndex = (int)i; break; }
		}
		videoDeviceId = targetIndex;
	}
	const int targetId = devices[targetIndex].id;
	activeVideoDeviceId = targetId;

	bool inited = false;
	std::string attemptMsg;

	// Attempt 1: Native pixel format, requested FPS
	vidGrabber.setDeviceID(targetId);
	vidGrabber.setPixelFormat(OF_PIXELS_NATIVE);
	vidGrabber.setDesiredFrameRate(camFps);
	attemptMsg = "Attempt#1 native fmt with FPS at " + ofToString(camWidth) + "x" + ofToString(camHeight);
	ofLogVerbose() << attemptMsg;
	inited = vidGrabber.setup(camWidth, camHeight);

	// Attempt 2: Native format, no explicit FPS
	if (!inited) {
		ofLogWarning() << "Init failed. " << attemptMsg << ". Retrying without setDesiredFrameRate.";
		vidGrabber.close();
		vidGrabber.setDeviceID(targetId);
		vidGrabber.setPixelFormat(OF_PIXELS_NATIVE);
		inited = vidGrabber.setup(camWidth, camHeight);
	}

	// Attempt 3: Force RGB conversion (some drivers refuse native caps)
	if (!inited) {
		ofLogWarning() << "Init failed. Retrying with RGB conversion.";
		vidGrabber.close();
		vidGrabber.setDeviceID(targetId);
		vidGrabber.setPixelFormat(OF_PIXELS_RGB);
		inited = vidGrabber.setup(camWidth, camHeight);
	}

	// Attempt 4: Safe 640x480 RGB
	if (!inited) {
		ofLogWarning() << "Init failed. Retrying 640x480 RGB safe mode.";
		vidGrabber.close();
		vidGrabber.setDeviceID(targetId);
		vidGrabber.setPixelFormat(OF_PIXELS_RGB);
		inited = vidGrabber.setup(640, 480);
	}

	// Attempt 5 (Linux): Force raw YUY2 capture + videoconvert (avoids needing jpegdec/h264dec plugins)
	// This is especially helpful when the camera advertises MJPG/H264 at 30fps but decode plugins are missing.
	if (!inited) {
		ofLogWarning() << "Init failed. Retrying forced raw YUY2 pipeline (Linux fallback).";
		inited = setupVideoCaptureForcedRawYUY2(targetId, 640, 480, 30);
	}

	// IMPORTANT (Linux/GStreamer): ofVideoGrabber::isInitialized() also depends on an allocated texture.
	// The texture is typically allocated on the first received frame inside ofVideoGrabber::update().
	// So we must not treat vidGrabber.isInitialized()==false at this stage as a camera-init failure.
	if (!inited) {
		ofLogError() << "Camera failed to initialize after all attempts. Check /dev/video* permissions, whether it's busy, and GStreamer plugins.";
		isCapturing = false;
		captureStatus = "Camera init failed";
		return;
	}

	// Update to actual reported size
	camWidth = (int)vidGrabber.getWidth();
	camHeight = (int)vidGrabber.getHeight();
	ofLogNotice() << "Using camera '" << devices[targetIndex].deviceName << "' at "
		<< camWidth << "x" << camHeight << " (device id " << targetId << ", index " << targetIndex << ")";

	// Allocate working images to the actual camera size
	cvColorImg.allocate(camWidth, camHeight, OF_IMAGE_COLOR);
	cvGrayImg.allocate(camWidth, camHeight, OF_IMAGE_GRAYSCALE);

	isCapturing = true;
	captureStatus = "Capturing (waiting for frames...)";
}

//--------------------------------------------------------------
/**
 * @brief Update video capture and grab new frames
 *
 * Only updates when capturing is enabled
 */
void ofApp::updateVideoCapture() {
	// ofVideoGrabber::isInitialized() may be false until the first frame allocates a texture.
	// Use the underlying grabber's initialized state for update gating, so frames can start flowing.
	const bool grabberPipelineUp =
		(vidGrabber.getGrabber() != nullptr) && vidGrabber.getGrabber()->isInitialized();

	if (isCapturing && grabberPipelineUp) {
		vidGrabber.update();

		if (vidGrabber.isFrameNew()) {
			lastFrameMs = ofGetElapsedTimeMillis();
			frameCount++;
			captureStatus = "Capturing (" + ofToString(frameCount) + " frames)";
		} else {
			// If the grabber initialized but we never get frames, auto-fallback to raw YUY2 on Linux.
			const uint64_t now = ofGetElapsedTimeMillis();
			const bool noFramesYet = (frameCount == 0);
			if (noFramesYet && (now - camInitMs) > 2000) {
				captureStatus = "No frames yet (2s). Retrying raw YUY2...";
				ofLogWarning() << "No frames received after 2s; attempting raw YUY2 fallback.";
				// Best-effort: retry with a forced raw pipeline using the actual V4L2 device id.
				if (activeVideoDeviceId >= 0) {
					setupVideoCaptureForcedRawYUY2(activeVideoDeviceId, 640, 480, 30);
				}
				camInitMs = now; // avoid tight retry loops
			}
		}
	}
}

bool ofApp::setupVideoCaptureForcedRawYUY2(int deviceId, int w, int h, int fps) {
#ifdef TARGET_LINUX
	// Ensure the grabber is a GStreamer grabber.
	auto gstGrabber = vidGrabber.getGrabber<ofGstVideoGrabber>();
	if (!gstGrabber) {
		vidGrabber.setGrabber(std::make_shared<ofGstVideoGrabber>());
		gstGrabber = vidGrabber.getGrabber<ofGstVideoGrabber>();
	}
	if (!gstGrabber) {
		ofLogError() << "Linux fallback requested but GStreamer grabber not available.";
		return false;
	}

	// Fully close current pipeline/texture state first.
	vidGrabber.close();
	frameCount = 0;
	lastFrameMs = 0;
	camInitMs = ofGetElapsedTimeMillis();

	// Force raw YUY2 from V4L2 and convert to RGB for OF. This avoids jpegdec/h264 decode requirements.
	const std::string dev = "/dev/video" + ofToString(deviceId);
	const std::string pipeline =
		"v4l2src device=" + dev + " io-mode=2 ! "
		"video/x-raw,format=YUY2,width=" + ofToString(w) + ",height=" + ofToString(h) + ",framerate=" + ofToString(fps) + "/1 ! "
		"videoconvert";

	ofLogNotice() << "Forced pipeline: " << pipeline;

	// Ask appsink for RGB and start.
	auto * utils = gstGrabber->getGstVideoUtils();
	const bool ok = utils->setPipeline(pipeline, OF_PIXELS_RGB, false, w, h) && utils->startPipeline();
	if (!ok) {
		ofLogError() << "Forced raw YUY2 pipeline failed. Check permissions on " << dev << " and GStreamer v4l2/videoconvert availability.";
		return false;
	}

	// Update sizes based on forced request.
	camWidth = w;
	camHeight = h;
	isCapturing = true;
	captureStatus = "Capturing (raw YUY2 fallback)";
	return true;
#else
	(void)deviceId;
	(void)w;
	(void)h;
	(void)fps;
	return false;
#endif
}

//--------------------------------------------------------------
/**
 * @brief Capture current video frame and process it
 *
 * Stops video capture and processes the frozen frame
 */
void ofApp::captureCurrentFrame() {
	if (vidGrabber.isInitialized() && vidGrabber.isFrameNew()) {
		// Read back from texture to ensure RGB pixels regardless of native format
		ofPixels rgbPix;
		rgbPix.allocate(vidGrabber.getWidth(), vidGrabber.getHeight(), OF_PIXELS_RGB);
		if (vidGrabber.getTexture().isAllocated()) {
			vidGrabber.getTexture().readToPixels(rgbPix);
		} else {
			// Fallback to CPU pixels if texture isn't available
			rgbPix = vidGrabber.getPixels();
			if (rgbPix.getNumChannels() != 3) {
				// Last resort: create a grayscale to RGB copy
				ofPixels tmp = rgbPix;
				tmp.setImageType(OF_IMAGE_COLOR);
				rgbPix = tmp;
			}
		}
		original.setFromPixels(rgbPix);
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

	// Re-run the same robust selection/initialization as in setup
	auto devices = vidGrabber.listDevices();
	int targetIndex = (int)videoDeviceId;
	if (devices.empty()) {
		isCapturing = false;
		captureStatus = "No camera found";
		return;
	}
	if (targetIndex < 0 || targetIndex >= (int)devices.size() || !devices[targetIndex].bAvailable) {
		for (size_t i = 0; i < devices.size(); ++i) {
			if (devices[i].bAvailable) { targetIndex = (int)i; break; }
		}
		videoDeviceId = targetIndex;
	}
	const int targetId = devices[targetIndex].id;
	activeVideoDeviceId = targetId;

	bool inited = false;
	// Attempt 1: native with FPS
	vidGrabber.setDeviceID(targetId);
	vidGrabber.setPixelFormat(OF_PIXELS_NATIVE);
	vidGrabber.setDesiredFrameRate(camFps);
	inited = vidGrabber.setup(camWidth, camHeight);
	// Attempt 2: native without FPS
	if (!inited) {
		vidGrabber.close();
		vidGrabber.setDeviceID(targetId);
		vidGrabber.setPixelFormat(OF_PIXELS_NATIVE);
		inited = vidGrabber.setup(camWidth, camHeight);
	}
	// Attempt 3: RGB
	if (!inited) {
		vidGrabber.close();
		vidGrabber.setDeviceID(targetId);
		vidGrabber.setPixelFormat(OF_PIXELS_RGB);
		inited = vidGrabber.setup(camWidth, camHeight);
	}
	// Attempt 4: 640x480 RGB safe
	if (!inited) {
		vidGrabber.close();
		vidGrabber.setDeviceID(targetId);
		vidGrabber.setPixelFormat(OF_PIXELS_RGB);
		inited = vidGrabber.setup(640, 480);
	}
	// Attempt 5: forced raw fallback (Linux)
	if (!inited) {
		inited = setupVideoCaptureForcedRawYUY2(targetId, 640, 480, 30);
	}
	if (!inited || !vidGrabber.isInitialized()) {
		ofLogError() << "Camera failed to initialize after all attempts on device change.";
		isCapturing = false;
		captureStatus = "Camera init failed";
		return;
	}
	camWidth = (int)vidGrabber.getWidth();
	camHeight = (int)vidGrabber.getHeight();
	isCapturing = true;
	captureStatus = "Capturing - Device id " + ofToString(targetId) + " (index " + ofToString(targetIndex) + ")";
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
	// On Linux/Pi the default device can be "none" depending on ALSA/Pulse/PipeWire.
	// Pick the first output-capable device if one exists.
	auto devices = soundStream.getDeviceList();
	for (const auto & d : devices) {
		if (d.outputChannels > 0) {
			settings.setOutDevice(d);
			break;
		}
	}
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

	// Always draw status overlay to aid debugging on Linux
	drawStatusOverlay();
}

//--------------------------------------------------------------
/**
 * @brief Draw live video preview
 *
 * Shows current camera feed when capturing
 */
void ofApp::drawVideoPreview() {
	const bool grabberPipelineUp =
		(vidGrabber.getGrabber() != nullptr) && vidGrabber.getGrabber()->isInitialized();

	if (grabberPipelineUp) {
		// Calculate scale to fit video in window using actual grabber size
		float vw = std::max(1.0f, (float)vidGrabber.getWidth());
		float vh = std::max(1.0f, (float)vidGrabber.getHeight());
		float videoScale = std::min((float)ofGetWidth() / vw,
			(float)ofGetHeight() / vh);

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
	else {
		// Helpful overlay when camera didn't init
		ofSetColor(255, 80, 80);
		ofDrawBitmapString("No camera initialized.\n- Check /dev/video* permissions.\n- Try changing 'Camera Device ID' in GUI.\n- Install GStreamer plugins (good/bad/ugly/libav).", 20, 40);
	}
}

//--------------------------------------------------------------
void ofApp::drawStatusOverlay() {
	ofPushStyle();
	ofSetColor(255);
	std::string status;
	status += "Status: " + (std::string)captureStatus + "\n";
	status += std::string("Capturing: ") + (isCapturing ? "yes" : "no") + ", Preview: " + (showVideoPreview ? "on" : "off") + "\n";
	status += "Device index: " + ofToString((int)videoDeviceId) + "\n";
	status += "Device id: " + ofToString(activeVideoDeviceId) + "\n";
	const bool grabberPipelineUp =
		(vidGrabber.getGrabber() != nullptr) && vidGrabber.getGrabber()->isInitialized();
	status += "Grabber pipeline up: " + std::string(grabberPipelineUp ? "yes" : "no") + "\n";
	status += "Grabber (with texture): " + std::string(vidGrabber.isInitialized() ? "yes" : "no") + "\n";
	if (grabberPipelineUp) {
		status += "Size: " + ofToString((int)vidGrabber.getWidth()) + "x" + ofToString((int)vidGrabber.getHeight()) + "\n";
		status += "FrameNew: " + std::string(vidGrabber.isFrameNew() ? "yes" : "no") + "\n";
		status += "Frames: " + ofToString(frameCount) + "\n";
		if (lastFrameMs > 0) {
			status += "Last frame: " + ofToString((uint64_t)(ofGetElapsedTimeMillis() - lastFrameMs)) + "ms ago\n";
		}
	}
	ofDrawBitmapString(status, 20, 60);
	ofPopStyle();
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
