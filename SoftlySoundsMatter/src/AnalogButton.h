#pragma once

#include <cstdint>

class Mcp3008Spi;

// One "analog button" = one MCP3008 channel, interpreted as pressed/released via thresholds.
// Designed for simple circuits like a momentary switch + pull-down/pull-up producing a high/low ADC value.
class AnalogButton {
public:
	AnalogButton() = default;
	explicit AnalogButton(int channel) : channel(channel) {}

	/// Attach this button to a shared MCP3008 SPI device and select an ADC channel (0..7).
	void setup(Mcp3008Spi *adc, int channel);
	/// Attach this button to a shared MCP3008 SPI device using the existing `channel` value.
	void setup(Mcp3008Spi *adc) { setup(adc, channel); }

	/// Set polling period in milliseconds (0 = poll every call to `update()`).
	void setReadPeriodMs(uint64_t ms) { readPeriodMs = ms; }

	/// Set hysteresis thresholds in raw units (0..1023).
	/// - Press happens when raw >= pressRaw
	/// - Release happens when raw <= releaseRaw
	/// @note releaseRaw should typically be <= pressRaw.
	void setThresholds(int pressRaw, int releaseRaw) { this->pressRaw = pressRaw; this->releaseRaw = releaseRaw; }

	/// Set debounce time in milliseconds. Edges only fire when stable for this long.
	void setDebounceMs(uint64_t ms) { debounceMs = ms; }

	/// Poll the ADC if enough time elapsed and update pressed/released edge flags.
	void update(uint64_t nowMs);

	/// ADC channel index (0..7).
	int getChannel() const { return channel; }
	/// Latest raw ADC value (0..1023), or -1 if never read / error.
	int getRaw() const { return lastRaw; }

	/// Current debounced pressed state.
	bool isPressed() const { return pressed; }

	/// Edge flags since last update (latched until consumed).
	bool consumePressed() { const bool v = pressedEdge; pressedEdge = false; return v; }
	bool consumeReleased() { const bool v = releasedEdge; releasedEdge = false; return v; }

private:
	Mcp3008Spi *adc = nullptr;
	int channel = 0;

	uint64_t readPeriodMs = 20;
	uint64_t lastReadMs = 0;
	int lastRaw = -1;

	// Defaults: treat "pressed" as a high ADC value.
	int pressRaw = 700;
	int releaseRaw = 600;

	uint64_t debounceMs = 30;
	bool pressed = false;

	// Debounce / edge tracking
	bool candidatePressed = false;
	uint64_t candidateSinceMs = 0;

	bool pressedEdge = false;
	bool releasedEdge = false;
};


