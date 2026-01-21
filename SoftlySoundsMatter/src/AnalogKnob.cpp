#include "AnalogKnob.h"

#include "Mcp3008Spi.h"

void AnalogKnob::setup(Mcp3008Spi *a, int ch) {
	adc = a;
	channel = ch;
	lastReadMs = 0;
	lastRaw = -1;
}

void AnalogKnob::update(uint64_t nowMs) {
	if (!adc || !adc->isOpen()) return;
	if (readPeriodMs > 0 && (nowMs - lastReadMs) < readPeriodMs) return;
	lastReadMs = nowMs;
	lastRaw = adc->readChannelRaw(channel);
}




