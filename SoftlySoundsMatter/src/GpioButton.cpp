#include "GpioButton.h"

#include "ofLog.h"

#include <gpiod.h>

GpioButton::~GpioButton() {
	close();
}

void GpioButton::close() {
	if (request) {
		gpiod_line_request_release(request);
		request = nullptr;
	}
	if (chip) {
		gpiod_chip_close(chip);
		chip = nullptr;
	}
}

bool GpioButton::setup(const std::string & chipPath, int offset, bool activeLow, bool pullUp) {
	close();
	lineOffset = offset;
	lastReadMs = 0;
	pressed = false;
	candidatePressed = false;
	candidateSinceMs = 0;
	pressedEdge = false;
	releasedEdge = false;

	if (offset < 0) return false;

	chip = gpiod_chip_open(chipPath.c_str());
	if (!chip) {
		ofLogWarning() << "[GpioButton] Failed to open " << chipPath;
		return false;
	}

	gpiod_line_settings *settings = gpiod_line_settings_new();
	gpiod_line_config *lineCfg = gpiod_line_config_new();
	gpiod_request_config *reqCfg = gpiod_request_config_new();
	if (!settings || !lineCfg || !reqCfg) {
		ofLogWarning() << "[GpioButton] Failed to allocate libgpiod configs.";
		if (settings) gpiod_line_settings_free(settings);
		if (lineCfg) gpiod_line_config_free(lineCfg);
		if (reqCfg) gpiod_request_config_free(reqCfg);
		close();
		return false;
	}

	(void)gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_INPUT);
	(void)gpiod_line_settings_set_active_low(settings, activeLow);
	(void)gpiod_line_settings_set_bias(settings, pullUp ? GPIOD_LINE_BIAS_PULL_UP : GPIOD_LINE_BIAS_PULL_DOWN);

	const unsigned int uOffset = static_cast<unsigned int>(offset);
	if (gpiod_line_config_add_line_settings(lineCfg, &uOffset, 1, settings) < 0) {
		ofLogWarning() << "[GpioButton] Failed to configure line settings for GPIO" << offset;
		gpiod_line_settings_free(settings);
		gpiod_line_config_free(lineCfg);
		gpiod_request_config_free(reqCfg);
		close();
		return false;
	}

	gpiod_request_config_set_consumer(reqCfg, "SoftlySoundsMatter");
	request = gpiod_chip_request_lines(chip, reqCfg, lineCfg);

	// Clean up configs regardless of success (request holds what it needs).
	gpiod_line_settings_free(settings);
	gpiod_line_config_free(lineCfg);
	gpiod_request_config_free(reqCfg);

	if (!request) {
		ofLogWarning() << "[GpioButton] Failed to request GPIO" << offset << " from " << chipPath
		               << " (is it already in use? wrong gpiochip? need permissions?)";
		close();
		return false;
	}

	ofLogNotice() << "[GpioButton] Ready on " << chipPath << " GPIO" << offset
	              << " activeLow=" << (activeLow ? "yes" : "no")
	              << " bias=" << (pullUp ? "pull-up" : "pull-down");
	return true;
}

void GpioButton::update(uint64_t nowMs) {
	if (!request || lineOffset < 0) return;
	if (readPeriodMs > 0 && (nowMs - lastReadMs) < readPeriodMs) return;
	lastReadMs = nowMs;

	int v = gpiod_line_request_get_value(request, static_cast<unsigned int>(lineOffset));
	if (v < 0) return;

	const bool desired = (v != 0);

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


