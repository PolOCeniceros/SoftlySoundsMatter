#pragma once

#include "ofMain.h"

// Owns the current source image and processed Sobel image.
// The processing pipeline is intentionally simple: resize -> grayscale -> exposure/contrast -> Sobel.
class ImageProcessor {
public:
	/// Set the downscale factor applied to the source image before processing (e.g. 0.25 for quarter resolution).
	void setScaleFactor(float s) { scaleFactor = s; dirty = true; }
	/// Get the current processing downscale factor.
	float getScaleFactor() const { return scaleFactor; }

	/// Set a new RGB source image (e.g. captured from the camera). Allocates internal buffers and marks processing dirty.
	void setSourceRGB(const ofPixels & rgb);

	/// Load an image from disk and set it as the source image.
	/// @return false if load failed.
	/// @note Declared but not currently implemented in `ImageProcessor.cpp`.
	bool loadFromFile(const std::string & path);

	/// Update processing parameters; marks dirty only when values change.
	/// @param contrast Multiplier around midpoint (1.0 = no change).
	/// @param exposure Additive offset in normalized [0..1] space.
	/// @param sobelStrength Scales the Sobel magnitude before clamping to [0..255].
	void setParams(float contrast, float exposure, float sobelStrength);

	/// Run processing if needed (when dirty and a source image is available).
	void update();

	/// True when a source image has been loaded/captured.
	bool hasSource() const { return original.isAllocated(); }
	/// True when a processed Sobel image is available.
	bool hasProcessed() const { return sobelImg.isAllocated(); }

	/// Get the processed Sobel image (grayscale).
	const ofImage & getSobelImage() const { return sobelImg; }
	/// Get writable Sobel pixels (grayscale). Modifying them does not automatically call `update()`.
	ofPixels & getSobelPixels() { return sobelImg.getPixels(); }
	/// Get read-only Sobel pixels (grayscale).
	const ofPixels & getSobelPixels() const { return sobelImg.getPixels(); }
	/// Processed image width in pixels.
	int getWidth() const { return sobelImg.getWidth(); }
	/// Processed image height in pixels.
	int getHeight() const { return sobelImg.getHeight(); }

	/// Compute a draw scale that fills the target window while keeping aspect ratio (cover scaling; may crop).
	float calculateDrawScale(float windowW, float windowH) const;

private:
	ofImage original;
	ofImage graySmall;
	ofImage sobelImg;

	float scaleFactor = 0.25f;
	bool dirty = true;

	// Cached params for change detection
	float lastContrast = 1.0f;
	float lastExposure = 0.0f;
	float lastSobelStrength = 1.0f;

	/// Allocate `graySmall` and `sobelImg` based on current source size and `scaleFactor`.
	void allocateProcessedImages();
	/// Run the full processing pipeline into `sobelImg`.
	void process();

	/// Downscale the source image and convert to grayscale into `graySmall`.
	void resizeToGrayscale();
	/// Apply exposure/contrast adjustments to `graySmall` in-place.
	void applyImageAdjustments(float contrast, float exposure);
	/// Apply Sobel filter to `graySmall` into `sobelImg`.
	void applySobelFilter(float sobelStrength);

	/// Compute Sobel magnitude of `src` into `dst` (grayscale), scaling by `sobelStrength`.
	void applySobel(const ofPixels & src, ofPixels & dst, float sobelStrength);
	/// Compute the Sobel magnitude at a pixel index (x,y) in a grayscale image.
	static int calculateSobelMagnitude(const ofPixels & src, int x, int y, int width, float sobelStrength);
};



