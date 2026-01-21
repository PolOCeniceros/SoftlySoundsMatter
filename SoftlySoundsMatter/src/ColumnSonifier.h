#pragma once

#include "ofMain.h"

#include <vector>

// Turns a single column of an image (typically Sobel brightness) into audio.
// Mapping:
// - each bright pixel becomes a sine oscillator
// - vertical position -> frequency (top high, bottom low)
class ColumnSonifier {
public:
	/// Configure the synthesis engine with audio stream parameters.
	void setup(float sampleRate, int bufferSize);
	/// Set runtime parameters controlling volume and frequency range mapping.
	void setParams(float volume, float minFreq, float maxFreq);

	// Generate audio for a column of pixels (grayscale 0..255).
	// `imgWidth`/`imgHeight` are needed to interpret pixel indexing and build phases.
	/// Render the selected image column to `out` (stereo). Outputs silence when inputs are invalid.
	void renderColumnToBuffer(const ofPixels & pixels, int imgWidth, int imgHeight, int columnX, ofSoundBuffer & out);

private:
	float sampleRate = 44100.0f;
	int bufferSize = 512;

	float volume = 0.5f;
	float minFreq = 100.0f;
	float maxFreq = 4000.0f;
	float brightnessThreshold = 0.1f;

	std::vector<float> phases;
	std::vector<float> audioBuffer;

	/// Ensure `phases` contains one phase accumulator per image row.
	void ensurePhasesSize(int height);
	/// Synthesize mono audio for one column into the internal `audioBuffer`.
	void synthesizeColumn(const ofPixels & pixels, int imgWidth, int imgHeight, int columnX);

	/// Read normalized brightness (0..1) at a pixel (x,y) from a grayscale pixel buffer.
	static float getPixelBrightness(const ofPixels & pixels, int imgWidth, int x, int y);
	/// Map a row index to a target frequency in Hz.
	float calculateFrequencyFromY(int y, int totalHeight) const;
	/// Add a sine oscillator corresponding to row `y` into the buffer, scaled by brightness and volume.
	void addFrequencyToBuffer(int y, float brightness, int totalHeight);
	/// Normalize summed audio by active oscillator count to stabilize loudness.
	void normalizeAudioBuffer(int activeFrequencies);
};



