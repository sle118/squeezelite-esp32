# Getting pre-compiled binaries
An automated build was configured to produce binaries on a regular basis, from common templates that are the most typical. They can be downloaded from : 

https://github.com/sle118/squeezelite-esp32/releases
 
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
- Reboot

3/ set bluetooth & airplaysink name (if you want something other than default)

*this will eventually be moved to the web configuration*

you need to be connected to the device using a usb to serial adapter, with a terminal program (for example putty) opened on that serial port. 
- To setup the bluetooth sink name, enter the following command

nvs_set bt_sink_name str -v "your_bt_name_here" 

- To setup the airplay sink name, enter the following command

nvs_set airplay_sink_name str -v "your_airplay_name_here"

# Additional command line notes
The squeezelite options are very similar to the regular Linux ones. Differences are :

	- the output is -o [\"BT -n <sinkname>\"] | [I2S]
	
	- if you've compiled with RESAMPLE option, normal soxr options are available using -R [-u <options>]. Note that anything above LQ or MQ will overload the CPU
	
	- if you've used RESAMPLE16, <options> are (b|l|m)[:i], with b = basic linear interpolation, l = 13 taps, m = 21 taps, i = interpolate filter coefficients
	
To add options that require quotes ("), escape them with \". For example, so use a BT speaker named MySpeaker and resample everything to 44100 (which is needed with Bluetooth) and use 16 bits resample with medium quality, the command line is:

nvs_set autoexec1 str -v "squeezelite -o \"BT -n 'MySpeaker'\" -b 500:2000 -R -u m -Z 192000 -r \"44100-44100\""

# Building Squeezelite-esp32
MOST IMPORTANT: create the right default config file
```
make defconfig
```
Then adapt the config file to your wifi/BT/I2C device (can alos be done on the command line)
```
make menuconfig
```
Then you will need to build the recovery binary and squeezelite binary:
```
# Build recovery.bin, bootloader.bin, ota_data_initial.bin, partitions.bin  
PROJECT_NAME="recovery" make -j4 all EXTRA_CPPFLAGS='-DRECOVERY_APPLICATION=1'
# Now force a rebuild by touching all the files which may have a RECOVERY_APPLICATION specific source compile logic
find . \( -name "*.cpp" -o -name "*.c" -o -name "*.h" \) -type f -print0 | xargs -0 grep -l "RECOVERY_APPLICATION" | xargs touch
# Build squeezelite.bin
PROJECT_NAME="squeezelite" make -j4 app EXTRA_CPPFLAGS='-DRECOVERY_APPLICATION=0'

make flash monitor
```

Once the application is running, under monitor, you can monitor the system activity. 

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
	
