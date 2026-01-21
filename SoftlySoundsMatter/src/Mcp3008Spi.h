#pragma once

#include <cstdint>
#include <string>

// Shared SPI device wrapper for MCP3008 (10-bit ADC).
// Opens /dev/spidevX.Y once and allows reading raw channels 0..7.
class Mcp3008Spi {
public:
	/// Destructor closes the SPI device if open.
	~Mcp3008Spi();

	/// Open and configure the MCP3008 SPI device.
	/// @param spidevPath Path to spidev device (e.g. "/dev/spidev0.0").
	/// @param speedHz SPI max speed in Hz.
	/// @param runGpiodSmokeTest When true (Linux), logs SPI0 pin usage info via libgpiod.
	/// @return true when the SPI device is opened and configured.
	bool setup(const std::string & spidevPath = "/dev/spidev0.0",
	           uint32_t speedHz = 1000000,
	           bool runGpiodSmokeTest = true);
	/// Close the SPI device (safe to call multiple times).
	void close();

	/// True when the SPI device file descriptor is open.
	bool isOpen() const { return fd >= 0; }

	// Returns 0..1023, or -1 on error.
	/// Read a raw 10-bit value from a channel (0..7). Returns -1 on error.
	int readChannelRaw(int channel);

private:
	int fd = -1;
	std::string devPath;
	uint32_t speedHz = 1000000;

	/// Linux-only: log a brief SPI0 pin "in use" report using libgpiod (diagnostic aid).
	void logSpi0GpiodSmokeTest();
};




