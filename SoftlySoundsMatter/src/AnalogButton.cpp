#include "AnalogButton.h"

#include "Mcp3008Spi.h"

void AnalogButton::setup(Mcp3008Spi *a, int ch) {
	adc = a;
	channel = ch;
	lastReadMs = 0;
	lastRaw = -1;
	pressed = false;
	candidatePressed = false;
	candidateSinceMs = 0;
	pressedEdge = false;
	releasedEdge = false;
}

void AnalogButton::update(uint64_t nowMs) {
	if (!adc || !adc->isOpen()) return;
	if (readPeriodMs > 0 && (nowMs - lastReadMs) < readPeriodMs) return;
	lastReadMs = nowMs;

	const int raw = adc->readChannelRaw(channel);
	lastRaw = raw;
	if (raw < 0) return;

	// Apply hysteresis: pressed when above pressRaw; released when below releaseRaw.
	bool desired = pressed;
	if (!pressed && raw >= pressRaw) desired = true;
	else if (pressed && raw <= releaseRaw) desired = false;

	// Debounce desired state: require stability for debounceMs before committing.
	if (desired != candidatePressed) {
		candidatePressed = desired;
		candidateSinceMs = nowMs;
		return;
	}

	if (pressed != candidatePressed) {
		if (debounceMs == 0 || (nowMs - candidateSinceMs) >= debounceMs) {
			pressed = candidatePressed;
			if (pressed) pressedEdge = true;
			else releasedEdge = true;
		}
	}
}


