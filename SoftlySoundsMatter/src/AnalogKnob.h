#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

class Mcp3008Spi;

// One "knob" = one MCP3008 channel, polled periodically.
class AnalogKnob {
public:
	AnalogKnob() = default;
	/// Construct a knob with a fixed ADC channel and a linear/stepped mapping.
	AnalogKnob(int channel, float min, float step, float max, float defaultValue) {
		this->channel = channel;
		setMapping(min, step, max, defaultValue);
	}

	/// Attach this knob to a shared MCP3008 SPI device and select an ADC channel (0..7).
	void setup(Mcp3008Spi *adc, int channel);
	/// Attach this knob to a shared MCP3008 SPI device using the existing `channel` value.
	void setup(Mcp3008Spi *adc) { setup(adc, channel); }
	/// Set polling period in milliseconds (0 = poll every call to `update()`).
	void setReadPeriodMs(uint64_t ms) { readPeriodMs = ms; }
	/// Poll the ADC if enough time elapsed and cache the latest raw reading.
	void update(uint64_t nowMs);

	/// ADC channel index (0..7).
	int getChannel() const { return channel; }
	/// Latest raw ADC value (0..1023), or -1 if never read / error.
	int getRaw() const { return lastRaw; } // 0..1023, or -1

	// Configure raw→value mapping as (min, step, max) and a default value.
	/// Configure raw→value mapping and a default value used before the first successful read.
	void setMapping(float min, float step, float max, float defaultValue) {
		minValue = min;
		stepValue = step;
		maxValue = max;
		defaultMappedValue = defaultValue;
		hasMapping = true;
	}

	// Returns the transformed value using the configured mapping.
	// If the knob has never been read (`raw == -1`) or mapping isn't configured, returns the default.
	/// Get the mapped knob value (with optional quantization). Falls back to default if raw is invalid.
	float getValue() const {
		if (!hasMapping || lastRaw < 0) return defaultMappedValue;
		const float t = std::clamp(lastRaw / 1023.0f, 0.0f, 1.0f);
		float v = minValue + t * (maxValue - minValue);
		if (stepValue > 0.0f) {
			v = minValue + std::round((v - minValue) / stepValue) * stepValue;
		}
		return std::clamp(v, minValue, maxValue);
	}

	/// Get the configured default value for this knob's mapping.
	float getDefaultValue() const { return defaultMappedValue; }

private:
	Mcp3008Spi *adc = nullptr;
	int channel = 0;
	uint64_t readPeriodMs = 200;
	uint64_t lastReadMs = 0;
	int lastRaw = -1;

	bool hasMapping = false;
	float minValue = 0.0f;
	float stepValue = 0.0f;
	float maxValue = 1.0f;
	float defaultMappedValue = 0.0f;
};




