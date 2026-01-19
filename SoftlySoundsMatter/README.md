# SoftlySoundsMatter

openFrameworks app.

## Build for Raspberry Pi 5 (Linux / ARM64)
This repo currently contains Windows/Visual Studio project files. A Raspberry Pi 5 cannot run a Windows `.exe`, so you must build a Linux/ARM64 executable using the Linux openFrameworks toolchain.

### Option A (recommended): build directly on the Raspberry Pi
1. Install openFrameworks for Linux `aarch64` on the Pi (same major version as your project, e.g. `0.12.1`).
2. Clone/copy this project into your Pi openFrameworks folder:
   - `of_v0.12.1_linuxaarch64_release/apps/myApps/SoftlySoundsMatter/`
3. Ensure addons exist on the Pi:
   - `ofxGui` (bundled with openFrameworks)
   - `ofxDropdown-master` (must be present under `of/addons/` or in the project `addons/` folder)
4. Generate Linux project files using the openFrameworks Project Generator on the Pi.
   - This will create a Linux `Makefile` for the project (required to build).
5. Build:
   - `cd apps/myApps/SoftlySoundsMatter/SoftlySoundsMatter`
   - `make -j4`
6. Run:
   - `make run`

### Video capture notes (Pi)
- Raspberry Pi camera/USB webcam support depends on your OS + backend (V4L2 / libcamera).
- If capture fails, try reducing `camWidth`/`camHeight` and verify the device list output from `vidGrabber.listDevices()`.

### Audio notes (Pi)
- Make sure PipeWire/PulseAudio/ALSA is configured. Use `soundStream.printDeviceList()` output to ensure you’re using the expected output device.
