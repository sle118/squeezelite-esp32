# Squeezelite-esp32
## Supported Hardware 
### SqueezeAMP
Works with the SqueezeAMP see [here](https://forums.slimdevices.com/showthread.php?110926-pre-ANNOUNCE-SqueezeAMP-and-SqueezeliteESP32) and [here](https://github.com/philippe44/SqueezeAMP/blob/master/README.md). Add repository https://raw.githubusercontent.com/sle118/squeezelite-esp32/master/plugin/repo.xml to LMS if you want to have a display

Use the `squeezelite-esp32-SqueezeAmp-sdkconfig.defaults` configuration file.

### ESP32-A1S
Works with [ESP32-A1S](https://docs.ai-thinker.com/esp32-a1s) module that includes audio codec and headset output. You still need to use a demo board like [this](https://www.aliexpress.com/item/4000765857347.html?spm=2114.12010615.8148356.11.5d963cd0j669ns) or an external amplifier if you want direct speaker connection. 

The board showed above has the following IO set
- amplifier: GPIO21
- key2: GPIO13, key3: GPIO19, key4: GPIO23, key5: GPIO18, key6: GPIO5 (to be confirmed with dip switches)
- key1: not sure, something with GPIO36
- jack insertion: GPIO39 (inserted low)
- LED: GPIO22 (active low)
(note that GPIO need pullups)

So a possible config would be
- set_GPIO: 21=amp,22=green:0,39=jack:0
- a button mapping: 
```
[{"gpio":5,"normal":{"pressed":"ACTRLS_TOGGLE"}},{"gpio":18,"pull":true,"shifter_gpio":5,"normal":{"pressed":"ACTRLS_VOLUP"}, "shifted":{"pressed":"ACTRLS_NEXT"}}, {"gpio":23,"pull":true,"shifter_gpio":5,"normal":{"pressed":"ACTRLS_VOLDOWN"},"shifted":{"pressed":"ACTRLS_PREV"}}]
```

### ESP32-WROVER + I2S DAC
Squeezelite-esp32 requires esp32 chipset and 4MB PSRAM. ESP32-WROVER meets these requirements. To get an audio output an I2S DAC can be used. Cheap PCM5102 I2S DACs work others may also work. PCM5012 DACs can be hooked up via:

I2S - WROVER  
VCC - 3.3V  
3.3V - 3.3V  
GND - GND  
FLT - GND  
DMP - GND  
SCL - GND  
BCK - (BCK - see below)  
DIN - (DO - see below)  
LCK - (WS - see below)
FMT - GND  
XMT - 3.3V 

Use the `squeezelite-esp32-I2S-4MFlash-sdkconfig.defaults` configuration file.

## Configuration
To access NVS, in the webUI, go to credits and select "shows nvs editor". Go into the NVS editor tab to change NFS parameters. In syntax description below \<\> means a value while \[\] describe optional parameters. 

### I2C
The NVS parameter "i2c_config" set the i2c's gpio used for generic purpose (e.g. display). Leave it blank to disable I2C usage. Note that on SqueezeAMP, port must be 1. Default speed is 400000 but some display can do up to 800000 or more. Syntax is
```
sda=<gpio>,scl=<gpio>[,port=0|1][,speed=<speed>]
```
### SPI
The NVS parameter "spi_config" set the spi's gpio used for generic purpose (e.g. display). Leave it blank to disable SPI usage. The DC parameter is needed for displays. Syntax is
```
data=<gpio>,clk=<gpio>[,dc=<gpio>][,host=1|2]
```
### DAC/I2S
The NVS parameter "dac_config" set the gpio used for i2s communication with your DAC. You can define the defaults at compile time but nvs parameter takes precedence except for SqueezeAMP and A1S where these are forced at runtime. If your DAC also requires i2c, then you must go the re-compile route. Syntax is
```
bck=<gpio>,ws=<gpio>,do=<gpio>
```
### SPDIF
The NVS parameter "spdif_config" sets the i2s's gpio needed for SPDIF. 

SPDIF is made available by re-using i2s interface in a non-standard way, so although only one pin (DO) is needed, the controller must be fully initialized, so the bit clock (bck) and word clock (ws) must be set as well. As i2s and SPDIF are mutually exclusive, you can reuse the same IO if your hardware allows so.

You can define the defaults at compile time but nvs parameter takes precedence except for SqueezeAMP where these are forced at runtime.

Leave it blank to disable SPDIF usage, you can also define them at compile time using "make menuconfig". Syntax is 
```
bck=<gpio>,ws=<gpio>,do=<gpio>
```
### Display
The NVS parameter "display_config" sets the parameters for an optional display. Syntax is
```
I2C,width=<pixels>,height=<pixels>[address=<i2c_address>][,HFlip][,VFlip][driver=SSD1306|SSD1326|SH1106]
SPI,width=<pixels>,height=<pixels>,cs=<gpio>[,speed=<speed>][,HFlip][,VFlip][driver=SSD1306|SSD1326|SH1106]
```
- VFlip and HFlip are optional can be used to change display orientation
- Default speed is 8000000 (8MHz) but SPI can work up to 26MHz or even 40MHz

Currently 128x32/64 I2C and SPI display like [this](https://www.buydisplay.com/i2c-blue-0-91-inch-oled-display-module-128x32-arduino-raspberry-pi) and [this](https://www.waveshare.com/wiki/1.3inch_OLED_HAT) are supported

The NVS parameter "metadata_config" sets how metadata is displayed for AirPlay and Bluetooth. Syntax is
```
[format=<display_content>][,speed=<speed>][,pause=<pause>]
```
- 'speed' is the scrolling speed in ms (default is 33ms)

- 'pause' is the pause time between scrolls in ms (default is 3600ms)

- 'format' can contain free text and any of the 3 keywords %artist%, %album%, %title%. Using that format string, the keywords are replaced by their value to build the string to be displayed. Note that the plain text following a keyword that happens to be empty during playback of a track will be removed. For example, if you have set format=%artist% - %title% and there is no artist in the metadata then only <title> will be displayed not " - <title>".

You can install the excellent plugin "Music Information Screen" which is super useful to tweak the layout for these small displays.

### RGB VU Meters
The NVS parameter "led_vu_config" sets the parameters for an RGB VU meter display. The syntax is 
```
WS2812,length=<leds>,data<gpio>[hold=<cycles>,enable=<gpio>,refresh=<delay>,bright=<bright>]
```
- 'length' is the number of leds in the string. This should be an odd number.

- 'data' is the gpio used for the data line for the leds

- 'hold' is the number of cycles that the peak leds stays at the peak level before decaying to the next lower led position. The leds update at 30 FPS, thus a hold count of 30 will cause the blue led to decay at one position per second.

- 'enable' is a gpio that is used to enable a 5V buck regulator that may be powering the LED string. Although in practice the LEDS seem to work fine with
a 3.3 volt supply. An enable will help conserve power when the the player is in "standyby" since the ws2812 leds have a current draw even when off. 

- 'refresh' is delay between update of the VU meters in milliseconds, The default is 100, and cannot be lower than 30.

- 'bright' is the intensity of the LEDs, the default is 10 (out of 255)

Currently the only supported LEDs are the WS2812B strips. The number of LEDs specified should be odd due to an LED in the center between the two channels,
The display is layed out as follows:
```
RRROOOGGG...GRG...GGGOOORRR
```
Where R is an always on RED led in the center of the string,  The VU meters are green at lower sound levels starting on either side of the RED LED. 
At higher sound levels the leds are ORANGE then RED. The peak hold LED is BLUE.

Configuring the RGB leds will bring in the display driver and code that consumes the sound data that is also used by the OLED VU Meters and spectrum analyzer. 
Thus the OLED VUs may not be as responsive since they will be competing for audio data with the RGB leds that usually have faster update times.
This short coming will be addressed in a future release.


### Infrared
You can use any IR receiver compatible with NEC protocol (38KHz). Vcc, GND and output are the only pins that need to be connected, no pullup, no filtering capacitor, it's a straight connection.

The IR codes are send "as is" to LMS, so only a Logitech SB remote from Boom, Classic or Touch will work. I think the file Slim_Devices_Remote.ir in the "server" directory of LMS can be modified to adapt to other codes, but I've not tried that.

In AirPlay and Bluetooth mode, only these native remotes are supported, I've not added the option to make your own mapping

See "set GPIO" below to set the GPIO associated to infrared receiver (option "ir"). 

### Set GPIO
The parameter "set_GPIO" is use to assign GPIO to various functions.

GPIO can be set to GND provide or Vcc at boot. This is convenient to power devices that consume less than 40mA from the side connector. Be careful because there is no conflict checks being made wrt which GPIO you're changing, so you might damage your board or create a conflict here. 

The \<amp\> parameter can use used to assign a GPIO that will be set to 1 when playback starts. It will be reset to 0 when squeezelite becomes idle. The idle timeout is set on the squeezelite command line through -C \<timeout\>

If you have an audio jack that supports insertion (use :0 or :1 to set the level when inserted), you can specify which GPIO it's connected to. Using the parameter jack_mutes_amp allows to mute the amp when headset (e.g.) is inserted.

You can set the Green and Red status led as well with their respective active state (:0 or :1)

The \<ir\> parameter set the GPIO associated to an IR receiver. No need to add pullup or capacitor

Syntax is:

```
<gpio>=Vcc|GND|amp|ir|jack[:0|1]|green[:0|1]|red[:0|1]|spkfault[:0|1][,<repeated sequence for next GPIO>]
```
You can define the defaults for jack, spkfault leds at compile time but nvs parameter takes precedence except for SqueezeAMP where these are forced at runtime.
### Rotary Encoder
One rotary encoder is supported, quadrature shift with press. Such encoders usually have 2 pins for encoders (A and B), and common C that must be set to ground and an optional SW pin for press. A, B and SW must be pulled up, so automatic pull-up is provided by ESP32, but you can add your own resistors. A bit of filtering on A and B (~470nF) helps for debouncing which is not made by software. 

Encoder is normally hard-coded to respectively knob left, right and push on LMS and to volume down/up/play toggle on BT and AirPlay. Using the option 'volume' makes it hard-coded to volume down/up/play toggle all the time (even in LMS). The option 'longpress' allows an alternate mode when SW is long-pressed. In that mode, left is previous, right is next and press is toggle. Every long press on SW alternates between modes.

Use parameter rotary_config with the following syntax:

```
A=<gpio>,B=<gpio>[,SW=gpio>[,volume][,longpress]]
```

HW note: all gpio used for rotary have internal pull-up so normally there is no need to provide Vcc to the encoder. Nevertheless if the encoder board you're using also has its own pull-up that are stronger than ESP32's ones (which is likely the case), then there will be crosstalk between gpio, so you must bring Vcc. Look at your board schematic and you'll understand that these board pull-up create a "winning" pull-down when any other pin is grounded. 

See also the "IMPORTANT NOTE" on the "Buttons" section

### Buttons
Buttons are described using a JSON string with the following syntax
```
[
{"gpio":<num>,		
 "type":"BUTTON_LOW | BUTTON_HIGH",	
 "pull":[true|false],
 "long_press":<ms>, 
 "debounce":<ms>,
 "shifter_gpio":<-1|num>,
 "normal": {"pressed":"<action>","released":"<action>"},
 "longpress": { <same> },
 "shifted": { <same> },
 "longshifted": { <same> },
 },
 { ... },
 { ... },
] 
```

Where (all parameters are optionals except gpio) 
 - "type": (BUTTON_LOW) logic level when the button is pressed 
 - "pull": (false) activate internal pull up/down
 - "long_press": (0) duration (in ms) of keypress to detect long press, 0 to disable it
 - "debounce": (0) debouncing duration in ms (0 = internal default of 50 ms)
 - "shifter_gpio": (-1) gpio number of another button that can be pressed together to create a "shift". Set to -1 to disable shifter
 - "normal": ({"pressed":"ACTRLS_NONE","released":"ACTRLS_NONE"}) action to take when a button is pressed/released (see below)
 - "longpress": action to take when a button is long-pressed/released (see above/below)
 - "shifted": action to take when a button is pressed/released and shifted (see above/below)
 - "longshifted": action to take when a button is long-pressed/released and shifted (see above/below)

Where \<action\> is either the name of another configuration to load (remap) or one amongst

```
ACTRLS_NONE, ACTRLS_VOLUP, ACTRLS_VOLDOWN, ACTRLS_TOGGLE, ACTRLS_PLAY, 
ACTRLS_PAUSE, ACTRLS_STOP, ACTRLS_REW, ACTRLS_FWD, ACTRLS_PREV, ACTRLS_NEXT, 
BCTRLS_UP, BCTRLS_DOWN, BCTRLS_LEFT, BCTRLS_RIGHT,
KNOB_LEFT, KNOB_RIGHT, KNOB_PUSH
```
				
One you've created such a string, use it to fill a new NVS parameter with any name below 16(?) characters. You can have as many of these configs as you can. Then set the config parameter "actrls_config" with the name of your default config

For example a config named "buttons" :
```
[{"gpio":4,"type":"BUTTON_LOW","pull":true,"long_press":1000,"normal":{"pressed":"ACTRLS_VOLDOWN"},"longpress":{"pressed":"buttons_remap"}},
 {"gpio":5,"type":"BUTTON_LOW","pull":true,"shifter_gpio":4,"normal":{"pressed":"ACTRLS_VOLUP"}, "shifted":{"pressed":"ACTRLS_TOGGLE"}}]
``` 
Defines two buttons
- first on GPIO 4, active low. When pressed, it triggers a volume down command. When pressed more than 1000ms, it changes the button configuration for the one named "buttons_remap"
- second on GPIO 5, active low. When pressed it triggers a volume up command. If first button is pressed together with this button, then a play/pause toggle command is generated.

While the config named "buttons_remap"
```
[{"gpio":4,"type":"BUTTON_LOW","pull":true,"long_press":1000,"normal":{"pressed":"BCTRLS_DOWN"},"longpress":{"pressed":"buttons"}},
 {"gpio":5,"type":"BUTTON_LOW","pull":true,"shifter_gpio":4,"normal":{"pressed":"BCTRLS_UP"}}]
``` 
Defines two buttons
- first on GPIO 4, active low. When pressed, it triggers a navigation down command. When pressed more than 1000ms, it changes the button configuration for the one described above
- second on GPIO 5, active low. When pressed it triggers a navigation up command. That button, in that configuration, has no shift option

Below is a difficult but functional 2-buttons interface for your decoding pleasure

*buttons*
```
[{"gpio":4,"type":"BUTTON_LOW","pull":true,"long_press":1000,
 "normal":{"pressed":"ACTRLS_VOLDOWN"},
 "longpress":{"pressed":"buttons_remap"}},
 {"gpio":5,"type":"BUTTON_LOW","pull":true,"long_press":1000,"shifter_gpio":4,
 "normal":{"pressed":"ACTRLS_VOLUP"}, 
 "shifted":{"pressed":"ACTRLS_TOGGLE"}, 
 "longpress":{"pressed":"ACTRLS_NEXT"}}
]
```
*buttons_remap*
```
[{"gpio":4,"type":"BUTTON_LOW","pull":true,"long_press":1000,
 "normal":{"pressed":"BCTRLS_DOWN"},
 "longpress":{"pressed":"buttons"}},
 {"gpio":5,"type":"BUTTON_LOW","pull":true,"long_press":1000,"shifter_gpio":4,
 "normal":{"pressed":"BCTRLS_UP"},
 "shifted":{"pressed":"BCTRLS_PUSH"},
 "longpress":{"pressed":"ACTRLS_PLAY"},
 "longshifted":{"pressed":"BCTRLS_LEFT"}}
]
```
<strong>IMPORTANT NOTE</strong>: LMS also supports the possibility to send 'raw' button codes. It's a bit complicated, so bear with me. Buttons can either be processed by SqueezeESP32 and mapped to a "function" like play/pause or they can be just sent to LMS as plain (raw) code and the full logic of press/release/longpress is handled by LMS, you don't have any control on that.

The benefit of the "raw" mode is that you can build a player which is as close as possible to a Boom (e.g.) but you can't use the remapping function nor longress or shift logics to do your own mapping when you have a limited set of buttons. In 'raw' mode, all you really need to define is the mapping between the gpio and the button. As far as LMS is concerned, any other option in these JSON payloads does not matter. Now, when you use BT or AirPlay, the full JSON construct described above fully applies, so the shift, longpress, remapping options still work. 

There is no good or bad option, it's your choice. Use the NVS parameter "lms_ctrls_raw" to change that option

### Battery / ADC
The NVS parameter "bat_config" sets the ADC1 channel used to measure battery/DC voltage. Scale is a float ratio applied to every sample of the 12 bits ADC. A measure is taken every 10s and an average is made every 5 minutes (not a sliding window). Syntax is
```
channel=0..7,scale=<scale>
```
NB: Set parameter to empty to disable battery reading
## Setting up ESP-IDF
### Docker
#### **************** todo: Docker scripts needs some rework.

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
<<<<<<< HEAD
Follow the instructions from https://docs.espressif.com/projects/esp-idf/en/v4.0/get-started/index.html to install the esp-idf v4.0. This is the currently supported release of the espressif software development system. 
=======
<strong>Currently the master branch of this project requires this [IDF](https://github.com/espressif/esp-idf/tree/28f1cdf5ed7149d146ad5019c265c8bc3bfa2ac9) with gcc 5.2 (toolschain dated 20181001)
If you want to use a more recent version of gcc and IDF (4.0 stable), move to cmake-master branch</strong>

You can install IDF manually on Linux or Windows (using the Subsystem for Linux) following the instructions at: https://www.instructables.com/id/ESP32-Development-on-Windows-Subsystem-for-Linux/
And then copying the i2s.c patch file from this repo over to the esp-idf folder
You also need to use esp-dsp recent version or at least make sure you have this patch https://github.com/espressif/esp-dsp/pull/12/commits/8b082c1071497d49346ee6ed55351470c1cb4264
>>>>>>> refs/remotes/origin/master

## Building Squeezelite-esp32
MOST IMPORTANT: create the right default config file
- make defconfig
(Note: You can also copy over config files from the build-scripts folder to ./sdkconfig)
Then adapt the config file to your wifi/BT/I2C device (can also be done on the command line)
- make menuconfig
Then

```
idf.py -p PORT [-b BAUD] flash
idf.py -p PORT [-b BAUD] monitor

```

# Configuration
1/ setup WiFi
- Boot the esp, look for a new wifi access point showing up and connect to it. Default build ssid and passwords are "squeezelite"/"squeezelite". 
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
- for codecs libraries, add -mlongcalls if you want to rebuild them, but you should not (use the provided ones in codecs/lib). if you really want to rebuild them, open an issue
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
	- stack consumption can be very high with some codec variants, so set NONTHREADSAFE_PSEUDOSTACK and GLOBAL_STACK_SIZE=32000 and unset VAR_ARRAYS in config.h
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
- When initially cloning the repo, make sure you do it recursively. For example: 
	- git clone --recursive https://github.com/sle118/squeezelite-esp32.git
- If you have already cloned the repository and you are getting compile errors on one of the submodules (e.g. telnet), run the following git command in the root of the repository location
	-  git submodule update --init --recursive

