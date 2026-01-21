#include "Mcp3008Spi.h"

#include "ofLog.h"

#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <gpiod.h>

Mcp3008Spi::~Mcp3008Spi() {
	close();
}

void Mcp3008Spi::close() {
	if (fd >= 0) {
		::close(fd);
		fd = -1;
	}
}

bool Mcp3008Spi::setup(const std::string & spidevPath, uint32_t hz, bool runGpiodSmokeTest) {
	devPath = spidevPath;
	speedHz = hz;

	if (runGpiodSmokeTest) {
		logSpi0GpiodSmokeTest();
	}

	close();
	fd = ::open(devPath.c_str(), O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		ofLogWarning() << "[Mcp3008Spi] Can't open " << devPath << ": " << std::strerror(errno)
		               << " (hint: enable SPI + check permissions/group)";
		return false;
	}

	uint8_t mode = SPI_MODE_0;
	uint8_t bits = 8;
	if (::ioctl(fd, SPI_IOC_WR_MODE, &mode) < 0 ||
	    ::ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
	    ::ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speedHz) < 0) {
		ofLogError() << "[Mcp3008Spi] SPI ioctl config failed: " << std::strerror(errno);
		close();
		return false;
	}

	ofLogNotice() << "[Mcp3008Spi] Opened " << devPath << " mode=0 bits=8 speed=" << (int)speedHz << "Hz";
	return true;
}

int Mcp3008Spi::readChannelRaw(int channel) {
	if (fd < 0) return -1;
	if (channel < 0 || channel > 7) return -1;

	// MCP3008 protocol (single-ended):
	// tx[0] = 0b00000001 (start bit)
	// tx[1] = 0b10000000 | (channel << 4)  (SGL/DIFF=1 + channel)
	// tx[2] = 0
	//
	// rx[1] bottom 2 bits + rx[2] => 10-bit value
	uint8_t tx[3] = { 0x01, static_cast<uint8_t>(0x80 | (channel << 4)), 0x00 };
	uint8_t rx[3] = { 0, 0, 0 };

	spi_ioc_transfer tr{};
	tr.tx_buf = reinterpret_cast<__u64>(tx);
	tr.rx_buf = reinterpret_cast<__u64>(rx);
	tr.len = 3;
	tr.speed_hz = speedHz;
	tr.bits_per_word = 8;
	tr.delay_usecs = 0;

	if (::ioctl(fd, SPI_IOC_MESSAGE(1), &tr) < 0) {
		return -1;
	}

	return ((rx[1] & 0x03) << 8) | rx[2];
}

void Mcp3008Spi::logSpi0GpiodSmokeTest() {
	constexpr unsigned int GPIO_SPI0_CE1 = 7;
	constexpr unsigned int GPIO_SPI0_CE0 = 8;
	constexpr unsigned int GPIO_SPI0_MISO = 9;
	constexpr unsigned int GPIO_SPI0_MOSI = 10;
	constexpr unsigned int GPIO_SPI0_SCLK = 11;
	const char *chipPath = "/dev/gpiochip0";

	ofLogNotice() << "[libgpiod] API version: " << gpiod_api_version();
	if (!gpiod_is_gpiochip_device(chipPath)) {
		ofLogWarning() << "[libgpiod] " << chipPath << " is not a gpiochip device (or not accessible).";
		return;
	}

	gpiod_chip *chip = gpiod_chip_open(chipPath);
	if (!chip) {
		ofLogWarning() << "[libgpiod] Failed to open " << chipPath << ": " << std::strerror(errno);
		return;
	}

	auto logLine = [&](unsigned int offset, const char *tag) {
		gpiod_line_info *li = gpiod_chip_get_line_info(chip, offset);
		if (!li) return;
		ofLogNotice() << "[libgpiod] " << tag << " GPIO" << offset
		              << " used=" << (gpiod_line_info_is_used(li) ? "yes" : "no")
		              << " consumer=" << (gpiod_line_info_get_consumer(li) ? gpiod_line_info_get_consumer(li) : "(none)");
		gpiod_line_info_free(li);
	};

	logLine(GPIO_SPI0_SCLK, "SPI0 SCLK");
	logLine(GPIO_SPI0_CE0, "SPI0 CE0");
	logLine(GPIO_SPI0_CE1, "SPI0 CE1");
	logLine(GPIO_SPI0_MISO, "SPI0 MISO");
	logLine(GPIO_SPI0_MOSI, "SPI0 MOSI");

	gpiod_chip_close(chip);
}




