#include "ImageProcessor.h"

#include <algorithm>
#include <cmath>

void ImageProcessor::setSourceRGB(const ofPixels & rgb) {
	if (!rgb.isAllocated()) return;
	original.setFromPixels(rgb);
	original.update();
	allocateProcessedImages();
	dirty = true;
}

void ImageProcessor::setParams(float contrast, float exposure, float sobelStrength) {
	if (contrast != lastContrast || exposure != lastExposure || sobelStrength != lastSobelStrength) {
		lastContrast = contrast;
		lastExposure = exposure;
		lastSobelStrength = sobelStrength;
		dirty = true;
	}
}

void ImageProcessor::update() {
	if (!dirty || !original.isAllocated()) return;
	process();
	dirty = false;
}

float ImageProcessor::calculateDrawScale(float windowW, float windowH) const {
	if (!sobelImg.isAllocated()) return 1.0f;
	const float sx = windowW / std::max(1.0f, (float)sobelImg.getWidth());
	const float sy = windowH / std::max(1.0f, (float)sobelImg.getHeight());
	// Use "cover" scaling: fill the window and crop the overflow.
	return std::max(sx, sy);
}

void ImageProcessor::allocateProcessedImages() {
	const int w = std::max(1, (int)(original.getWidth() * scaleFactor));
	const int h = std::max(1, (int)(original.getHeight() * scaleFactor));
	graySmall.allocate(w, h, OF_IMAGE_GRAYSCALE);
	sobelImg.allocate(w, h, OF_IMAGE_GRAYSCALE);
}

void ImageProcessor::process() {
	resizeToGrayscale();
	applyImageAdjustments(lastContrast, lastExposure);
	applySobelFilter(lastSobelStrength);
}

void ImageProcessor::resizeToGrayscale() {
	ofPixels resized;
	resized.allocate(graySmall.getWidth(), graySmall.getHeight(), OF_PIXELS_RGB);
	original.getPixels().resizeTo(resized);
	graySmall.setFromPixels(resized);
	graySmall.setImageType(OF_IMAGE_GRAYSCALE);
}

void ImageProcessor::applyImageAdjustments(float contrast, float exposure) {
	ofPixels & pix = graySmall.getPixels();
	for (auto & p : pix) {
		float v = p / 255.0f;

		v += exposure;

		v = (v - 0.5f) * contrast + 0.5f;
		
		p = ofClamp(v * 255.0f, 0, 255);
	}
	graySmall.update();
}

void ImageProcessor::applySobelFilter(float sobelStrength) {
	applySobel(graySmall.getPixels(), sobelImg.getPixels(), sobelStrength);
	sobelImg.update();
}

void ImageProcessor::applySobel(const ofPixels & src, ofPixels & dst, float sobelStrength) {
	dst.set(0);
	const int w = src.getWidth();
	const int h = src.getHeight();
	for (int y = 1; y < h - 1; y++) {
		for (int x = 1; x < w - 1; x++) {
			const int magnitude = calculateSobelMagnitude(src, x, y, w, sobelStrength);
			dst[y * w + x] = ofClamp(magnitude, 0, 255);
		}
	}
}

int ImageProcessor::calculateSobelMagnitude(const ofPixels & src, int x, int y, int width, float sobelStrength) {
	const int i = y * width + x;
	const int gx =
		-src[i - width - 1] + src[i - width + 1] +
		-2 * src[i - 1] + 2 * src[i + 1] +
		-src[i + width - 1] + src[i + width + 1];
	const int gy =
		-src[i - width - 1] - 2 * src[i - width] - src[i - width + 1] +
		src[i + width - 1] + 2 * src[i + width] + src[i + width + 1];
	return (int)((std::abs(gx) + std::abs(gy)) * sobelStrength);
}



