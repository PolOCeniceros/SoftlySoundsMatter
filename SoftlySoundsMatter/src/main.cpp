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
	// Start in fullscreen so we don't see Debian panels/menus (kiosk-style).
	settings.windowMode = OF_FULLSCREEN;
	// Size is ignored in fullscreen on most platforms, but keep a sane default.
	settings.setSize(1920, 1080);
	settings.resizable = false;
	settings.decorated = false;
	// On some Linux window managers, fullscreen requests can be ignored; maximize as a fallback.
	settings.maximized = true;
	// Keep the window above desktop panels/menus (taskbar, top bar) when possible.
	settings.floating = true;
	// Explicitly target primary monitor by default.
	settings.monitor = 0;

	auto window = ofCreateWindow(settings);
	ofRunApp(window, std::make_shared<ofApp>());
	ofRunMainLoop();

	return 0;
}
