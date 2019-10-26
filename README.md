# Squeezelite-esp32
## Supported Hardware 
### SqueezeAMP
Works with the SqueezeAMP see [here](https://forums.slimdevices.com/showthread.php?110926-pre-ANNOUNCE-SqueezeAMP-and-SqueezeliteESP32) and [here](https://github.com/philippe44/SqueezeAMP/blob/master/README.md)

Use the `squeezelite-esp32-SqueezeAmp-sdkconfig.defaults` configuration file.

### ESP32-WROVER + I2S DAC
Squeezelite-esp32 requires esp32 chipset and 4MB PSRAM. ESP32-WROVER meets these requirements.  
To get an audio output an I2S DAC can be used. Cheap PCM5102 I2S DACs work others may also work. PCM5012 DACs can be hooked up via:

I2S - WROVER  
VCC - 3.3V  
3.3V - 3.3V  
GND - GND  
FLT - GND  
DMP - GND  
SCL - GND  
BCK - 26  
DIN - 22  
LCK - 25  
FMT - GND  
XMT - 3.3V 

Use the `squeezelite-esp32-I2S-4MFlash-sdkconfig.defaults` configuration file.

## Setting up ESP-IDF
### Docker
You can use docker to build squeezelite-esp32  
First you need to build the Docker container:
```
docker build -t esp-idf .
```
Then you need to run the container:
```
docker run -i -t -v `pwd`:/workspace/squeezelite-esp32 esp-idf
```
The above command will mount this repo into the docker container and start a bash terminal
for you to then follow the below build steps

### Manual Install of ESP-IDF
<strong>Currently this project requires a specific combination of IDF 4 with gcc 5.2. You'll have to implement the gcc 5.2 toolchain from an IDF 3.2 install into the IDF 4 directory in order to successfully compile it</strong>

You can install IDF manually on Linux or Windows (using the Subsystem for Linux) following the instructions at: https://www.instructables.com/id/ESP32-Development-on-Windows-Subsystem-for-Linux/
And then copying the i2s.c patch file from this repo over to the esp-idf folder

## Building Squeezelite-esp32
MOST IMPORTANT: create the right default config file
- for all libraries, add -mlongcalls. 
- make defconfig
(Note: You can also copy over config files from the build-scripts folder to ./sdkconfig)
Then adapt the config file to your wifi/BT/I2C device (can also be done on the command line)
- make menuconfig
Then

```
# Build recovery.bin, bootloader.bin, ota_data_initial.bin, partitions.bin  
# force appropriate rebuild by touching all the files which may have a RECOVERY_APPLICATION specific source compile logic
	find . \( -name "*.cpp" -o -name "*.c" -o -name "*.h" \) -type f -print0 | xargs -0 grep -l "RECOVERY_APPLICATION" | xargs touch
	export PROJECT_NAME="recovery" 
	make -j4 all EXTRA_CPPFLAGS='-DRECOVERY_APPLICATION=1'
make flash
#
# Build squeezelite.bin
# Now force a rebuild by touching all the files which may have a RECOVERY_APPLICATION specific source compile logic
find . \( -name "*.cpp" -o -name "*.c" -o -name "*.h" \) -type f -print0 | xargs -0 grep -l "RECOVERY_APPLICATION" | xargs touch
export PROJECT_NAME="squeezelite" 
make -j4 app EXTRA_CPPFLAGS='-DRECOVERY_APPLICATION=0'
python ${IDF_PATH}/components/esptool_py/esptool/esptool.py --chip esp32 --port ${ESPPORT} --baud ${ESPBAUD} --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size detect 0x150000 ./build/squeezelite.bin  
# monitor serial output
make monitor

```
 
# Configuration
1/ setup WiFi
- Boot the esp, look for a new wifi access point showing up and connect to it.  Default build ssid and passwords are "squeezelite"/"squeezelite". 
- Once connected, navigate to 192.168.4.1 
- Wait for the list of access points visible from the device to populate in the web page.
- Choose an access point and enter any credential as needed
- Once connection is established, note down the address the device received; this is the address you will use to configure it going forward 

2/ setup squeezelite command line (optional)

At this point, the device should have disabled its built-in access point and should be connected to a known WiFi network.
- navigate to the address that was noted in step #1
- Using the list of predefined options, hoose the mode in which you want squeezelite to start
- Generate the command
- Add or change any additional command line option (for example player name, etc)
- Activate squeezelite execution: this tells the device to automatiaclly run the command at start
- Update the configuration
- click on the "start toggle" button. This will force a reboot. 
- The toggle switch should be set to 'ON' to ensure that squeezelite is active after booting

3/ Updating Squeezelite
- From the firmware tab, click on "Check for Updates"
- Look for updated binaries
- Select a line
- Click on "Flash!"
- The system will reboot into recovery mode (if not already in that mode), wipe the squeezelite partition and download/flash the selected version 

3/ Recovery
- From the firmware tab, click on the "Recovery" button. This will reboot the ESP32 into recovery, where additional configuration options are available from the NVS editor

# Additional command line notes, configured from the http configuration
The squeezelite options are very similar to the regular Linux ones. Differences are :

	- the output is -o ["BT -n '<sinkname>' "] | [I2S]
	- if you've compiled with RESAMPLE option, normal soxr options are available using -R [-u <options>]. Note that anything above LQ or MQ will overload the CPU
	- if you've used RESAMPLE16, <options> are (b|l|m)[:i], with b = basic linear interpolation, l = 13 taps, m = 21 taps, i = interpolate filter coefficients

For example, so use a BT speaker named MySpeaker, accept audio up to 192kHz and resample everything to 44100 and use 16 bits resample with medium quality, the command line is:
	
	squeezelite -o "BT -n 'BT <sinkname>'" -b 500:2000 -R -u m -Z 192000 -r "44100-44100"

See squeezlite command line, but keys options are

	- Z <rate> : tell LMS what is the max sample rate supported before LMS resamples
	- R (see above)
	- r "<minrate>-<maxrate>"

## Additional misc notes to do you build
- as of this writing, ESP-IDF has a bug int he way the PLL values are calculated for i2s, so you *must* use the i2s.c file in the patch directory
- for all libraries, add -mlongcalls.
- audio libraries are complicated to rebuild, open an issue if you really want to
- libmad, libflac (no esp's version), libvorbis (tremor - not esp's version), alac work
- libfaad does not really support real time, but if you want to try
	- -O3 -DFIXED_POINT -DSMALL_STACK
	- change ac_link in configure and case ac_files, remove ''
	- compiler but in cfft.c and cffti1, must disable optimization using
			#pragma GCC push_options
			#pragma GCC optimize ("O0")
			#pragma GCC pop_options
- opus & opusfile
	- for opus, the ESP-provided library seems to work, but opusfile is still needed
	- per mad & few others, edit configure and change $ac_link to add -c (faking link)
	- change ac_files to remove ''
	- add DEPS_CFLAGS and DEPS_LIBS to avoid pkg-config to be required
- better use helixaac			
- set IDF_PATH=/home/esp-idf
- set ESPPORT=COM9
- update flash partition size
- other compiler #define
	- use no resampling or set RESAMPLE (soxr) or set RESAMPLE16 for fast fixed 16 bits resampling
	- use LOOPBACK (mandatory)
	- use BYTES_PER_FRAME=4 (8 is not fully functionnal)
	- LINKALL (mandatory)
	- NO_FAAD unless you want to us faad, which currently overloads the CPU
	- TREMOR_ONLY (mandatory)
