#pragma once

#include "ofMain.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

// Owns the sound stream and the audio callback.
// The app provides a render function; AudioEngine handles device selection + silence fallback.
class AudioEngine : public ofBaseSoundOutput {
public:
	/// Configure and start an output audio stream.
	/// Prefers PulseAudio on Linux when available, then falls back to DEFAULT/UNSPECIFIED APIs.
	void setup(float sampleRate, int bufferSize, std::function<void(ofSoundBuffer &)> renderFn);
	/// Close the audio stream (safe to call multiple times).
	void close();

	/// Replace the render callback used by `audioOut()`.
	void setRenderFn(std::function<void(ofSoundBuffer &)> fn);

	// Output device management (useful on Raspberry Pi / Bluetooth sinks).
	/// Enumerate output devices for the currently selected backend API.
	std::vector<ofSoundDevice> getOutputDevices() const;
	/// Build a user-friendly mapping of deviceID -> label for UI/console selection.
	std::map<int, std::string> getOutputDeviceOptions() const; // deviceID -> label
	/// Get the currently selected output device id, or -1 when using default/unspecified output.
	int getOutputDeviceId() const { return outDeviceId; }
	/// Switch output to a specific device id (must exist in `getOutputDevices()`).
	bool setOutputDeviceById(int deviceId);

	/// Audio callback invoked by the sound stream. Calls the user render function or outputs silence.
	void audioOut(ofSoundBuffer & buffer) override;

private:
	/// (Re)start the sound stream for a specific output device.
	void setupStreamForDevice(const ofSoundDevice & device);
	/// Try to configure a stream for a given backend API; returns false if no usable output devices exist.
	bool setupStreamForApiWithFallback(ofSoundDevice::Api api);

	ofSoundStream stream;
	std::function<void(ofSoundBuffer &)> render;
	int outDeviceId = -1;
	ofSoundDevice::Api outDeviceApi = ofSoundDevice::Api::DEFAULT;
	int sampleRate = 44100;
	int bufferSize = 512;
	int numOutputChannels = 2;

	/// Utility: fill the output buffer with zeros.
	static void fillSilence(ofSoundBuffer & buffer);
};



