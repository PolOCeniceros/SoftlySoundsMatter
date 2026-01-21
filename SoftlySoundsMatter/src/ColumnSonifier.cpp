#include "ColumnSonifier.h"

#include <algorithm>
#include <cmath>

void ColumnSonifier::setup(float sr, int bs) {
	sampleRate = sr;
	bufferSize = bs;
	audioBuffer.assign(bufferSize, 0.0f);
}

void ColumnSonifier::setParams(float v, float minF, float maxF) {
	volume = v;
	minFreq = minF;
	maxFreq = maxF;
}

void ColumnSonifier::renderColumnToBuffer(const ofPixels & pixels, int imgWidth, int imgHeight, int columnX, ofSoundBuffer & out) {
	if (imgWidth <= 0 || imgHeight <= 0 || !pixels.isAllocated()) {
		out.getBuffer().assign(out.getNumFrames() * out.getNumChannels(), 0.0f);
		return;
	}

	const int clampedX = ofClamp(columnX, 0, imgWidth - 1);
	ensurePhasesSize(imgHeight);
	synthesizeColumn(pixels, imgWidth, imgHeight, clampedX);

	// Copy mono -> stereo
	for (size_t i = 0; i < out.getNumFrames(); i++) {
		const float sample = (i < audioBuffer.size()) ? audioBuffer[i] : 0.0f;
		out.getBuffer()[i * 2] = sample;
		out.getBuffer()[i * 2 + 1] = sample;
	}
}

void ColumnSonifier::ensurePhasesSize(int height) {
	if ((int)phases.size() != height) {
		phases.assign(height, 0.0f);
	}
}

void ColumnSonifier::synthesizeColumn(const ofPixels & pixels, int imgWidth, int imgHeight, int columnX) {
	audioBuffer.assign(bufferSize, 0.0f);
	int active = 0;
	for (int y = 0; y < imgHeight; y++) {
		const float b = getPixelBrightness(pixels, imgWidth, columnX, y);
		if (b > brightnessThreshold) {
			active++;
			addFrequencyToBuffer(y, b, imgHeight);
		}
	}
	normalizeAudioBuffer(active);
}

float ColumnSonifier::getPixelBrightness(const ofPixels & pixels, int imgWidth, int x, int y) {
	const int idx = y * imgWidth + x;
	return pixels[idx] / 255.0f;
}

void ColumnSonifier::addFrequencyToBuffer(int y, float brightness, int totalHeight) {
	const float freq = calculateFrequencyFromY(y, totalHeight);
	const float phaseInc = (freq / sampleRate) * TWO_PI;
	for (int i = 0; i < bufferSize; i++) {
		audioBuffer[i] += sin(phases[y]) * brightness * volume;
		phases[y] += phaseInc;
		if (phases[y] >= TWO_PI) phases[y] -= TWO_PI;
	}
}

float ColumnSonifier::calculateFrequencyFromY(int y, int totalHeight) const {
	// Simple 6-note scale repeated across octaves.
	const std::vector<float> scale = { 0, 3, 5, 7, 10, 12 };
	float normalizedY = 1.0f;
	if (totalHeight > 1) normalizedY = 1.0f - (float)y / (totalHeight - 1);

	const int octaveCount = 4;
	const int totalNotes = (int)scale.size() * octaveCount;
	const int noteIndex = (int)(normalizedY * (totalNotes - 1));
	const int octave = noteIndex / (int)scale.size();
	const int scaleNote = (int)scale[noteIndex % (int)scale.size()];

	const float midiNote = 48 + octave * 12 + scaleNote; // C3 base
	const float baseFreq = 440.0f * pow(2.0f, (midiNote - 69) / 12.0f);
	return ofMap(baseFreq, 130.8128f, 2093.0045f, minFreq, maxFreq, true);
}

void ColumnSonifier::normalizeAudioBuffer(int activeFrequencies) {
	if (activeFrequencies <= 0) return;
	const float normalization = 1.0f / sqrt((float)activeFrequencies);
	for (auto & s : audioBuffer) s *= normalization;
}



