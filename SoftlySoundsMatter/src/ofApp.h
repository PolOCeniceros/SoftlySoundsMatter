#pragma once

#include "ofMain.h"
#include "ofxGui.h"

class ofApp : public ofBaseApp {

public:
	~ofApp();

	void setup();
	void update();
	void draw();
	void keyPressed(int key);
	void windowResized(int w, int h);
	void audioOut(ofSoundBuffer & buffer);

	// Imagen
	void loadImage(const std::string & path);
	void processImage();
	void applySobel(const ofPixels & src, ofPixels & dst);

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

	// Playhead
	float playheadX = 0.0f;

	// Dibujo
	float drawScale = 1.0f;
};
