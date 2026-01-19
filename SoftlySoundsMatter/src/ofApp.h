#pragma once
#include "ofMain.h"
#include "ofxGui.h"

#ifdef TARGET_LINUX
// Allow Linux-only access to GStreamer utils for robust webcam pipelines.
#include "ofGstVideoGrabber.h"
#endif

#include <vector>


class ofApp : public ofBaseApp {
public:
	~ofApp();
	// Main openFrameworks functions
	void setup();
	void update();
	void draw();
	void keyPressed(int key);
	void windowResized(int w, int h);
	void audioOut(ofSoundBuffer & buffer);

	// Setup functions
	void setupGraphics();
	void setupGUI();
	void setupAudio();
	void setupVideoCapture();

	// Video capture functions
	void updateVideoCapture();
	void captureCurrentFrame();
	void toggleVideoCapture();
	void changeVideoDevice();
	bool setupVideoCaptureForcedRawYUY2(int deviceId, int w, int h, int fps);

	// Image loading and processing
	void loadImage(const std::string & path);
	void allocateProcessedImages();
	void processImage();
	void resizeToGrayscale();
	void applyImageAdjustments();
	void applySobelFilter();
	void applySobel(const ofPixels & src, ofPixels & dst);
	int calculateSobelMagnitude(const ofPixels & src, int x, int y, int width);

	// Audio synthesis functions
	void clearAudioBuffer(ofSoundBuffer & buffer);
	int getImageXFromPlayhead();
	float calculateDrawScale();
	void ensurePhasesVectorSize();
	void synthesizeAudioFromColumn(int columnX);
	float getPixelBrightness(ofPixels & pixels, int x, int y);
	void addFrequencyToBuffer(int y, float brightness, int totalHeight);
	float calculateFrequencyFromY(int y, int totalHeight);
	void normalizeAudioBuffer(int activeFrequencies);
	void fillSoundBuffer(ofSoundBuffer & buffer);

	// Update helper functions
	void checkAndProcessImageChanges();
	bool hasImageParametersChanged();
	void updateLastParameters();
	void updateFrequencyRange();
	void updatePlayheadPosition();

	// Drawing functions
	void drawProcessedImage();
	void drawPlayhead();
	void drawActiveFrequencies();
	void drawVideoPreview();
	void drawStatusOverlay();

	// Input handling functions
	void resetImageParameters();
	void openImageDialog();
	void togglePlayback();

	// Video capture components
	ofVideoGrabber vidGrabber;
	ofImage cvColorImg;
	ofImage cvGrayImg;
	bool isCapturing = true;
	bool showVideoPreview = true;
	// On Linux, the GUI selector is an index into listDevices(), but the actual V4L2 id
	// we must pass to setDeviceID() is typically devices[index].id (often /dev/video{id}).
	int activeVideoDeviceId = -1;
	int camWidth = 640;   // Safer default for Linux/V4L2
	int camHeight = 480;  // Safer default for Linux/V4L2
	int camFps = 30;
	uint64_t camInitMs = 0;
	uint64_t lastFrameMs = 0;
	uint64_t frameCount = 0;

	// Image data
	ofImage original;
	ofImage graySmall;
	ofImage sobelImg;
	float scaleFactor = 0.25f;
	bool imageDirty = true;

	// Audio components
	ofSoundStream soundStream;
	ofSoundBuffer soundBuffer;
	std::vector<float> audioBuffer;
	std::vector<float> phases; // For oscillator phases
	float sampleRate = 44100;
	int bufferSize = 512;

	// Frequency mapping
	float minFreq = 100.0f; // Minimum frequency in Hz
	float maxFreq = 4000.0f; // Maximum frequency in Hz

	// GUI
	ofxPanel gui;
	ofxFloatSlider contrast;
	ofxFloatSlider exposure;
	ofxFloatSlider sobelStrength;
	ofxFloatSlider playheadSpeed;
	ofxFloatSlider volume;
	ofxFloatSlider minFreqSlider;
	ofxFloatSlider maxFreqSlider;
	ofxIntSlider videoDeviceId;
	ofxToggle showVideo;
	ofxLabel captureStatus;

	float lastContrast = 1.0f;
	float lastExposure = 0.0f;
	float lastSobel = 1.0f;
	float lastPlayheadSpeed = 120.0f;
	int lastVideoDeviceId = 0;

	// Playhead
	float playheadX = 0.0f;

	// Drawing
	float drawScale = 1.0f;
};
