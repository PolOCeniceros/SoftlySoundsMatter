#pragma once

#include <cstdint>
#include <string>

struct gpiod_chip;
struct gpiod_line_request;

// Simple GPIO button (direct Raspberry Pi GPIO) using libgpiod v2.
// Polls input value, applies debounce in software, and exposes pressed/released edges.
class GpioButton {
public:
	GpioButton() = default;
	~GpioButton();

	// Non-copyable (owns libgpiod resources)
	GpioButton(const GpioButton &) = delete;
	GpioButton & operator=(const GpioButton &) = delete;

	/// Open gpiochip and request one line as input.
	/// @param chipPath e.g. "/dev/gpiochip0"
	/// @param lineOffset GPIO line offset (BCM number on Raspberry Pi in most setups)
	/// @param activeLow When true, a physical low level is treated as "pressed".
	/// @param pullUp When true, request a pull-up bias; otherwise pull-down bias.
	bool setup(const std::string & chipPath, int lineOffset, bool activeLow = true, bool pullUp = true);

	void close();

	bool isReady() const { return request != nullptr; }
	int getLineOffset() const { return lineOffset; }

	void setReadPeriodMs(uint64_t ms) { readPeriodMs = ms; }
	void setDebounceMs(uint64_t ms) { debounceMs = ms; }

	/// Poll and update edge flags.
	void update(uint64_t nowMs);

	bool isPressed() const { return pressed; }
	bool consumePressed() { const bool v = pressedEdge; pressedEdge = false; return v; }
	bool consumeReleased() { const bool v = releasedEdge; releasedEdge = false; return v; }

private:
	gpiod_chip *chip = nullptr;
	gpiod_line_request *request = nullptr;
	int lineOffset = -1;

	uint64_t readPeriodMs = 10;
	uint64_t lastReadMs = 0;

	uint64_t debounceMs = 30;
	bool pressed = false;

	bool candidatePressed = false;
	uint64_t candidateSinceMs = 0;

	bool pressedEdge = false;
	bool releasedEdge = false;
};


