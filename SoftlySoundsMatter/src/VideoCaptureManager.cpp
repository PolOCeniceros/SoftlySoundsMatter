#include "VideoCaptureManager.h"

#include <algorithm>
#include <cstdlib>
#include <vector>

void VideoCaptureManager::setup() {
	// On some ARM/GStreamer setups, force libv4l2 conversion to avoid DMA_DRM formats.
	setenv("GST_V4L2_USE_LIBV4L2", "1", 1);
	resetTiming();

	// Ensure we are using the GStreamer grabber so we can fallback to a forced pipeline.
	if (!vidGrabber.getGrabber<ofGstVideoGrabber>()) {
		vidGrabber.setGrabber(std::make_shared<ofGstVideoGrabber>());
	}
	// Keep camera subsystem quiet by default (especially on Raspberry Pi / GStreamer).
	vidGrabber.setVerbose(false);

	(void)initFromIndex(deviceIndex);
}

void VideoCaptureManager::close() {
	if (vidGrabber.isInitialized()) {
		vidGrabber.close();
	}
}

void VideoCaptureManager::resetTiming() {
	camInitMs = ofGetElapsedTimeMillis();
	lastFrameMs = 0;
	frameCount = 0;
}

bool VideoCaptureManager::isGrabberPipelineUp() const {
	const auto g = vidGrabber.getGrabber();
	return g != nullptr && g->isInitialized();
}

bool VideoCaptureManager::isGrabberTextureReady() const {
	// ofVideoGrabber::isInitialized() includes texture readiness when using textures.
	return vidGrabber.isInitialized();
}

void VideoCaptureManager::resume() {
	capturing = true;
	resetTiming();
}

void VideoCaptureManager::pause() {
	capturing = false;
}

bool VideoCaptureManager::setDeviceIndex(int requestedIndex) {
	deviceIndex = requestedIndex;
	return initFromIndex(requestedIndex);
}

void VideoCaptureManager::update() {
	// Keep updating as soon as the pipeline is up; the first frame will allocate textures.
	if (!capturing || !isGrabberPipelineUp()) return;

	vidGrabber.update();
	if (vidGrabber.isFrameNew()) {
		lastFrameMs = ofGetElapsedTimeMillis();
		frameCount++;
		return;
	}

	// If the grabber initialized but we never get frames, auto-fallback to raw YUY2 on Linux.
	const uint64_t now = ofGetElapsedTimeMillis();
	const bool noFramesYet = (frameCount == 0);
	if (noFramesYet && (now - camInitMs) > 2000) {
		(void)setupForcedRawYUY2(0, 640, 480, 30);
		camInitMs = now; // avoid tight retry loops
	}
}

bool VideoCaptureManager::captureFrameToRGB(ofPixels & outRgb) const {
	if (!vidGrabber.isInitialized()) return false;

	const int w = (int)vidGrabber.getWidth();
	const int h = (int)vidGrabber.getHeight();
	if (w <= 0 || h <= 0) return false;

	outRgb.allocate(w, h, OF_PIXELS_RGB);
	if (vidGrabber.getTexture().isAllocated()) {
		// Prefer texture readback to guarantee RGB pixels regardless of native format.
		vidGrabber.getTexture().readToPixels(outRgb);
		return true;
	}

	// Fallback to CPU pixels if texture isn't available.
	auto pix = vidGrabber.getPixels();
	if (!pix.isAllocated()) return false;
	outRgb = pix;
	if (outRgb.getNumChannels() != 3) {
		ofPixels tmp = outRgb;
		tmp.setImageType(OF_IMAGE_COLOR);
		outRgb = tmp;
	}
	return true;
}

bool VideoCaptureManager::initFromIndex(int requestedIndex) {
	// Enumerate devices (keep logs quiet; rely on status string for UX).
	auto devices = vidGrabber.listDevices();
	if (devices.empty()) {
		capturing = false;
		return false;
	}

	// Choose target device: prefer requested if available, else first available.
	auto selectAvailableIndex = [&](int preferred) -> int {
		if (preferred >= 0 && preferred < (int)devices.size() && devices[preferred].bAvailable) return preferred;
		for (size_t i = 0; i < devices.size(); ++i) if (devices[i].bAvailable) return (int)i;
		return -1;
	};

	const int targetIndex = selectAvailableIndex(requestedIndex);
	if (targetIndex < 0) {
		capturing = false;
		return capturing;
	}

	deviceIndex = targetIndex;
	activeVideoDeviceId = devices[targetIndex].id;

	if (!initFromDeviceId(activeVideoDeviceId)) {
		ofLogError() << "Camera failed to initialize (device id " << activeVideoDeviceId << ", index " << targetIndex << ").";
		return false;
	}
	return true;
}

bool VideoCaptureManager::initFromDeviceId(int deviceId) {
	resetTiming();

	struct SetupAttempt {
		ofPixelFormat fmt;
		int w;
		int h;
		bool setFps;
		const char * desc;
	};

	const std::vector<SetupAttempt> attempts = {
		{ OF_PIXELS_NATIVE, camWidth, camHeight, true,  "native + FPS" },
		{ OF_PIXELS_NATIVE, camWidth, camHeight, false, "native (no FPS)" },
		{ OF_PIXELS_RGB,    camWidth, camHeight, false, "RGB conversion" },
		{ OF_PIXELS_RGB,    640,      480,       false, "RGB safe 640x480" },
	};

	auto tryAttempt = [&](const SetupAttempt & a) -> bool {
		vidGrabber.close(); // safe even if not initialized
		vidGrabber.setDeviceID(deviceId);
		vidGrabber.setPixelFormat(a.fmt);
		if (a.setFps) vidGrabber.setDesiredFrameRate(camFps);
		return vidGrabber.setup(a.w, a.h);
	};

	bool inited = false;
	for (const auto & a : attempts) {
		inited = tryAttempt(a);
		if (inited) break;
	}

	// Last resort (Linux): Force raw YUY2 capture + videoconvert (avoids needing jpegdec/h264dec plugins).
	if (!inited) {
		inited = setupForcedRawYUY2(deviceId, 640, 480, 30);
	}

	// IMPORTANT (Linux/GStreamer): ofVideoGrabber::isInitialized() also depends on an allocated texture.
	// The texture is typically allocated on the first received frame inside ofVideoGrabber::update().
	// So do not treat isGrabberTextureReady()==false here as a failure.
	if (!inited) {
		ofLogError() << "Camera failed to initialize after all attempts. Check /dev/video* permissions, whether it's busy, and GStreamer plugins.";
		capturing = false;
		return false;
	}

	// Update to actual reported size (forced pipeline sets camWidth/camHeight itself).
	const int reportedW = (int)vidGrabber.getWidth();
	const int reportedH = (int)vidGrabber.getHeight();
	if (reportedW > 0 && reportedH > 0) {
		camWidth = reportedW;
		camHeight = reportedH;
	}

	capturing = true;
	return true;
}

bool VideoCaptureManager::setupForcedRawYUY2(int deviceId, int w, int h, int fps) {
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

	vidGrabber.close();
	resetTiming();

	// Force raw YUY2 from V4L2 and convert to RGB for OF. This avoids jpegdec/h264 decode requirements.
	const std::string dev = "/dev/video" + ofToString(deviceId);
	const std::string pipeline =
		"v4l2src device=" + dev + " io-mode=2 ! "
		"video/x-raw,format=YUY2,width=" + ofToString(w) + ",height=" + ofToString(h) + ",framerate=" + ofToString(fps) + "/1 ! "
		"videoconvert";

	auto * utils = gstGrabber->getGstVideoUtils();
	const bool ok = utils->setPipeline(pipeline, OF_PIXELS_RGB, false, w, h) && utils->startPipeline();
	if (!ok) {
		ofLogError() << "Forced raw YUY2 pipeline failed. Check permissions on " << dev << " and GStreamer v4l2/videoconvert availability.";
		return false;
	}

	camWidth = w;
	camHeight = h;
	capturing = true;
	return true;
}



