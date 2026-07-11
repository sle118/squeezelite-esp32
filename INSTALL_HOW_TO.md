# Building and Flashing Squeezelite-ESP32 for ESP32-S3

These instructions cover building the firmware from source for an ESP32-S3
board (tested against a generic ESP32-S3-DevKitC-1 N16R8: 16MB Quad flash +
8MB Octal PSRAM) and flashing it over USB from macOS/Linux.

## 1. Prerequisites

- Docker
- `git`
- A data-capable USB cable, plugged directly into the host (not through a
  hub) — the ESP32-S3's native USB-CDC port is sensitive to flaky links.

## 2. Get the source

```bash
git clone --recursive https://github.com/sle118/squeezelite-esp32.git
cd squeezelite-esp32
# if you already cloned without --recursive:
git submodule update --init --recursive
```

## 3. Why ESP-IDF v4.4.5 specifically

- ESP32-S3 support requires ESP-IDF **4.4 or newer**.
- This codebase still uses the legacy `driver/i2s.h` API, which was removed
  from ESP-IDF starting around v5.3. Using a v5.x toolchain will fail to
  build.
- So ESP-IDF **v4.4.5** is the sweet spot: new enough for esp32s3, old enough
  to keep the legacy I2S driver.

We use the official `espressif/idf:v4.4.5` Docker image rather than
installing ESP-IDF locally, since it comes with the esp32s3 toolchain
preinstalled.

```bash
docker pull espressif/idf:v4.4.5
```

## 4. Configure for ESP32-S3

Copy the S3 config template and set the build target:

```bash
cp build-scripts/I2S-S3-sdkconfig sdkconfig.defaults
rm -f sdkconfig

docker run --rm -v "$PWD:/project" -w /project espressif/idf:v4.4.5 \
  bash -c "pip install --quiet protobuf grpcio-tools && idf.py set-target esp32s3"
```

### Known config fixes needed for common Quad-flash / Octal-PSRAM (R8) boards

The shipped `I2S-S3-sdkconfig` template assumes **Octal flash + Quad PSRAM**.
Most generic ESP32-S3-DevKitC-1 boards ("N16R8" / "N8R8") are the opposite:
**Quad flash + Octal PSRAM** (Espressif's "R8" suffix means 8MB *Octal*
PSRAM; only "R2" modules use Quad PSRAM). If your board hangs or aborts on
boot, check `sdkconfig` for these settings and correct them to match your
actual hardware:

```
# Flash: this board's flash is Quad, not Octal
# CONFIG_ESPTOOLPY_OCT_FLASH is not set
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
CONFIG_ESPTOOLPY_FLASH_SAMPLE_MODE_STR=y
CONFIG_ESPTOOLPY_FLASHMODE="dio"

# PSRAM: this board's PSRAM is Octal (R8 module), not Quad
# CONFIG_SPIRAM_MODE_QUAD is not set
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y
```

Symptoms if these are wrong:
- `E cpu_start: Octal Flash option selected, but EFUSE not configured!` →
  flash is set to Octal but the chip's flash is Quad. Fix the
  `ESPTOOLPY_*` options above.
- `E psram: PSRAM ID read error: 0x00ffffff, PSRAM chip not found...` →
  PSRAM mode doesn't match the chip (Quad vs Octal mismatch). Fix the
  `SPIRAM_MODE_*` options above. This is a hard bus mismatch, not a speed
  issue — changing `SPIRAM_SPEED` alone will not fix it.

After editing `sdkconfig`, re-run `idf.py reconfigure` before building.

## 5. Build

```bash
docker run --rm -v "$PWD:/project" -w /project espressif/idf:v4.4.5 \
  bash -c "pip install --quiet protobuf grpcio-tools && idf.py reconfigure && idf.py build"
```

This builds both the `recovery` app and the `squeezelite` app (a plain
`idf.py build` builds both, as wired up in the top-level `CMakeLists.txt`).
No `npm`/webapp build is needed — the wifi-manager web UI is already
prebuilt and checked in (`components/wifi-manager/webapp/webpack.c/.h`).

Output binaries land in `build/`:
- `build/bootloader/bootloader.bin`
- `build/partition_table/partition-table.bin`
- `build/ota_data_initial.bin`
- `build/recovery.bin`
- `build/squeezelite.bin`

## 6. Flash over USB

Docker on macOS cannot pass through USB serial devices to the container, so
flashing is done from the host using `esptool`.

```bash
python3 -m venv .esptool-venv
.esptool-venv/bin/pip install esptool
```

Find the device path:

```bash
ls /dev/cu.usbmodem*   # macOS, native USB-CDC/JTAG port
```

Flash:

```bash
PORT=/dev/cu.usbmodem<your-device-id>
.esptool-venv/bin/esptool.py -p "$PORT" -b 460800 \
  --before default_reset --after hard_reset --chip esp32s3 \
  write_flash --flash_mode dio --flash_size detect --flash_freq 80m \
  0x0      build/bootloader/bootloader.bin \
  0x8000   build/partition_table/partition-table.bin \
  0xd000   build/ota_data_initial.bin \
  0x10000  build/recovery.bin \
  0x150000 build/squeezelite.bin
```

(The exact offsets/flags are also printed at the end of a successful
`idf.py build`.)

### If flashing fails with checksum/timeout/corruption errors

The initial handshake (chip ID, MAC address) succeeding while the actual
stub/data upload fails with a *different* error each retry (checksum error,
timeout, "Guru Meditation") is a classic symptom of a flaky USB link on the
ESP32-S3's native USB-CDC port, not a software problem:

1. Swap to a different, known-good **data** USB cable (not charge-only).
2. Plug directly into the host — avoid hubs.
3. Retry the same `esptool` command; the device path may change after a
   reconnect (re-check with `ls /dev/cu.usbmodem*`).

## 7. Verify boot

```bash
.esptool-venv/bin/python3 - <<'EOF'
import serial, sys, time
s = serial.Serial()
s.port = '/dev/cu.usbmodem<your-device-id>'
s.baudrate = 115200
s.timeout = 2
s.dtr = False   # do not pre-assert DTR/RTS or you'll hold the chip in reset
s.rts = False
s.open()
t0 = time.time()
while time.time() - t0 < 15:
    data = s.read(4096)
    if data:
        sys.stdout.buffer.write(data)
s.close()
EOF
```

A healthy first boot looks like:

```
I (394) spiram: Found 64MBit SPI RAM device
I (403) spiram: PSRAM initialized, cache is in normal (1-core) mode.
I (846) spiram: SPI SRAM memory test OK
...
I (2697) network_wifi: AP SSID: squeezelite-<macsuffix>
I (2697) network_wifi: AP Password: squeezelite
****************************************************************
RECOVERY APPLICATION
This mode is used to flash Squeezelite into the OTA partition
****
```

On first boot (no saved WiFi credentials), the device starts its own AP
(`squeezelite-<mac-suffix>` / password `squeezelite`) and lands in the
recovery console. Connect to that AP and use the web UI to configure your
WiFi network and flash Squeezelite into the OTA partition.
