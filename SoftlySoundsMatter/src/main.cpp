#include "ofMain.h"
#include "ofApp.h"
#include <cstdlib>

int main() {
	// Force V4L2 to use userspace (mmap/libv4l2) buffers to avoid DMA_DRM caps that break videoscale on some ARM builds
	setenv("GST_V4L2_USE_LIBV4L2", "1", 1);
	setenv("GST_V4L2_ENABLE_DMABUF", "0", 1); // best-effort; ignored if unsupported
	setenv("GST_V4L2_MEMORY", "mmap", 1);     // best-effort; ignored if unsupported
	// Mild GStreamer debug to surface init issues without too much noise
	setenv("GST_DEBUG", "2", 0);

	ofGLFWWindowSettings settings;
	settings.setSize(1280, 800);
	settings.resizable = true;
	settings.decorated = true;

	auto window = ofCreateWindow(settings);
	ofRunApp(window, std::make_shared<ofApp>());
	ofRunMainLoop();

	return 0;
}
