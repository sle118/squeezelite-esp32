/* 
 *  Squeezelite - lightweight headless squeezebox emulator
 *
 *  (c) Adrian Smith 2012-2015, triode1@btinternet.com
 *      Ralph Irving 2015-2017, ralph_irving@hotmail.com
 *  
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Additions (c) Paul Hermann, 2015-2017 under the same license terms
 *   -Control of Raspberry pi GPIO for amplifier power
 *   -Launch script on power status change from LMS
 */

#include "squeezelite.h"

#include <signal.h>

#define TITLE "Squeezelite " VERSION ", Copyright 2012-2015 Adrian Smith, 2015-2019 Ralph Irving."

#define CODECS_BASE "flac,pcm,mp3,ogg"
#if NO_FAAD
#define CODECS_AAC  ""
#else
#define CODECS_AAC  ",aac"
#endif
#if FFMPEG
#define CODECS_FF   ",wma,alac"
#else
#define CODECS_FF   ""
#endif
#if DSD
#define CODECS_DSD  ",dsd"
#else
#define CODECS_DSD  ""
#endif
#define CODECS_MP3  " (mad,mpg for specific mp3 codec)"

#define CODECS CODECS_BASE CODECS_AAC CODECS_FF CODECS_DSD CODECS_MP3

static void usage(const char *argv0) {
	printf(TITLE " See -t for license terms\n"
		   "Usage: %s [options]\n"
		   "  -s <server>[:<port>]\tConnect to specified server, otherwise uses autodiscovery to find server\n"
#if !EMBEDDED		   
           "  NB: use \"-disable\" (no quote) as server to disable LMS connection\n"
		   "  -o <output device>\tSpecify output device, default \"default\", - = output to stdout\n"
		   "  -l \t\t\tList output devices\n"
#endif		   
#if ALSA
		   "  -a <b>:<p>:<f>:<m>\tSpecify ALSA params to open output device, b = buffer time in ms or size in bytes, p = period count or size in bytes, f sample format (16|24|24_3|32), m = use mmap (0|1)\n"
#endif
#if PORTAUDIO
#if PA18API
		   "  -a <frames>:<buffers>\tSpecify output target 4 byte frames per buffer, number of buffers\n"
#elif OSX && !defined(OSXPPC)
		   "  -a <l>:<r>\t\tSpecify Portaudio params to open output device, l = target latency in ms, r = allow OSX to resample (0|1)\n"
#elif WIN
		   "  -a <l>:<e>\t\tSpecify Portaudio params to open output device, l = target latency in ms, e = use exclusive mode for WASAPI (0|1)\n"
#else
		   "  -a <l>\t\tSpecify Portaudio params to open output device, l = target latency in ms\n"
#endif
#endif
		   "  -a <f>\t\tSpecify sample format (16|24|32) of output file when using -o - to output samples to stdout (interleaved little endian only)\n"
		   "  -b <stream>:<output>\tSpecify internal Stream and Output buffer sizes in Kbytes\n"
		   "  -c <codec1>,<codec2>\tRestrict codecs to those specified, otherwise load all available codecs; known codecs: " CODECS "\n"
		   "  \t\t\tCodecs reported to LMS in order listed, allowing codec priority refinement.\n"
		   "  -C <timeout>\t\tClose output device when idle after timeout seconds, default is to keep it open while player is 'on'\n"
#if !IR
		   "  -d <log>=<level>\tSet logging level, logs: all|slimproto|stream|decode|output, level: info|debug|sdebug\n"
#else
		   "  -d <log>=<level>\tSet logging level, logs: all|slimproto|stream|decode|output|ir, level: info|debug|sdebug\n"
#endif
#if defined(GPIO) && defined(RPI)
		   "  -G <Rpi GPIO#>:<H/L>\tSpecify the BCM GPIO# to use for Amp Power Relay and if the output should be Active High or Low\n"
#endif
		   "  -e <codec1>,<codec2>\tExplicitly exclude native support of one or more codecs; known codecs: " CODECS "\n"
		   "  -f <logfile>\t\tWrite debug to logfile\n"
#if IR
		   "  -i [<filename>]\tEnable lirc remote control support (lirc config file ~/.lircrc used if filename not specified)\n"
#endif
		   "  -m <mac addr>\t\tSet mac address, format: ab:cd:ef:12:34:56\n"
		   "  -M <modelname>\tSet the squeezelite player model name sent to the server (default: " MODEL_NAME_STRING ")\n"
		   "  -n <name>\t\tSet the player name\n"
		   "  -N <filename>\t\tStore player name in filename to allow server defined name changes to be shared between servers (not supported with -n)\n"
		   "  -W\t\t\tRead wave and aiff format from header, ignore server parameters\n"
#if ALSA
		   "  -p <priority>\t\tSet real time priority of output thread (1-99)\n"
#endif
#if LINUX || FREEBSD || SUN
		   "  -P <filename>\t\tStore the process id (PID) in filename\n"
#endif
		   "  -r <rates>[:<delay>]\tSample rates supported, allows output to be off when squeezelite is started; rates = <maxrate>|<minrate>-<maxrate>|<rate1>,<rate2>,<rate3>; delay = optional delay switching rates in ms\n"
#if GPIO
			"  -S <Power Script>\tAbsolute path to script to launch on power commands from LMS\n"
#endif
#if RESAMPLE
		   "  -R -u [params]\tResample, params = <recipe>:<flags>:<attenuation>:<precision>:<passband_end>:<stopband_start>:<phase_response>,\n" 
		   "  \t\t\t recipe = (v|h|m|l|q)(L|I|M)(s) [E|X], E = exception - resample only if native rate not supported, X = async - resample to max rate for device, otherwise to max sync rate\n"
		   "  \t\t\t flags = num in hex,\n"
		   "  \t\t\t attenuation = attenuation in dB to apply (default is -1db if not explicitly set),\n"
		   "  \t\t\t precision = number of bits precision (NB. HQ = 20. VHQ = 28),\n"
		   "  \t\t\t passband_end = number in percent (0dB pt. bandwidth to preserve. nyquist = 100%%),\n"
		   "  \t\t\t stopband_start = number in percent (Aliasing/imaging control. > passband_end),\n"
		   "  \t\t\t phase_response = 0-100 (0 = minimum / 50 = linear / 100 = maximum)\n"
#endif
#if RESAMPLE16
		   "  -R -u [params]\tResample, params = (b|l|m)[:i],\n" 
		   "   \t\t\t b = basic linear interpolation, l = 13 taps, m = 21 taps, i = interpolate filter coefficients\n"
#endif
#if DSD
#if ALSA
		   "  -D [delay][:format]\tOutput device supports DSD, delay = optional delay switching between PCM and DSD in ms\n"
		   "  \t\t\t format = dop (default if not specified), u8, u16le, u16be, u32le or u32be.\n"
#else
		   "  -D [delay]\t\tOutput device supports DSD over PCM (DoP), delay = optional delay switching between PCM and DoP in ms\n"
#endif
#endif
#if VISEXPORT
		   "  -v \t\t\tVisualiser support\n"
#endif
# if ALSA
		   "  -O <mixer device>\tSpecify mixer device, defaults to 'output device'\n"
		   "  -L \t\t\tList volume controls for output device\n"
		   "  -U <control>\t\tUnmute ALSA control and set to full volume (not supported with -V)\n"
		   "  -V <control>\t\tUse ALSA control for volume adjustment, otherwise use software volume adjustment\n"
		   "  -X \t\t\tUse linear volume adjustments instead of in terms of dB (only for hardware volume control)\n"
#endif
#if LINUX || FREEBSD || SUN
		   "  -z \t\t\tDaemonize\n"
#endif
#if RESAMPLE || RESAMPLE16
		   "  -Z <rate>\t\tReport rate to server in helo as the maximum sample rate we can support\n"
#endif
		   "  -t \t\t\tLicense terms\n"
		   "  -? \t\t\tDisplay this help text\n"
		   "\n"
		   "Build options:"
#if SUN
		   " SOLARIS"
#elif LINUX
		   " LINUX"
#endif
#if WIN
		   " WIN"
#endif
#if OSX
		   " OSX"
#endif
#if OSXPPC
		   "PPC"
#endif
#if FREEBSD
		   " FREEBSD"
#endif
#if ALSA
		   " ALSA"
#endif
#if PORTAUDIO
		   " PORTAUDIO"
#if PA18API
		   "18"
#endif
#endif
#if EMBEDDED
			" EMBEDDED"
#endif	
#if EVENTFD
		   " EVENTFD"
#endif
#if SELFPIPE
		   " SELFPIPE"
#endif		   
#if LOOPBACK
		   " LOOPBACK"
#endif
#if WINEVENT
		   " WINEVENT"
#endif
#if RESAMPLE_MP
		   " RESAMPLE_MP"
#else
#if RESAMPLE
		   " RESAMPLE"
#endif
#if RESAMPLE16
		   " RESAMPLE16"
#endif
#endif
#if FFMPEG
		   " FFMPEG"
#endif
#if NO_FAAD
		   " NO_FAAD"
#endif
#if VISEXPORT
		   " VISEXPORT"
#endif
#if IR
		   " IR"
#endif
#if GPIO
		   " GPIO"
#endif
#if RPI
		   " RPI"
#endif
#if DSD
		   " DSD"
#endif
#if USE_SSL
                   " SSL"
#endif
#if NO_SSLSYM
                   " NO_SSLSYM"
#endif
#if LINKALL
		   " LINKALL"
#endif
		   "\n\n",
		   argv0);
}

static void license(void) {
	printf(TITLE "\n\n"
		   "This program is free software: you can redistribute it and/or modify\n"
		   "it under the terms of the GNU General Public License as published by\n"
		   "the Free Software Foundation, either version 3 of the License, or\n"
		   "(at your option) any later version.\n\n"
		   "This program is distributed in the hope that it will be useful,\n"
		   "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
		   "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
		   "GNU General Public License for more details.\n\n"
		   "You should have received a copy of the GNU General Public License\n"
		   "along with this program.  If not, see <http://www.gnu.org/licenses/>.\n"
#if DSD		   
		   "\nContains dsd2pcm library Copyright 2009, 2011 Sebastian Gesemann which\n"
		   "is subject to its own license.\n"
		   "\nContains the Daphile Project full dsd patch Copyright 2013-2017 Daphile,\n"
		   "which is subject to its own license.\n"
#endif
		   "\nOption to allow server side upsampling for PCM streams (-W) from\n"
		   "squeezelite-R2 (c) Marco Curti 2015, marcoc1712@gmail.com.\n"
#if GPIO
		   "\nAdditions (c) Paul Hermann, 2015, 2017 under the same license terms\n"
		   "- Launch a script on power status change\n"
		   "- Control of Raspberry pi GPIO for amplifier power\n"
#endif
#if RPI
		   "\nContains wiringpi GPIO Interface library Copyright (c) 2012-2017\n"
		   "Gordon Henderson, which is subject to its own license.\n"
#endif
#if FFMPEG
		   "\nThis software uses libraries from the FFmpeg project under\n"
		   "the LGPLv2.1 and its source can be downloaded from\n"
		   "<https://sourceforge.net/projects/lmsclients/files/source/>\n"
#endif
		   "\n"
		   );
}

static void sighandler(int signum) {
	slimproto_stop();

	// remove ourselves in case above does not work, second SIGINT will cause non gracefull shutdown
	signal(signum, SIG_DFL);
}

#ifndef EMBEDDED
int main(int argc, char **argv) {
#else
int squeezelite_main(int argc, char **argv) {
#endif
	char *server = NULL;
	char *output_device = "default";
	char *include_codecs = NULL;
	char *exclude_codecs = "";
	char *name = NULL;
	char *namefile = NULL;
	char *modelname = NULL;
	extern bool pcm_check_header;
	extern bool user_rates;
	char *logfile = NULL;
	u8_t mac[6];
	unsigned stream_buf_size = STREAMBUF_SIZE;
	unsigned output_buf_size = 0; // set later
	unsigned rates[MAX_SUPPORTED_SAMPLERATES] = { 0 };
	unsigned rate_delay = 0;
	char *resample = NULL;
	char *output_params = NULL;
	unsigned idle = 0;
#if LINUX || FREEBSD || SUN
	bool daemonize = false;
	char *pidfile = NULL;
	FILE *pidfp = NULL;
#endif
#if ALSA
	unsigned rt_priority = OUTPUT_RT_PRIORITY;
	char *mixer_device = output_device;
	char *output_mixer = NULL;
	bool output_mixer_unmute = false;
	bool linear_volume = false;
#endif
#if DSD
	unsigned dsd_delay = 0;
	dsd_format dsd_outfmt = PCM;
#endif
#if VISEXPORT
	bool visexport = false;
#endif
#if IR
	char *lircrc = NULL;
#endif

#if EMBEDDED
    int err = embedded_init();
    if (err) return err;
#endif    
	
	log_level log_output = lWARN;
	log_level log_stream = lWARN;
	log_level log_decode = lWARN;
	log_level log_slimproto = lWARN;
#if IR
	log_level log_ir     = lWARN;
#endif

	int maxSampleRate = 0;

	char *optarg = NULL;
	int optind = 1;
	int i;

#define MAXCMDLINE 512
	char cmdline[MAXCMDLINE] = "";

	get_mac(mac);

	for (i = 0; i < argc && (strlen(argv[i]) + strlen(cmdline) + 2 < MAXCMDLINE); i++) {
		strcat(cmdline, argv[i]);
		strcat(cmdline, " ");
	}

	while (optind < argc && strlen(argv[optind]) >= 2 && argv[optind][0] == '-') {
		char *opt = argv[optind] + 1;
		if (strstr("oabcCdefmMnNpPrsZ"
#if ALSA
				   "UVO"
#endif
				   , opt) && optind < argc - 1) {
			optarg = argv[optind + 1];
			optind += 2;
		} else if (strstr("ltz?W"
#if ALSA
						  "LX"
#endif
#if RESAMPLE || RESAMPLE16
						  "uR"
#endif
#if DSD
						  "D"
#endif
#if VISEXPORT
						  "v"
#endif
#if IR
						  "i"
#endif
#if defined(GPIO) && defined(RPI)
						  "G"
#endif
#if GPIO
						  "S"
#endif

						  , opt)) {
			optarg = NULL;
			optind += 1;
		} else {
			LOG_ERROR("=> Option error: -%s", opt);
			usage(argv[0]);
			exit(1);
		}

		switch (opt[0]) {
		case 'o':
			output_device = optarg;
#if ALSA
			mixer_device = optarg;
#endif
			break;
		case 'a':
			output_params = optarg;
			break;
		case 'b': 
			{
				char *s = next_param(optarg, ':');
				char *o = next_param(NULL, ':');
				if (s) stream_buf_size = atoi(s) * 1024;
				if (o) output_buf_size = atoi(o) * 1024;
			}
			break;
		case 'c':
			include_codecs = optarg;
			break;
		case 'C':
			if (atoi(optarg) > 0) {
				idle = atoi(optarg) * 1000;
			}
			break;
		case 'e':
			exclude_codecs = optarg;
			break;
		case 'd':
			{
				char *l = strtok(optarg, "=");
				char *v = strtok(NULL, "=");
				log_level new = lWARN;
				if (l && v) {
					if (!strcmp(v, "info"))   new = lINFO;
					if (!strcmp(v, "debug"))  new = lDEBUG;
					if (!strcmp(v, "sdebug")) new = lSDEBUG;
					if (!strcmp(l, "all") || !strcmp(l, "slimproto")) log_slimproto = new;
					if (!strcmp(l, "all") || !strcmp(l, "stream"))    log_stream = new;
					if (!strcmp(l, "all") || !strcmp(l, "decode"))    log_decode = new;
					if (!strcmp(l, "all") || !strcmp(l, "output"))    log_output = new;
#if IR
					if (!strcmp(l, "all") || !strcmp(l, "ir"))        log_ir     = new;
#endif
				} else {
					LOG_ERROR("=> Debug settings error: -d %s", optarg);
					usage(argv[0]);
					exit(1);
				}
			}
			break;
		case 'f':
			logfile = optarg;
			break;
		case 'm':
			{
				int byte = 0;
				char *tmp;
				if (!strncmp(optarg, "00:04:20", 8)) {
					LOG_ERROR("=> ignoring mac address from hardware player range 00:04:20:**:**:**");
				} else {
					char *t = strtok(optarg, ":");
					while (t && byte < 6) {
						mac[byte++] = (u8_t)strtoul(t, &tmp, 16);
						t = strtok(NULL, ":");
					}
				}
			}
			break;
		case 'M':
			modelname = optarg;
			break;
		case 'r':
			{ 
				char *rstr = next_param(optarg, ':');
				char *dstr = next_param(NULL, ':');
				if (rstr && strstr(rstr, ",")) {
					// parse sample rates and sort them
					char *r = next_param(rstr, ',');
					unsigned tmp[MAX_SUPPORTED_SAMPLERATES] = { 0 };
					int i, j;
					int last = 999999;
					for (i = 0; r && i < MAX_SUPPORTED_SAMPLERATES; ++i) { 
						tmp[i] = atoi(r);
						r = next_param(NULL, ',');
					}
					for (i = 0; i < MAX_SUPPORTED_SAMPLERATES; ++i) {
						int largest = 0;
						for (j = 0; j < MAX_SUPPORTED_SAMPLERATES; ++j) {
							if (tmp[j] > largest && tmp[j] < last) {
								largest = tmp[j];
							}
						}
						rates[i] = last = largest;
					}
				} else if (rstr) {
					// optstr is <min>-<max> or <max>, extract rates from test rates within this range
					unsigned ref[] TEST_RATES;
					char *str1 = next_param(rstr, '-');
					char *str2 = next_param(NULL, '-');
					unsigned max = str2 ? atoi(str2) : (str1 ? atoi(str1) : ref[0]);
					unsigned min = str1 && str2 ? atoi(str1) : 0;
					unsigned tmp;
					int i, j;
					if (max < min) { tmp = max; max = min; min = tmp; }
					rates[0] = max;
					for (i = 0, j = 1; i < MAX_SUPPORTED_SAMPLERATES; ++i) {
						if (ref[i] < rates[j-1] && ref[i] >= min) {
							rates[j++] = ref[i];
						}
					}
				}
				if (dstr) {
					rate_delay = atoi(dstr);
				}
				if (rates[0]) {
					user_rates = true;
				}
			}
			break;
		case 's':
			server = optarg;
			break;
		case 'n':
			name = optarg;
			break;
		case 'N':
			namefile = optarg;
			break;
		case 'Z':
			maxSampleRate = atoi(optarg);
			break;			
		case 'W':
			pcm_check_header = true;
			break;
#if ALSA
		case 'p':
			rt_priority = atoi(optarg);
			if (rt_priority > 99 || rt_priority < 1) {
				fprintf(stderr, "\nError: invalid priority: %s\n\n", optarg);
				usage(argv[0]);
				exit(1);
			}
			break;
#endif
#if LINUX || FREEBSD || SUN
		case 'P':
			pidfile = optarg;
			break;
#endif
#if !EMBEDDED
		case 'l':
			list_devices();
			exit(0);
			break;
#endif			
#if RESAMPLE || RESAMPLE16
		case 'u':
		case 'R':
			if (optind < argc && argv[optind] && argv[optind][0] != '-') {
				resample = argv[optind++];
			} else {
				resample = "";
			}
			break;
#endif
#if DSD
		case 'D':
			dsd_outfmt = DOP;
			if (optind < argc && argv[optind] && argv[optind][0] != '-') {
				char *dstr = next_param(argv[optind++], ':');
				char *fstr = next_param(NULL, ':');
				dsd_delay = dstr ? atoi(dstr) : 0;
				if (fstr) {
					if (!strcmp(fstr, "dop")) dsd_outfmt = DOP; 
					if (!strcmp(fstr, "u8")) dsd_outfmt = DSD_U8; 
					if (!strcmp(fstr, "u16le")) dsd_outfmt = DSD_U16_LE; 
					if (!strcmp(fstr, "u32le")) dsd_outfmt = DSD_U32_LE; 
					if (!strcmp(fstr, "u16be")) dsd_outfmt = DSD_U16_BE; 
					if (!strcmp(fstr, "u32be")) dsd_outfmt = DSD_U32_BE;
					if (!strcmp(fstr, "dop24")) dsd_outfmt = DOP_S24_LE;
					if (!strcmp(fstr, "dop24_3")) dsd_outfmt = DOP_S24_3LE;
				}
			}
			break;
#endif
#if VISEXPORT
		case 'v':
			visexport = true;
			break;
#endif
#if ALSA
		case 'O':
			mixer_device = optarg;
			break;
		case 'L':
			list_mixers(mixer_device);
			exit(0);
			break;
		case 'X':
			linear_volume = true;
			break;
		case 'U':
			output_mixer_unmute = true;
		case 'V':
			if (output_mixer) {
				fprintf(stderr, "-U and -V option should not be used at same time\n");
				exit(1);
			}
			output_mixer = optarg;
			break;
#endif
#if IR
		case 'i':
			if (optind < argc && argv[optind] && argv[optind][0] != '-') {
				lircrc = argv[optind++];
			} else {
				lircrc = "~/.lircrc"; // liblirc_client will expand ~/
			}
			break;
#endif
#if defined(GPIO) && defined(RPI)
		case 'G':
			if (power_script != NULL){
				fprintf(stderr, "-G and -S options cannot be used together \n\n" );
				usage(argv[0]);
				exit(1);
			}
			if (optind < argc && argv[optind] && argv[optind][0] != '-') {
				char *gp = next_param(argv[optind++], ':');
				char *go = next_param (NULL, ':');
				gpio_pin = atoi(gp);
				if (go != NULL){
					if ((strcmp(go, "H")==0)|(strcmp(go, "h")==0)){
						gpio_active_low=false;
					}else if((strcmp(go, "L")==0)|(strcmp(go, "l")==0)){
						gpio_active_low=true;
					}else{
						fprintf(stderr,"Must set output to be active High or Low i.e. -G18:H or -G18:L\n");
						usage(argv[0]);
						exit(1);
					}
				}else{
					fprintf(stderr,"-G Option Error\n");
					usage(argv[0]);
					exit(1);
				}
				gpio_active = true;
				relay(0);

			} else {
				fprintf(stderr, "Error in GPIO Pin assignment.\n");
				usage(argv[0]);
				exit(1);
			}
			break;
#endif
#if GPIO
		case 'S':
			if (gpio_active){
				fprintf(stderr, "-G and -S options cannot be used together \n\n" );
				usage(argv[0]);
				exit(1);
			}
			if (optind < argc && argv[optind] && argv[optind][0] != '-') {
				power_script = argv[optind++];
				if( access( power_script, R_OK|X_OK ) == -1 ) {
				    // file doesn't exist
					fprintf(stderr, "Script %s, not found\n\n", argv[optind-1]);
					usage(argv[0]);
					exit(1);
				}
			} else {
				fprintf(stderr, "No Script Name Given.\n\n");
				usage(argv[0]);
				exit(1);
			}
			relay_script(0);
			break;
#endif
#if LINUX || FREEBSD || SUN
		case 'z':
			daemonize = true;
#if SUN
			init_daemonize();
#endif /* SUN */
			break;
#endif
		case 't':
			license();
			exit(0);
            break;
		case '?':
			usage(argv[0]);
			exit(0);
			break;
		default:
			LOG_ERROR("=> arg error: %s", argv[optind]);
			break;
		}
	}

	// warn if command line includes something which isn't parsed
	if (optind < argc) {
		LOG_ERROR("=> command line argument error");
		usage(argv[0]);
		exit(1);
	}

	signal(SIGINT, sighandler);
	signal(SIGTERM, sighandler);
#if defined(SIGQUIT)
	signal(SIGQUIT, sighandler);
#endif
#if defined(SIGHUP)
	signal(SIGHUP, sighandler);
#endif

#if USE_SSL && !LINKALL && !NO_SSLSYM
	ssl_loaded = load_ssl_symbols();
#endif	

	// set the output buffer size if not specified on the command line, take account of resampling
	if (!output_buf_size) {
		output_buf_size = OUTPUTBUF_SIZE;
		if (resample) {
			unsigned scale = 8;
			if (rates[0]) {
				scale = rates[0] / 44100;
				if (scale > 8) scale = 8;
				if (scale < 1) scale = 1;
			}
			output_buf_size *= scale;
		}
	}

	if (logfile) {
		if (!freopen(logfile, "a", stderr)) {
			fprintf(stderr, "error opening logfile %s: %s\n", logfile, strerror(errno));
		} else {
			if (log_output >= lINFO || log_stream >= lINFO || log_decode >= lINFO || log_slimproto >= lINFO) {
				fprintf(stderr, "\n%s\n", cmdline);
			}
		}
	}

#if LINUX || FREEBSD || SUN
	if (pidfile) {
		if (!(pidfp = fopen(pidfile, "w")) ) {
			fprintf(stderr, "Error opening pidfile %s: %s\n", pidfile, strerror(errno));
			exit(1);
		}
		pidfile = realpath(pidfile, NULL); // daemonize will change cwd
	}

	if (daemonize) {
		if (daemon(0, logfile ? 1 : 0)) {
			fprintf(stderr, "error daemonizing: %s\n", strerror(errno));
		}
	}

	if (pidfp) {
		fprintf(pidfp, "%d\n", (int) getpid());
		fclose(pidfp);
	}
#endif

#if WIN
	winsock_init();
#endif

	stream_init(log_stream, stream_buf_size);

#if EMBEDDED
	output_init_embedded();
#else
	if (!strcmp(output_device, "-")) {
		output_init_stdout(log_output, output_buf_size, output_params, rates, rate_delay);
	} else {
#if ALSA
		output_init_alsa(log_output, output_device, output_buf_size, output_params, rates, rate_delay, rt_priority, idle, mixer_device, output_mixer,
						 output_mixer_unmute, linear_volume);
#endif
#if PORTAUDIO
		output_init_pa(log_output, output_device, output_buf_size, output_params, rates, rate_delay, idle);
#endif
	}
#endif	

#if DSD
	dsd_init(dsd_outfmt, dsd_delay);
#endif

#if VISEXPORT
	if (visexport) {
		output_vis_init(log_output, mac);
	}
#endif

	decode_init(log_decode, include_codecs, exclude_codecs);

#if RESAMPLE || RESAMPLE16
	if (resample) {
		process_init(resample);
	}
#endif

#if IR
	if (lircrc) {
		ir_init(log_ir, lircrc);
	}
#endif

	if (name && namefile) {
		LOG_ERROR("=> -n and -N option should not be used at same time");
		exit(1);
	}

#if EMBEDDED
    if (server && !strcasecmp(server, "-disable")) {
        LOG_ERROR("LMS is disabled");
        while (1) sleep(3600);
    }
#endif
	slimproto(log_slimproto, server, mac, name, namefile, modelname, maxSampleRate);

	decode_close();
	stream_close();

#if EMBEDDED
	output_close_embedded();	
#else
	if (!strcmp(output_device, "-")) {
		output_close_stdout();
	} else {
#if ALSA
		output_close_alsa();
#endif
#if PORTAUDIO
		output_close_pa();
#endif
	}
#endif	

#if IR
	ir_close();
#endif

#if WIN
	winsock_close();
#endif

#if LINUX || FREEBSD || SUN
	if (pidfile) {
		unlink(pidfile);
		free(pidfile);
	}
#endif

#if USE_SSL && !LINKALL && !NO_SSLSYM
	free_ssl_symbols();
#endif	

	return(0);
}
