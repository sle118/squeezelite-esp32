*********************
<strong>Currently this project requires a specific combination of IDF 4 with gcc 5.2. You'll have to implement the gcc 5.2 toolchain from an IDF 3.2 install into the IDF 4 directory in order to successfully compile it</strong>
*********************

	
TODO
- when IP changes, best is to reboot at this point

MOST IMPORTANT: create the right default config file
- make defconfig
Then adapt the config file to your wifi/BT/I2C device (can alos be done on the command line)
- make menuconfig
Then 
- make -j4
- make flash monitor

Once the application is running, under monitor, add autoexec to launch squeezelite at boot

1/ setup WiFi

nvs_set autoexec1 str -v "join \<SSID\> \<password\>"

2/ setup squeezelite command line (optional)

nvs_set autoexec2 str -v "squeezelite -o I2S -b 500:2000 -d all=info -m ESP32"

nvs_set autoexec2 str -v "squeezelite -o SPDIF -R -b 500:2000 -d all=info -m ESP32"

nvs_set autoexec2 str -v "squeezelite -o \\"BT -n '\<sinkname\>' \\" -R -b 500:2000 -d all=info -m ESP32"

(for BT and SPDIF, the -R option allows resampling)

3/ enable autoexec

nvs_set autoexec u8 -v 1		

4/ set bluetooth & airplaysink name (if not set in menuconfig)

nvs_set bt_sink_name str -v "<name>"
nvs_set airplay_sink_name str -v "<name>"

The "join" and "squeezelite" commands can also be typed at the prompt to start manually. Use "help" to see the list.

The squeezelite options are very similar to the regular Linux ones. Differences are :

	- the output is -o [\"BT -n '<sinkname>' \"] | [I2S]
	
	- if you've compiled with RESAMPLE option, normal soxr options are available using -R [-u <options>]. Note that anything above LQ or MQ will overload the CPU
	
	- if you've used RESAMPLE16, <options> are (b|l|m)[:i], with b = basic linear interpolation, l = 13 taps, m = 21 taps, i = interpolate filter coefficients
	
To add options that require quotes ("), escape them with \\". For example, so use a BT speaker named MySpeaker, accept audio up to 192kHz and resample everything to 44100 and use 16 bits resample with medium quality, the command line is:

nvs_set autoexec2 str -v "squeezelite -o \\"BT -n 'BT \<sinkname\>'\\" -b 500:2000 -R -u m -Z 192000 -r \"44100-44100\"

See squeezlite command line, but keys options are

	- Z <rate> : tell LMS what is the max sample rate supported before LMS resamples
	- R (see above)
	- r \"<minrate>-<maxrate>\"

# Additional misc notes to do you build
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
	
