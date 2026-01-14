#pragma once
#include "ofMain.h"
#include "ofxGui.h"

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

	// Input handling functions
	void resetImageParameters();
	void openImageDialog();
	void togglePlayback();

	// Image data
	ofImage original;
	ofImage graySmall;
	ofImage sobelImg;
	float scaleFactor = 0.25f;
	bool imageDirty = true;

	// Audio components
	ofSoundStream soundStream;
	ofSoundBuffer soundBuffer;
	vector<float> audioBuffer;
	vector<float> phases; // For oscillator phases
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

	float lastContrast = 1.0f;
	float lastExposure = 0.0f;
	float lastSobel = 1.0f;
	float lastPlayheadSpeed = 120.0f;

	// Playhead
	float playheadX = 0.0f;

	// Drawing
	float drawScale = 1.0f;
};
