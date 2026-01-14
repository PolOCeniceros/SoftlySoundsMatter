#include "ofMain.h"
#include "ofApp.h"

int main() {

	ofGLFWWindowSettings settings;
	settings.setSize(1280, 800);
	settings.resizable = true;
	settings.decorated = true;

	auto window = ofCreateWindow(settings);
	ofRunApp(window, std::make_shared<ofApp>());
	ofRunMainLoop();

	return 0;
}
