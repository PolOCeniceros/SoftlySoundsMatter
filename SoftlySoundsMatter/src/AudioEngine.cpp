#include "AudioEngine.h"

void AudioEngine::setup(float sampleRate, int bufferSize, std::function<void(ofSoundBuffer &)> renderFn) {
	render = std::move(renderFn);
	this->sampleRate = (int)sampleRate;
	this->bufferSize = bufferSize;

	stream.printDeviceList();

	// Important on Linux: ofSoundStream::getDeviceList() defaults to Api::DEFAULT, which often
	// maps to ALSA. On systems that route audio via PulseAudio, ALSA probing can fail even
	// though Pulse devices exist (and are shown by printDeviceList()).
	//
	// Strategy:
	// - Prefer Pulse if it yields usable output devices.
	// - Fall back to DEFAULT (whatever the system backend picks).
	// - As a last resort, try UNSPECIFIED with no explicit device and let RtAudio decide.
	if (setupStreamForApiWithFallback(ofSoundDevice::Api::PULSE)) return;
	if (setupStreamForApiWithFallback(ofSoundDevice::Api::DEFAULT)) return;

	ofLogWarning("AudioEngine") << "No output audio devices found on preferred APIs; trying UNSPECIFIED default output.";
	ofSoundStreamSettings settings;
	settings.setOutListener(this);
	settings.sampleRate = this->sampleRate;
	settings.numOutputChannels = numOutputChannels;
	settings.numInputChannels = 0;
	settings.bufferSize = this->bufferSize;
	(void)stream.setup(settings);
	outDeviceId = -1;
	outDeviceApi = ofSoundDevice::Api::UNSPECIFIED;
}

void AudioEngine::close() {
	stream.close();
}

void AudioEngine::setRenderFn(std::function<void(ofSoundBuffer &)> fn) {
	render = std::move(fn);
}

std::vector<ofSoundDevice> AudioEngine::getOutputDevices() const {
	std::vector<ofSoundDevice> out;
	auto devices = stream.getDeviceList(outDeviceApi);
	out.reserve(devices.size());
	for (const auto & d : devices) {
		if (d.outputChannels > 0) out.push_back(d);
	}
	return out;
}

std::map<int, std::string> AudioEngine::getOutputDeviceOptions() const {
	std::map<int, std::string> options;
	for (const auto & d : getOutputDevices()) {
		std::string label = d.name;
		label += " (id=" + ofToString(d.deviceID) + ", out=" + ofToString(d.outputChannels) + ")";
		options[d.deviceID] = label;
	}
	return options;
}

bool AudioEngine::setOutputDeviceById(int deviceId) {
	if (deviceId == outDeviceId) return true;
	for (const auto & d : getOutputDevices()) {
		if (d.deviceID == deviceId) {
			setupStreamForDevice(d);
			return true;
		}
	}
	ofLogWarning("AudioEngine") << "Requested output deviceID not found: " << deviceId;
	return false;
}

void AudioEngine::setupStreamForDevice(const ofSoundDevice & device) {
	ofSoundStreamSettings settings;
	settings.setOutDevice(device);
	settings.setOutListener(this);
	settings.sampleRate = sampleRate;
	settings.numOutputChannels = numOutputChannels;
	settings.numInputChannels = 0;
	settings.bufferSize = bufferSize;

	// Restart stream on the selected output device.
	stream.close();
	if (!stream.setup(settings)) {
		ofLogWarning("AudioEngine") << "Failed to setup stream on device '" << device.name
			<< "' (api=" << ofToString((int)device.api) << ", id=" << device.deviceID << "). Falling back to UNSPECIFIED default.";
		ofSoundStreamSettings fallback;
		fallback.setOutListener(this);
		fallback.sampleRate = sampleRate;
		fallback.numOutputChannels = numOutputChannels;
		fallback.numInputChannels = 0;
		fallback.bufferSize = bufferSize;
		(void)stream.setup(fallback);
		outDeviceId = -1;
		outDeviceApi = ofSoundDevice::Api::UNSPECIFIED;
		return;
	}
	outDeviceId = device.deviceID;
	outDeviceApi = device.api;
	ofLogNotice("AudioEngine") << "Using output device: " << device.name << " (id=" << outDeviceId << ")";
}

bool AudioEngine::setupStreamForApiWithFallback(ofSoundDevice::Api api) {
	outDeviceApi = api;
	auto devices = getOutputDevices();
	if (devices.empty()) return false;

	// Pick the first output device on that API (the GUI will allow switching).
	setupStreamForDevice(devices.front());
	return (outDeviceId >= 0 && outDeviceApi == api);
}

void AudioEngine::audioOut(ofSoundBuffer & buffer) {
	if (render) {
		render(buffer);
		return;
	}
	fillSilence(buffer);
}

void AudioEngine::fillSilence(ofSoundBuffer & buffer) {
	buffer.getBuffer().assign(buffer.getNumFrames() * buffer.getNumChannels(), 0.0f);
}



