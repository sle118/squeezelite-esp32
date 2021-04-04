![Cross-Build](https://github.com/sle118/squeezelite-esp32/workflows/Cross-Build/badge.svg?branch=master-cmake)
# Squeezelite-esp32
## What is this
Squeezelite-esp32 is an audio software suite made to run on espressif's ESP32 wifi (b/g/n) and bluetooth chipset. It offers the following capabilities

- Stream your local music and connect to all major on-line music providers (Spotify, Deezer, Tidal, Qobuz) using [Logitech Media Server - a.k.a LMS](https://forums.slimdevices.com/) and enjoy multi-room audio synchronization. LMS can be extended by numerous plugins and can be controlled using a Web browser or dedicated applications (iPhone, Android). It can also send audio to UPnP, Sonos, ChromeCast and AirPlay speakers/devices.
- Stream from a Bluetooth device (iPhone, Android)
- Stream from an AirPlay controller (iPhone, iTunes ...) and enjoy synchronization multiroom as well (although it's AirPlay 1 only)

Depending on the hardware connected to the ESP32, you can send audio to a local DAC, to SPDIF or to a Bluetooth speaker. The bare minimum required hardware is a WROVER module with 4MB of Flash and 4MB of PSRAM (https://www.espressif.com/en/products/modules/esp32). With that module standalone, just apply power and you can stream to a Bluetooth speaker. You can also send audio to most I2S DAC as well as to SPDIF receivers using just a cable or an optical transducer.

But squeezelite-esp32 is highly extensible and you can add

- Buttons and Rotary Encoder and map/combine them to various functions (play, pause, volume, next ...)
- IR receiver (no pullup resistor or capacitor needed, just the 38kHz receiver)
- Monochrome, GrayScale or Color displays using SPI or I2C (supported drivers are SH1106, SSD1306, SSD1322, SSD1326/7, SSD1351, ST7735, ST7789 and ILI9341).

Other features include

 - Resampling 
 - 10-bands equalizer
 - Automatic initial setup using any WiFi device 
 - Full web interface for further configuration/management
 - Firmware over-the-air update 

## Performances 
*(opinions presented here so I = @philippe44)*
The main build of squeezelite-esp32 is a 16 bits internal core with all calculations in 32 bits or float precision. This is a design choice I've made to preserve CPU performances (it is already stretching a lot the esp32 chipset) and optimize memory usage as we only have 4MB of usable RAM. Some might correctly comment that the WROVER module have 8MB of RAM, but the processor is only able to address 4MB and the remaining 4MB must be paginated by smaller blocks and I don't have patience to that. 

Now, when I did the porting of squeezelite to esp32, I've also made the core 16 or 32 bits compatible at compile-time. So far, it works in 32 bits but less tests have been done. You can chose to compile it in 32 bits mode. I'm not very interested above 16 bits samples because it does not bring anything (I have an engineering background in theory of information). 

| Capability                 |16 bits|32 bits| comment         						         |	
|----------------------------|-------|-------|-------------------------------------------------------------------|
| max sampling rate          |  192k |  96k  | 192k is very challenging, especially when combined with display   |
| max bit depth              |  16   |  24   | 24 bits are truncated in 16 bits mode                             | 
| spdif			     |16 bits|20 bits|		                                                         |
| mp3, aac, opus, ogg/vorbis |  48k  |  48k  |		                                                         |
| alac, flac, ogg/flac       |  96k  |  96k  | 		                                                         |
| pcm, wav, aif              |  192k |  96k  |		                                                         |
| equalizer                  |   Y   |   N   | 48kHz max (after resampling) - equalization skipped on 96k tracks |
| resampling                 |   Y   |   N   |		                                                         |
| cross-fade                 |  10s  |  <5s  | depends on buffer size and sampling rate		                 |

The esp32 must run at 240 MHz, with Quad-SPI I/O at 80 MHz and a clock of 40 Mhz. Still, it's a lot to run, especially knowing that it has a serial Flash and PSRAM, so kudos to Espressif for their chipset optimization. Now, to have all the decoding, resampling, equalizing, gain, display, spectrum/vu is a very (very) delicate equilibrium between use of internal /external RAM, tasks priorities and buffer handling. It is not perfect and the more you push the system to the limit, the higher the risk that some files would not play (see below). In general, the display will always have the lowest priority and you'll notice slowdown in scrolling and VU/Spectrum refresh rates. Now, even display thread has some critical section and impacts the capabilities. For example, a 16 bits-depth color display with low SPI speed might prevent 24/96 flac to work but still work with pcm 24/96

In 16 bits mode, although 192 kHz is reported as max rate, it's highly recommended to limit reported sampling rate to 96k (-Z 96000). Note that some high-speed 24/96k on-line streams might stutter because of TCP/IP stack performances. It is usually due to the fact that the server sends small packets of data and the esp32 cannot receive encoded audio fast enough, regardless of task priority settings (I've tried to tweak that a fair bit). The best option in that case is to let LMS proxy the stream as it will provide larger chunks and a "smoother" stream that can then be handled.
## Supported Hardware
Any esp32-based hardware with at least 4MB of flash and 4MB of PSRAM will be capable of running squeezelite-esp32 and there are various boards that include such chip. A few are mentionned below, but any should work. You can find various help & instructions [here](https://forums.slimdevices.com/showthread.php?112697-ANNOUNCE-Squeezelite-ESP32-(dedicated-thread))
### Raw WROVER module
Per above description, a [WROVER module](https://www.espressif.com/en/products/modules/esp32) is enough to run Squeezelite-esp32, but that requires a bit of tinkering to extend it to have analogue audio or hardware buttons (e.g.) 

Please note that when sending to a Bluetooth speaker (source), only 44.1 kHz can be used, so you either let LMS do the resampling, but you must make sure it only sends 44.1kHz tracks or enable internal resampling (using -R) option. If you connect a DAC, choice of sample rates will depends on its capabilities. See below for more details.

Most DAC will work out-of-the-box with simply an I2S connection, but some require specific commands to be sent using I2C. See DAC option below to understand how to send these dedicated commands. There is build-in support for TAS575x, TAS5780, TAS5713 and AC101 DAC.
### SqueezeAMP
This is the main hardware companion of Squeezelite-esp32 and has been developped together. Details on capabilities can be found [here](https://forums.slimdevices.com/showthread.php?110926-pre-ANNOUNCE-SqueezeAMP-and-SqueezeliteESP32) and [here](https://github.com/philippe44/SqueezeAMP).

if you want to rebuild, use the `squeezelite-esp32-SqueezeAmp-sdkconfig.defaults` configuration file.

NB: You can use the pre-build binaries SqueezeAMP4MBFlash which has all the hardware I/O set properly. You can also use the generic binary I2S4MBFlash in which case the NVS parameters shall be set to get the exact same behavior
- set_GPIO: 12=green,13=red,34=jack,2=spkfault
- batt_config: channel=7,scale=20.24
- dac_config: model=TAS57xx,bck=33,ws=25,do=32,sda=27,scl=26,mute=14:0
- spdif_config: bck=33,ws=25,do=15

### ESP32-A1S
Works with [ESP32-A1S](https://docs.ai-thinker.com/esp32-a1s) module that includes audio codec and headset output. You still need to use a demo board like [this](https://www.aliexpress.com/item/4000765857347.html?spm=2114.12010615.8148356.11.5d963cd0j669ns) or an external amplifier if you want direct speaker connection. 

The board shown above has the following IO set
- amplifier: GPIO21
- key2: GPIO13, key3: GPIO19, key4: GPIO23, key5: GPIO18, key6: GPIO5 (to be confirmed with dip switches)
- key1: not sure, using GPIO36 in a matrix
- jack insertion: GPIO39 (inserted low)
- D4 -> GPIO22 used for green LED (active low)
- D5 -> GPIO19 (muxed with key3)
- The IO connector also brings GPIO5, GPIO18, GPIO19, GPIO21, GPIO22 and GPIO23 (don't forget it's muxed with keys!)
- The JTAG connector uses GPIO 12, 13, 14 and 15 (see dip switch) but these are also used for SD-card (and GPIO13 is key2 as well)
- It's always possible to re-use GPIOO (download at boot) and GPIO1/GPIO3 which are RX/TX of UART0 but you'll lose trace

(note that some GPIO need pullups)

So a possible config would be
- set_GPIO: 21=amp,22=green:0,39=jack:0
- dac_config: model=AC101,bck=27,ws=26,do=25,di=35,sda=33,scl=32
- a button mapping: 
```
[{"gpio":5,"normal":{"pressed":"ACTRLS_TOGGLE"}},{"gpio":18,"pull":true,"shifter_gpio":5,"normal":{"pressed":"ACTRLS_VOLUP"}, "shifted":{"pressed":"ACTRLS_NEXT"}}, {"gpio":23,"pull":true,"shifter_gpio":5,"normal":{"pressed":"ACTRLS_VOLDOWN"},"shifted":{"pressed":"ACTRLS_PREV"}}]
```
### T-WATCH2020 by LilyGo
This is a fun [smartwatch](http://www.lilygo.cn/prod_view.aspx?TypeId=50036&Id=1290&FId=t3:50036:3) based on ESP32. It has a 240x240 ST7789 screen and onboard audio. Not very useful to listen to anything but it works. This is an example of a device that requires an I2C set of commands for its dac (see below). There is a build-option if you decide to rebuild everything by yourself, otherwise the I2S default option works with the following parameters

- dac_config: model=I2S,bck=26,ws=25,do=33,i2c=106,sda=21,scl=22
- dac_controlset: { "init": [ {"reg":41, "val":128}, {"reg":18, "val":255} ], "poweron": [ {"reg":18, "val":64, "mode":"or"} ], "poweroff": [ {"reg":18, "val":191, "mode":"and"} ] }
- spi_config: dc=27,data=19,clk=18
- display_config: SPI,driver=ST7789,width=240,height=240,cs=5,back=12,speed=16000000,HFlip,VFlip
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

### SqueezeAmpToo !

And the super cool project https://github.com/rochuck/squeeze-amp-too

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
bck=<gpio>,ws=<gpio>,do=<gpio>[,mute=<gpio>[:0|1][,model=TAS57xx|TAS5713|AC101|I2S][,sda=<gpio>,scl=gpio[,i2c=<addr>]]
```
if "model" is not set or is not recognized, then default "I2S" is used. I2C parameters are optional an only needed if your dac requires an I2C control (See 'dac_controlset' below). Note that "i2c" parameters are decimal, hex notation is not allowed.

The parameter "dac_controlset" allows definition of simple commands to be sent over i2c for init, power on and off using a JSON syntax:
```
{ init: [ {"reg":<register>,"val":<value>,"mode":<nothing>|"or"|"and"}, ... {{"reg":<register>,"val":<value>,"mode":<nothing>|"or"|"and"} ],
  poweron: [ {"reg":<register>,"val":<value>,"mode":<nothing>|"or"|"and"}, ... {{"reg":<register>,"val":<value>,"mode":<nothing>|"or"|"and"} ],
  poweroff: [ {"reg":<register>,"val":<value>,"mode":<nothing>|"or"|"and"}, ... {{"reg":<register>,"val":<value>,"mode":<nothing>|"or"|"and"} ] }
```
This is standard JSON notation, so if you are not familiar with it, Google is your best friend. Be aware that the '...' means you can have as many entries as you want, it's not part of the syntax. Every section is optional, but it does not make sense to set i2c in the 'dac_config' parameter and not setting anything here. The parameter 'mode' allows to *or* the register with the value or to *and* it. Don't set 'mode' if you simply want to write. **Note that all values must be decimal**. You can use a validator like [this](https://jsonlint.com) to verify your syntax

NB: For well-known configuration, this is ignored
### SPDIF
The NVS parameter "spdif_config" sets the i2s's gpio needed for SPDIF. 

SPDIF is made available by re-using i2s interface in a non-standard way, so although only one pin (DO) is needed, the controller must be fully initialized, so the bit clock (bck) and word clock (ws) must be set as well. As i2s and SPDIF are mutually exclusive, you can reuse the same IO if your hardware allows so.

You can define the defaults at compile time but nvs parameter takes precedence except for SqueezeAMP where these are forced at runtime.

Leave it blank to disable SPDIF usage, you can also define them at compile time using "make menuconfig". Syntax is 
```
bck=<gpio>,ws=<gpio>,do=<gpio>
```
NB: For well-known configuration, this is ignored

To optimize speed, a bit-manipulation trick is used and as a result, the bit depth is limited to 20 bits, even in 32 bits mode. As said before, this is more than enough for any human ear. In theory, it could be extended up to 23 bits but I don't see the need. Now, you can also get SPDIF using a specialized chip that offers a I2S interface like a DAC but spits out SPDIF (optical and coax). Refers to DAC chapter then.

If you want coax, you can also use a poor-man's trick to generate signal from a 3.3V GPIO. All that does is dividing the 3.3V to generate a 0.6V peak-to-peak and then remove DC
```
                          100nF
GPIO  ----210ohm-----------||---- coax S/PDIF signal out
                    |
                  110ohm
                    |
Ground -------------------------- coax signal ground
```
### Display
The NVS parameter "display_config" sets the parameters for an optional display. Syntax is
```
I2C,width=<pixels>,height=<pixels>[address=<i2c_address>][,reset=<gpio>][,HFlip][,VFlip][driver=SSD1306|SSD1326[:1|4]|SSD1327|SH1106]
SPI,width=<pixels>,height=<pixels>,cs=<gpio>[,back=<gpio>][,reset=<gpio>][,speed=<speed>][,HFlip][,VFlip][driver=SSD1306|SSD1322|SSD1326[:1|4]|SSD1327|SH1106|SSD1675|ST7735|ST7789|ILI9341[:16|18][,rotate]]
```
- back: a LED backlight used by some older devices (ST7735). It is PWM controlled for brightness
- reset: some display have a reset pin that is should normally be pulled up if unused
- VFlip and HFlip are optional can be used to change display orientation
- rotate: for non-square *drivers*, move to portrait mode. Note that *width* and *height* must be inverted then
- Default speed is 8000000 (8MHz) but SPI can work up to 26MHz or even 40MHz
- SH1106 is 128x64 monochrome I2C/SPI [here]((https://www.waveshare.com/wiki/1.3inch_OLED_HAT))
- SSD1306 is 128x32 monochrome I2C/SPI [here](https://www.buydisplay.com/i2c-blue-0-91-inch-oled-display-module-128x32-arduino-raspberry-pi)
- SSD1322 is 128x128 16-level grayscale SPI [here](https://www.amazon.com/gp/product/B079N1LLG8/ref=ox_sc_act_title_1?smid=A1N6DLY3NQK2VM&psc=1) - artwork can be up to 96x96 with vertical vu-meter/spectrum
- SSD1351 is 128x128 65k/262k color SPI [here](https://www.waveshare.com/product/displays/lcd-oled/lcd-oled-3/1.5inch-rgb-oled-module.htm)
- SSD1326 is 256x32 monochrome or grayscale 16-levels SPI [here](https://www.aliexpress.com/item/32833603664.html?spm=a2g0o.productlist.0.0.2d19776cyQvsBi&algo_pvid=c7a3db92-e019-4095-8a28-dfdf0a087f98&algo_expid=c7a3db92-e019-4095-8a28-dfdf0a087f98-1&btsid=0ab6f81e15955375483301352e4208&ws_ab_test=searchweb0_0,searchweb201602_,searchweb201603_)
- SSD1327 is 256x64 grayscale 16-levels SPI in multiple sizes [here](https://www.buydisplay.com/oled-display/oled-display-module?resolution=159) - it is very nice
- SSD1675 is an e-ink paper and is experimental as e-ink is really not suitable for LMS du to its very low refresh rate
- ST7735 is a 128x160 65k color SPI [here](https://www.waveshare.com/product/displays/lcd-oled/lcd-oled-3/1.8inch-lcd-module.htm). This needs a backlight control
- ST7789 is a 240x320 65k (262k not enabled) color SPI [here](https://www.waveshare.com/product/displays/lcd-oled/lcd-oled-3/2inch-lcd-module.htm). It also exist with 240x240 displays. See **rotate** for use in portrait mode
- ILI9341 is another 240x320 65k (262k capable) color SPI. I've not used it much, the driver it has been provided by one external contributor to the project

To use the display on LMS, add repository https://raw.githubusercontent.com/sle118/squeezelite-esp32/master/plugin/repo.xml. You will then be able to tweak how the vu-meter and spectrum analyzer are displayed, as well as size of artwork. You can also install the excellent plugin "Music Information Screen" which is super useful to tweak the layout.

The NVS parameter "metadata_config" sets how metadata is displayed for AirPlay and Bluetooth. Syntax is
```
[format=<display_content>][,speed=<speed>][,pause=<pause>]
```
- 'speed' is the scrolling speed in ms (default is 33ms)

- 'pause' is the pause time between scrolls in ms (default is 3600ms)

- 'format' can contain free text and any of the 3 keywords %artist%, %album%, %title%. Using that format string, the keywords are replaced by their value to build the string to be displayed. Note that the plain text following a keyword that happens to be empty during playback of a track will be removed. For example, if you have set format=%artist% - %title% and there is no artist in the metadata then only <title> will be displayed not " - <title>".

### Infrared
You can use any IR receiver compatible with NEC protocol (38KHz). Vcc, GND and output are the only pins that need to be connected, no pullup, no filtering capacitor, it's a straight connection.

The IR codes are send "as is" to LMS, so only a Logitech SB remote from Boom, Classic or Touch will work. I think the file Slim_Devices_Remote.ir in the "server" directory of LMS can be modified to adapt to other codes, but I've not tried that.

In AirPlay and Bluetooth mode, only these native remotes are supported, I've not added the option to make your own mapping

See "set GPIO" below to set the GPIO associated to infrared receiver (option "ir"). 

### Set GPIO
The parameter "set_GPIO" is used to assign GPIO to various functions.

GPIO can be set to GND provide or Vcc at boot. This is convenient to power devices that consume less than 40mA from the side connector. Be careful because there is no conflict checks being made wrt which GPIO you're changing, so you might damage your board or create a conflict here. 

The \<amp\> parameter can use used to assign a GPIO that will be set to active level (default 1) when playback starts. It will be reset when squeezelite becomes idle. The idle timeout is set on the squeezelite command line through -C \<timeout\>

If you have an audio jack that supports insertion (use :0 or :1 to set the level when inserted), you can specify which GPIO it's connected to. Using the parameter jack_mutes_amp allows to mute the amp when headset (e.g.) is inserted.

You can set the Green and Red status led as well with their respective active state (:0 or :1)

The \<ir\> parameter set the GPIO associated to an IR receiver. No need to add pullup or capacitor

Syntax is:

```
<gpio>=Vcc|GND|amp[:1|0]|ir|jack[:0|1]|green[:0|1]|red[:0|1]|spkfault[:0|1][,<repeated sequence for next GPIO>]
```
You can define the defaults for jack, spkfault leds at compile time but nvs parameter takes precedence except for well-known configurations where these are forced at runtime.
### LED 
See §**set_GPIO** for how to set the green and red LEDs. In addition, their brightness can be controlled using the "led_brigthness" parameter. The syntax is
```
[green=0..100][,red=0..100]
```
NB: For well-known configuration, this is ignored
### Rotary Encoder
One rotary encoder is supported, quadrature shift with press. Such encoders usually have 2 pins for encoders (A and B), and common C that must be set to ground and an optional SW pin for press. A, B and SW must be pulled up, so automatic pull-up is provided by ESP32, but you can add your own resistors. A bit of filtering on A and B (~470nF) helps for debouncing which is not made by software. 

Encoder is normally hard-coded to respectively knob left, right and push on LMS and to volume down/up/play toggle on BT and AirPlay. Using the option 'volume' makes it hard-coded to volume down/up/play toggle all the time (even in LMS). The option 'longpress' allows an alternate mode when SW is long-pressed. In that mode, left is previous, right is next and press is toggle. Every long press on SW alternates between modes (the main mode actual behavior depends on 'volume').

There is also the possibility to use 'knobonly' option (exclusive with 'volume' and 'longpress'). This mode attempts to offer a single knob full navigation which is a bit contorded due to LMS UI's principles. Left, Right and Press obey to LMS's navigation rules and especially Press always goes to lower submenu item, even when navigating in the Music Library. That causes a challenge as there is no 'Play', 'Back' or 'Pause' button. Workaround are as of below:
- longpress is 'Play'
- double press is 'Back' (Left in LMS's terminology). 
- a quick left-right movement on the encoder is 'Pause' 

The speed of double click (or left-right) can be set using the optional parameter of 'knobonly'. This is not a perfect solution, and other ideas are welcome. Be aware that the longer you set double click speed, the less responsive the interface will be. The reason is that I need to wait for that delay before deciding if it's a single or double click. It can also make menu navigation "hesitations" being easily interpreted as 'Pause'

Use parameter rotary_config with the following syntax:

```
A=<gpio>,B=<gpio>[,SW=gpio>[[,knobonly[=<ms>]]|[[,volume][,longpress]]]]
```

HW note: all gpio used for rotary have internal pull-up so normally there is no need to provide Vcc to the encoder. Nevertheless if the encoder board you're using also has its own pull-up that are stronger than ESP32's ones (which is likely the case), then there will be crosstalk between gpio, so you must bring Vcc. Look at your board schematic and you'll understand that these board pull-up create a "winning" pull-down when any other pin is grounded. 

The SW gpio is optional, you can re-affect it to a pure button if you prefer but the volume, longpress and knobonly options make little sense as the missing switch plays an important role in these modes. You could still have the "volume" mode, but you won't be able to use it for *anything* expect volume up and down. So be aware that the use of syntax [] is a bit misleading hereabove.

See also the "IMPORTANT NOTE" on the "Buttons" section and remember that when 'lms_ctrls_raw' (see below) is activated, none of these knobonly,volume,longpress options apply, raw button codes (not actions) are simply sent to LMS

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

**Be aware that when using non "raw" mode, the CLI (Command Line Interface) of LMS is used and *must* be available without password**

There is no good or bad option, it's your choice. Use the NVS parameter "lms_ctrls_raw" to change that option

### Battery / ADC
The NVS parameter "bat_config" sets the ADC1 channel used to measure battery/DC voltage. Scale is a float ratio applied to every sample of the 12 bits ADC. A measure is taken every 10s and an average is made every 5 minutes (not a sliding window). Syntax is
```
channel=0..7,scale=<scale>,cells=<2|3>
```
NB: Set parameter to empty to disable battery reading. For well-known configuration, this is ignored (except for SqueezeAMP where number of cells is required)
# Configuration
## Setup WiFi
- Boot the esp, look for a new wifi access point showing up and connect to it. Default build ssid and passwords are "squeezelite"/"squeezelite". 
- Once connected, navigate to 192.168.4.1 
- Wait for the list of access points visible from the device to populate in the web page.
- Choose an access point and enter any credential as needed
- Once connection is established, note down the address the device received; this is the address you will use to configure it going forward 

## Setup squeezelite command line (optional)

At this point, the device should have disabled its built-in access point and should be connected to a known WiFi network.
- navigate to the address that was noted in step #1
- Using the list of predefined options, choose the mode in which you want squeezelite to start
- Generate the command
- Add or change any additional command line option (for example player name, etc)
- Activate squeezelite execution: this tells the device to automatiaclly run the command at start
- Update the configuration
- click on the "start toggle" button. This will force a reboot. 
- The toggle switch should be set to 'ON' to ensure that squeezelite is active after booting (you might have to fiddle with it a few times)
- You can enable accessto  NVS parameters under 'credits'

## Monitor

In addition of the esp-idf serial link monitor option, you can also enable a telnet server (see NVS parameters) where you'll have access to a ton of logs of what's happening inside the WROVER.

## Update Squeezelite
- From the firmware tab, click on "Check for Updates"
- Look for updated binaries
- Select a line
- Click on "Flash!"
- The system will reboot into recovery mode (if not already in that mode), wipe the squeezelite partition and download/flash the selected version 
- You can choose a local file or have a local webserver

## Recovery
- From the firmware tab, click on the "Recovery" button. This will reboot the ESP32 into recovery, where additional configuration options are available from the NVS editor

## Additional configuration notes (from the Web UI)
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
	- C <sec> : set timeout to switch off amp gpio
	- W : activate WAV and AIFF header parsing
# Building everything yourself
## Setting up ESP-IDF
### Docker
You can use docker to build squeezelite-esp32 (optional) 
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
You can install IDF manually on Linux or Windows (using the Subsystem for Linux) following the instructions at: https://www.instructables.com/id/ESP32-Development-on-Windows-Subsystem-for-Linux/ or see here https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/windows-setup.html for a direct install. 

**Use the esp-idf 4.0 https://github.com/espressif/esp-idf/tree/release/v4.0 and a recent add esp-dsp (after 08/2020)**

## Building Squeezelite-esp32
When initially cloning the repo, make sure you do it recursively. For example: `git clone --recursive https://github.com/sle118/squeezelite-esp32.git`
	
Don't forget to choose one of the config files in build_scripts/ and rename it sdkconfig.defaults or sdkconfig as many important WiFi/BT options are set there. **The codecs libraries will not be rebuilt by these scripts (it's a tedious process - see below)**

Create and tweak your config using `idf.py menuconfig` then build binaries using `idf.py all`. It will build the recovery and the application (squeezelite). then use `idf.py flash` to write everything. Otherwise, if you just want to download squeezelite, do (assuming you have set ESPPORT (e.g. COM10) and ESPBAUD (e.g. 921600)
```
<path_to_your_python>/python.exe <path_to_your_esptool>/esptool.py -p %ESPPORT% -b %ESPBAUD% --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size detect --flash_freq 80m 0x150000 build/squeezelite.bin
```
Use `idf.py monitor` to monitor the application (see esp-idf documentation)

Note: You can use `idf.py build -DDEPTH=32` to build the 32 bits version and add the `-DVERSION=<your_version>` to add a custom version name (it will be 0.0-<your_version>). If you want to change the whole version string, see squeezelite.h

If you have already cloned the repository and you are getting compile errors on one of the submodules (e.g. telnet), run the following git command in the root of the repository location: `git submodule update --init --recursive`
### Rebuild codecs (highly recommended to NOT try that)
- for codecs libraries, add -mlongcalls if you want to rebuild them, but you should not (use the provided ones in codecs/lib). if you really want to rebuild them, open an issue
- libmad, libflac (no esp's version), libvorbis (tremor - not esp's version), alac work
- libfaad does not really support real time, but if you want to try (but using helixaac is a better option)
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
- libmad has been patched to avoid using a lot of stack and is not provided here. There is an issue with sync detection in 1.15.1b from where the original stack patch was done but since a few fixes have been made wrt sync detection. This 1.15.1b-10 found on debian fixes the issue where mad thinks it has reached sync but has not and so returns a wrong sample rate. It comes at the expense of 8KB (!) of code where a simple check in squeezelite/mad.c that next_frame[0] is 0xff and next_frame[1] & 0xf0 is 0xf0 does the trick ...

# Footnotes
(1) SPDIF is made by tricking the I2S bus but this consumes a fair bit of CPU as it multiplies by four the throughput on the i2s bus. To optimize some computation, the parity of the spdif frames must always be 0, so at least one bit has to be available to force it. As SPDIF samples are 20+4 bits length maximum, the LSB is used for that purpose, so the bit 24 is randomly toggling. It does not matter for 16 bits samples but it has been chosen to truncate the last 4 bits for 24 bits samples. I'm sure that some smart dude can further optimize spdif_convert() and use the user bit instead. You're welcome to do a PR but, as said above, I (philippe44) am not interested by 24 bits mental illness :-) and I've already made an effort to provide 20 bits which already way more what's needed :-)
