/* cmd_i2ctools.c

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
//#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#include <stdio.h>
#include "cmd_config.h"
#include "argtable3/argtable3.h"
#include "platform_console.h"
#include "esp_log.h"
#include "string.h"
#include "stdio.h"
#include "platform_config.h"
#include "trace.h"
#include "messaging.h"
#include "accessors.h"

const char * desc_squeezelite ="Squeezelite Options";
const char * desc_dac= "DAC Options";
const char * desc_spdif= "SPDIF Options";
const char * desc_audio= "General Audio Options";
const char * desc_bt_source= "Bluetooth Audio Output Options";
const char * desc_rotary= "Rotary Control";
const char * desc_ledvu= "Led Strip Options";


#define CODECS_BASE "flac|pcm|mp3|ogg"
#if NO_FAAD
#define CODECS_AAC  ""
#else
#define CODECS_AAC  "|aac"
#endif
#if FFMPEG
#define CODECS_FF   "|wma|alac"
#else
#define CODECS_FF   ""
#endif
#if DSD
#define CODECS_DSD  "|dsd"
#else
#define CODECS_DSD  ""
#endif
#define CODECS_MP3  "|mad|mpg"


#if !defined(MODEL_NAME)
#define MODEL_NAME SqueezeLite
#endif

#ifndef QUOTE
#define QUOTE(name) #name
#define STR(macro)  QUOTE(macro)
#endif
#ifndef MODEL_NAME_STRING
#define MODEL_NAME_STRING STR(MODEL_NAME)
#endif

#define CODECS CODECS_BASE CODECS_AAC CODECS_FF CODECS_DSD CODECS_MP3
#define NOT_OUTPUT "has input capabilities only"
#define NOT_GPIO "is not a GPIO"
typedef enum {
    SEARCHING_FOR_BT,
    SEARCHING_FOR_NAME,
    SEARCHING_FOR_NAME_START,
    SEARCHING_FOR_NAME_END,
    SEARCHING_FOR_BT_CMD_END,
    FINISHING
} parse_state_t;
static const char *TAG = "cmd_config";
extern struct arg_end *getParmsEnd(struct arg_hdr * * argtable);
//bck=<gpio>,ws=<gpio>,do=<gpio>[,mute=<gpio>[:0|1][,model=TAS57xx|TAS5713|AC101|WM8978|I2S][,sda=<gpio>,scl=gpio[,i2c=<addr>]]
static struct {
	struct arg_str *model_name;
    struct arg_int *clock;
    struct arg_int *wordselect;
    struct arg_int *data;
    struct arg_int *mute_gpio;
	struct arg_lit *mute_level;
    struct arg_int *dac_sda;
    struct arg_int *dac_scl;
    struct arg_int *dac_i2c;
	struct arg_lit *clear;
    struct arg_end *end;
} i2s_args;

static struct {
	struct arg_rem * rem;
	struct arg_int * A;
	struct arg_int * B;
	struct arg_int * SW;
	struct arg_lit * volume_lock;
	struct arg_lit * longpress;
	struct arg_lit * knobonly;
	struct arg_int * timer;
	struct arg_lit * clear;
	struct arg_end * end;
} rotary_args;
//config_rotary_get

static struct {
	struct arg_str * type;
	struct arg_int * length;
	struct arg_int * gpio;
	struct arg_lit * clear;
	struct arg_end * end;
} ledvu_args;

static struct{
		struct arg_str *sink_name;
		struct arg_str *pin_code;
//		struct arg_dbl *connect_timeout_delay;
//		struct arg_dbl *control_delay;
		struct arg_end *end;
} bt_source_args;

static struct {
    struct arg_int *clock;
    struct arg_int *wordselect;
    struct arg_int *data;
	struct arg_lit *clear;	
    struct arg_end *end;
} spdif_args;
static struct {
    struct arg_str *jack_behavior;	
    struct arg_end *end;
} audio_args;
static struct {
	struct arg_str * output_device; // "  -d <log>=<level>\tSet logging level, logs: all|slimproto|stream|decode|output|ir, level: info|debug|sdebug\n"
	struct arg_str * name;//			   "  -n <name>\t\tSet the player name\n"
	struct arg_str * server; // -s <server>[:<port>]\tConnect to specified server, otherwise uses autodiscovery to find server\n"
	struct arg_str * buffers;//			   "  -b <stream>:<output>\tSpecify internal Stream and Output buffer sizes in Kbytes\n"
	struct arg_str * codecs;//			   "  -c <codec1>,<codec2>\tRestrict codecs to those specified, otherwise load all available codecs; known codecs: " CODECS "\n"
	struct arg_int * timeout;//			   "  -C <timeout>\t\tClose output device when idle after timeout seconds, default is to keep it open while player is 'on'\n"
	struct arg_str * log_level; // "  -d <log>=<level>\tSet logging level, logs: all|slimproto|stream|decode|output|ir, level: info|debug|sdebug\n"
	// struct arg_str * log_level_all; // "  -d <log>=<level>\tSet logging level, logs: all|slimproto|stream|decode|output|ir, level: info|debug|sdebug\n"
	// struct arg_str * log_level_slimproto; // "  -d <log>=<level>\tSet logging level, logs: all|slimproto|stream|decode|output|ir, level: info|debug|sdebug\n"
	// struct arg_str * log_level_stream;
	// struct arg_str * log_level_decode;
	// struct arg_str * log_level_output;
	#if IR
	struct arg_str * log_level_ir;
	#endif
	//			   "  -e <codec1>,<codec2>\tExplicitly exclude native support of one or more codecs; known codecs: " CODECS "\n"
	//			   "  -f <logfile>\t\tWrite debug to logfile\n"
	//	#if IR
	//			   "  -i [<filename>]\tEnable lirc remote control support (lirc config file ~/.lircrc used if filename not specified)\n"
	//	#endif
	struct arg_str * mac_addr; //			   "  -m <mac addr>\t\tSet mac address, format: ab:cd:ef:12:34:56\n"
	struct arg_str * model_name;//			   "  -M <modelname>\tSet the squeezelite player model name sent to the server (default: " MODEL_NAME_STRING ")\n"
	struct arg_lit * header_format;//			   "  -W\t\t\tRead wave and aiff format from header, ignore server parameters\n"
	struct arg_str * rates; //			   "  -r <rates>[:<delay>]\tSample rates supported, allows output to be off when squeezelite is started; rates = <maxrate>|<minrate>-<maxrate>|<rate1>,<rate2>,<rate3>; delay = optional delay switching rates in ms\n"
	#if RESAMPLE
	struct arg_lit * resample;
	struct arg_str * resample_parms; //"  -R -u [params]\tResample, params = <recipe>:<flags>:<attenuation>:<precision>:<passband_end>:<stopband_start>:<phase_response>,\n"
	#endif
	#if RESAMPLE16
	struct arg_lit * resample;
	struct arg_str * resample_parms; //" -R -u [params]\tResample, params = (b|l|m)[:i],\n"
	//			   "   \t\t\t b = basic linear interpolation, l = 13 taps, m = 21 taps, i = interpolate filter coefficients\n"
	#endif
	struct arg_int * rate;//			   "  -Z <rate>\t\tReport rate to server in helo as the maximum sample rate we can support\n"
    struct arg_end *end;
} squeezelite_args;

int is_gpio(struct arg_int * gpio, FILE * f, int * gpio_out, bool mandatory, bool output){
	int res = 0;
	const char * name = gpio->hdr.longopts?gpio->hdr.longopts:gpio->hdr.glossary;
	*gpio_out=-1;
	int t_gpio=gpio->ival[0];
	if(gpio->count==0){
		if(mandatory){
			fprintf(f,"Missing: %s\n", name);
			res++;
		}
	} else  if((output && !GPIO_IS_VALID_OUTPUT_GPIO(t_gpio)) || (!GPIO_IS_VALID_GPIO(t_gpio))){
		fprintf(f,"Invalid %s gpio: [%d] %s\n",name, t_gpio, GPIO_IS_VALID_GPIO(t_gpio)?NOT_OUTPUT:NOT_GPIO );
		res++;
	}
	else{
		*gpio_out = t_gpio;
	}
	return res;
}
int is_output_gpio(struct arg_int * gpio, FILE * f, int * gpio_out, bool mandatory){
	return is_gpio(gpio,f,gpio_out,mandatory,true);
}
int check_missing_parm(struct arg_int * int_parm, FILE * f){
	int res=0;
	const char * name = int_parm->hdr.longopts?int_parm->hdr.longopts:int_parm->hdr.glossary;
	if(int_parm->count==0){
		fprintf(f,"Missing: %s\n", name);
		res++;
	} 
	return res;
}
char * strip_bt_name(char * opt_str)
{
    char *result = malloc(strlen(opt_str)+1);
    memset(result, 0x00, strlen(opt_str)+1);
    char *str = strdup(opt_str);
    const char * output_marker=" -o";
    
    if(!result ){
        ESP_LOGE(TAG,"Error allocating memory for result.");
        return opt_str;
    }
    if(!str){
        ESP_LOGE(TAG,"Error duplicating command line string.");
        return opt_str;
    }	
    bool quoted=false;
    parse_state_t state = SEARCHING_FOR_BT;
    char *start = strstr(str, output_marker);
    if (start)
    { 
        ESP_LOGV(TAG,"Found output option : %s\n",start);
        start+=strlen(output_marker);
        strncpy(result, str, (size_t)(start - str));
        char * pch=strtok(start," ");
        while(pch){
            ESP_LOGV(TAG,"Current output: %s\n[%s]",result,pch);
            switch (state)
            {
                case SEARCHING_FOR_BT:
                    if (strcasestr(pch, "BT") )
                    {
                        state = SEARCHING_FOR_NAME;
                        quoted=strcasestr(pch, "BT")!=NULL;
                        ESP_LOGV(TAG," - fount BT Start %s", quoted?"quoted":"");
                    }
                    else
                    {
                        ESP_LOGV(TAG," - Searching for BT, Ignoring");
                    }
                    strcat(result, " ");
                    strcat(result, pch);
                    break;
                case SEARCHING_FOR_NAME:
                    if (strcasestr(pch, "name") || strcasestr(pch, "n"))
                    {
                        ESP_LOGV(TAG," - Found name tag");
                        state = SEARCHING_FOR_NAME_START;
                    }
                    else
                    {
                        strcat(result, " ");
                        strcat(result, pch);
                        ESP_LOGV(TAG," - Searching for name - added ");;
                    }
                    break;
                case SEARCHING_FOR_NAME_START:
                    ESP_LOGV(TAG," - Name start");
                    state = SEARCHING_FOR_NAME_END;
                break;
                case SEARCHING_FOR_NAME_END:
                    if (strcasestr(pch, "\"")){
                        ESP_LOGV(TAG," - got quoted string");
                        state = FINISHING;
                    } 
                    else if(pch[0]== '-'){
                        strcat(result, " ");
                        strcat(result, pch);
                        ESP_LOGV(TAG," - got parameter marker");
                        state = quoted?SEARCHING_FOR_BT_CMD_END:FINISHING;
                    }
                    else {
                        ESP_LOGV(TAG," - name continued");
                    }
                    break;
                case SEARCHING_FOR_BT_CMD_END:
                    ESP_LOGV(TAG," - looking for quoted BT cmd end");
                    if (strcasestr(pch, "\"")){
                        ESP_LOGV(TAG," - got quote termination");
                        state = FINISHING;
                    } 
                    strcat(result, " ");
                    strcat(result, pch);
                    break;
                case FINISHING:
                    strcat(result, " ");
                    strcat(result, pch);
                    break;
                default:
    
                    break;
            }
            pch = strtok(NULL, " ");
            ESP_LOGV(TAG,"\n");
        }
    }
    else
    {
        ESP_LOGE(TAG,"output option not found in %s\n",str);
        strcpy(result,str);
    }

    ESP_LOGV(TAG,"Result commmand : %s\n", result);
    free(str);
    return result;
}

static int do_bt_source_cmd(int argc, char **argv){
	esp_err_t err=ESP_OK;
	int nerrors = arg_parse(argc, argv,(void **)&bt_source_args);
	char *buf = NULL;
	size_t buf_size = 0;
//	char value[100] ={0};
	FILE *f = open_memstream(&buf, &buf_size);
	if (f == NULL) {
		cmd_send_messaging(argv[0],MESSAGING_ERROR,"Unable to open memory stream.\n");
		return 1;
	}
	if(nerrors >0){
		arg_print_errors(f,bt_source_args.end,desc_bt_source);
		return 1;
	}

	if(bt_source_args.sink_name->count >0){
		err = config_set_value(NVS_TYPE_STR, "a2dp_sink_name", bt_source_args.sink_name->sval[0]);
		if(err!=ESP_OK){
            nerrors++;
            fprintf(f,"Error setting Bluetooth audio device name %s. %s\n",bt_source_args.sink_name->sval[0], esp_err_to_name(err));
        }
        else {
            fprintf(f,"Bluetooth audio device name changed to %s\n",bt_source_args.sink_name->sval[0]);
        }        
		char * squeezelite_cmd = config_alloc_get_default(NVS_TYPE_STR, "autoexec1", NULL, 0);
		if( squeezelite_cmd && strstr(squeezelite_cmd," -o ") ){
			char * new_cmd = strip_bt_name(squeezelite_cmd);
			if(strcmp(new_cmd,squeezelite_cmd)!=0){
				fprintf(f,"Replacing old squeezelite command [%s] with [%s].\n",squeezelite_cmd,new_cmd);
				config_set_value(NVS_TYPE_STR, "autoexec1", new_cmd);
				if(err!=ESP_OK){
					nerrors++;
					fprintf(f,"Error updating squeezelite command line options . %s\n", esp_err_to_name(err));
				}			
			}
			free(squeezelite_cmd);
			free(new_cmd);
		}

	}
	if(bt_source_args.pin_code->count >0){
		const char * v=bt_source_args.pin_code->sval[0];
		bool bInvalid=false;
		for(int i=0;i<strlen(v) && !bInvalid;i++){
			if(v[i]<'0' || v[i]>'9'){
				bInvalid=true;
			}
		}
		if(bInvalid || strlen(bt_source_args.pin_code->sval[0])>16 || strlen(bt_source_args.pin_code->sval[0])<4){
            nerrors++;
            fprintf(f,"Pin code %s invalid. Should be numbers only with length between 4 and 16 characters. \n",bt_source_args.pin_code->sval[0]);
		}
		else {
			err = config_set_value(NVS_TYPE_STR, "a2dp_spin", bt_source_args.pin_code->sval[0]);
			if(err!=ESP_OK){
				nerrors++;
				fprintf(f,"Error setting Bluetooth source pin to %s. %s\n",bt_source_args.pin_code->sval[0], esp_err_to_name(err));
			}
			else {
				fprintf(f,"Bluetooth source pin changed to %s\n",bt_source_args.pin_code->sval[0]);
			}        
		}
	}	
	// if(bt_source_args.connect_timeout_delay->count >0){
		
	// 	snprintf(value,sizeof(value),"%d",(int)(bt_source_args.connect_timeout_delay->dval[0]*1000.0));
	// 	if(bt_source_args.connect_timeout_delay->dval[0] <0.5 || bt_source_args.connect_timeout_delay->dval[0] >5.0){
	// 		nerrors++;
	// 		fprintf(f,"Invalid connection timeout %0.0f (%s milliseconds). Value must be between 0.5 sec and 5 sec.\n", bt_source_args.connect_timeout_delay->dval[0], value );
	// 	}
	// 	else {
	// 		err = config_set_value(NVS_TYPE_STR, "a2dp_ctmt", value);
	// 		if(err!=ESP_OK){
	// 			nerrors++;
	// 			fprintf(f,"Error setting connection timeout %0.0f sec (%s milliseconds). %s\n", bt_source_args.connect_timeout_delay->dval[0],value, esp_err_to_name(err));
	// 		}
	// 		else {
	// 			fprintf(f,"Connection timeout changed to %0.0f sec (%s milliseconds)\n",bt_source_args.connect_timeout_delay->dval[0],value);
	// 		}     
	// 	}   
	// }

	// if(bt_source_args.control_delay->count >0){
	// 	snprintf(value,sizeof(value),"%d",(int)(bt_source_args.control_delay->dval[0]*1000.0));
	// 	if(bt_source_args.control_delay->dval[0] <0.1 || bt_source_args.control_delay->dval[0] >2.0){
	// 		nerrors++;
	// 		fprintf(f,"Invalid control delay %0.0f (%s milliseconds). Value must be between 0.1s and 2s.\n", bt_source_args.control_delay->dval[0], value );
	// 	}
	// 	else {
	// 		err = config_set_value(NVS_TYPE_STR, "a2dp_ctrld", value);
	// 		if(err!=ESP_OK){
	// 			nerrors++;
	// 			fprintf(f,"Error setting control delay to %0.0f sec (%s milliseconds). %s\n",bt_source_args.control_delay->dval[0],value, esp_err_to_name(err));
	// 		}
	// 		else {
	// 			fprintf(f,"Control delay changed to %0.0f sec (%s milliseconds)\n",bt_source_args.control_delay->dval[0],value);
	// 		}     
	// 	}   
	// }

	if(!nerrors ){
		fprintf(f,"Done.\n");
	}
	fflush (f);
	cmd_send_messaging(argv[0],nerrors>0?MESSAGING_ERROR:MESSAGING_INFO,"%s", buf);
	fclose(f);
	FREE_AND_NULL(buf);
	return (nerrors==0 && err==ESP_OK)?0:1;

}
static int do_audio_cmd(int argc, char **argv){
	esp_err_t err=ESP_OK;
	int nerrors = arg_parse(argc, argv,(void **)&audio_args);
	char *buf = NULL;
	size_t buf_size = 0;
	FILE *f = open_memstream(&buf, &buf_size);
	if (f == NULL) {
		cmd_send_messaging(argv[0],MESSAGING_ERROR,"Unable to open memory stream.\n");
		return 1;
	}
	if(nerrors >0){
		arg_print_errors(f,audio_args.end,desc_audio);
		return 1;
	}

    if(audio_args.jack_behavior->count>0){
        err = ESP_OK; // suppress any error code that might have happened in a previous step
        if(strcasecmp(audio_args.jack_behavior->sval[0],"Headphones")==0){
            err = config_set_value(NVS_TYPE_STR, "jack_mutes_amp", "y");
        }
        else if(strcasecmp(audio_args.jack_behavior->sval[0],"Subwoofer")==0){
            err = config_set_value(NVS_TYPE_STR, "jack_mutes_amp", "n");
        }
        else {
            nerrors++;
            fprintf(f,"Unknown Audio Jack Behavior %s.\n",audio_args.jack_behavior->sval[0]);
        }

        if(err!=ESP_OK){
            nerrors++;
            fprintf(f,"Error setting Audio Jack Behavior %s. %s\n",audio_args.jack_behavior->sval[0], esp_err_to_name(err));
        }
        else {
            fprintf(f,"Audio Jack Behavior changed to %s\n",audio_args.jack_behavior->sval[0]);
        }        
    }

	if(!nerrors ){
		fprintf(f,"Done.\n");
	}
	fflush (f);
	cmd_send_messaging(argv[0],nerrors>0?MESSAGING_ERROR:MESSAGING_INFO,"%s", buf);
	fclose(f);
	FREE_AND_NULL(buf);
	return (nerrors==0 && err==ESP_OK)?0:1;
}
static int do_spdif_cmd(int argc, char **argv){
		i2s_platform_config_t i2s_dac_pin = {
		.i2c_addr = -1,
		.sda= -1,
		.scl = -1,
		.mute_gpio = -1,
		.mute_level = -1
	};
	if(is_spdif_config_locked()){
		cmd_send_messaging(argv[0],MESSAGING_ERROR,"SPDIF Configuration is locked on this platform\n");
		return 1;
	}
	esp_err_t err=ESP_OK;
	int nerrors = arg_parse(argc, argv,(void **)&spdif_args);
	if (spdif_args.clear->count) {
		cmd_send_messaging(argv[0],MESSAGING_WARNING,"SPDIF config cleared\n");
		config_set_value(NVS_TYPE_STR, "spdif_config", "");
		return 0;
	}

	char *buf = NULL;
	size_t buf_size = 0;
	FILE *f = open_memstream(&buf, &buf_size);
	if (f == NULL) {
		cmd_send_messaging(argv[0],MESSAGING_ERROR,"Unable to open memory stream.\n");
		return 1;
	}
	if(nerrors >0){
		arg_print_errors(f,spdif_args.end,desc_dac);
		return 1;
	}
	nerrors+=is_output_gpio(spdif_args.clock, f, &i2s_dac_pin.pin.bck_io_num, true);
	nerrors+=is_output_gpio(spdif_args.wordselect, f, &i2s_dac_pin.pin.ws_io_num, true);
	nerrors+=is_output_gpio(spdif_args.data, f, &i2s_dac_pin.pin.data_out_num, true);
	if(!nerrors ){
		fprintf(f,"Storing SPDIF parameters.\n");
		nerrors+=(config_spdif_set(&i2s_dac_pin )!=ESP_OK);
	}
	if(!nerrors ){
		fprintf(f,"Done.\n");
	}
	fflush (f);
	cmd_send_messaging(argv[0],nerrors>0?MESSAGING_ERROR:MESSAGING_INFO,"%s", buf);
	fclose(f);
	FREE_AND_NULL(buf);
	return (nerrors==0 && err==ESP_OK)?0:1;
}

static int do_rotary_cmd(int argc, char **argv){
	rotary_struct_t rotary={  .A = -1, .B = -1, .SW = -1, .longpress = 0, .knobonly=0,.volume_lock=false};
	esp_err_t err=ESP_OK;
	int nerrors = arg_parse(argc, argv,(void **)&rotary_args);
	if (rotary_args.clear->count) {
		cmd_send_messaging(argv[0],MESSAGING_WARNING,"rotary config cleared\n");
		config_set_value(NVS_TYPE_STR, "rotary_config", "");
		return 0;
	}

	char *buf = NULL;
	size_t buf_size = 0;
	FILE *f = open_memstream(&buf, &buf_size);
	if (f == NULL) {
		cmd_send_messaging(argv[0],MESSAGING_ERROR,"Unable to open memory stream.\n");
		return 1;
	}
	if(nerrors >0){
		arg_print_errors(f,rotary_args.end,desc_rotary);
		return 1;
	}
	nerrors+=is_gpio(rotary_args.A, f, &rotary.A, true,false);
	nerrors+=is_gpio(rotary_args.B, f, &rotary.B, true,false);
	nerrors+=is_gpio(rotary_args.SW, f, &rotary.SW,false,false);


	if(rotary_args.knobonly->count>0 && (rotary_args.volume_lock->count>0 || rotary_args.longpress->count>0)){
		fprintf(f,"error: Cannot use volume lock or longpress option when knob only option selected\n");
		nerrors++;
	}
	if(rotary_args.timer->count>0 && rotary_args.timer->ival[0]<0){
		fprintf(f,"error: knob only timer should be greater than or equal to zero.\n");
		nerrors++;
	}
	else {
		rotary.timer = rotary_args.timer->count>0?rotary_args.timer->ival[0]:0;
	}
	rotary.knobonly = rotary_args.knobonly->count>0;
	rotary.volume_lock= rotary_args.volume_lock->count>0;
	rotary.longpress = rotary_args.longpress->count>0;
	if(!nerrors ){
		fprintf(f,"Storing rotary parameters.\n");
		nerrors+=(config_rotary_set(&rotary )!=ESP_OK);
	}
	if(!nerrors ){
		fprintf(f,"Done.\n");
	}
	fflush (f);
	cmd_send_messaging(argv[0],nerrors>0?MESSAGING_ERROR:MESSAGING_INFO,"%s", buf);
	fclose(f);
	FREE_AND_NULL(buf);
	return (nerrors==0 && err==ESP_OK)?0:1;
}

static int do_ledvu_cmd(int argc, char **argv){
	ledvu_struct_t ledvu={  .type = "WS2812", .gpio = -1, .length = 0};
	esp_err_t err=ESP_OK;
	int nerrors = arg_parse(argc, argv,(void **)&ledvu_args);
	if (ledvu_args.clear->count) {
		cmd_send_messaging(argv[0],MESSAGING_WARNING,"ledvu config cleared\n");
		config_set_value(NVS_TYPE_STR, "led_vu_config", "");
		return 0;
	}

	char *buf = NULL;
	size_t buf_size = 0;
	FILE *f = open_memstream(&buf, &buf_size);
	if (f == NULL) {
		cmd_send_messaging(argv[0],MESSAGING_ERROR,"Unable to open memory stream.\n");
		return 1;
	}
	if(nerrors >0){
		arg_print_errors(f,ledvu_args.end,desc_ledvu);
		return 1;
	}

	nerrors+=is_output_gpio(ledvu_args.gpio, f, &ledvu.gpio, true);
	
	if(ledvu_args.length->count==0 || ledvu_args.length->ival[0]<1 || ledvu_args.length->ival[0]>255){
		fprintf(f,"error: strip length must be greater than 0 and no more than 255\n");
		nerrors++;
	}
	else {
		ledvu.length = ledvu_args.length->count>0?ledvu_args.length->ival[0]:0;
	}
	
	if(!nerrors ){
		fprintf(f,"Storing ledvu parameters.\n");
		nerrors+=(config_ledvu_set(&ledvu )!=ESP_OK);
	}
	if(!nerrors ){
		fprintf(f,"Done.\n");
	}
	fflush (f);
	cmd_send_messaging(argv[0],nerrors>0?MESSAGING_ERROR:MESSAGING_INFO,"%s", buf);
	fclose(f);
	FREE_AND_NULL(buf);
	return (nerrors==0 && err==ESP_OK)?0:1;
}

static int do_i2s_cmd(int argc, char **argv)
{
	i2s_platform_config_t i2s_dac_pin = {
		.i2c_addr = -1,
		.sda= -1,
		.scl = -1,
		.mute_gpio = -1,
		.mute_level = -1
	};
	if(is_dac_config_locked()){
		cmd_send_messaging(argv[0],MESSAGING_ERROR,"DAC Configuration is locked on this platform\n");
		return 1;
	}
	strcpy(i2s_dac_pin.model, "I2S");

	esp_err_t err=ESP_OK;
	int nerrors = arg_parse(argc, argv,(void **)&i2s_args);
	if (i2s_args.clear->count) {
		cmd_send_messaging(argv[0],MESSAGING_WARNING,"DAC config cleared\n");
		config_set_value(NVS_TYPE_STR, "dac_config", "");
		return 0;
	}

	char *buf = NULL;
	size_t buf_size = 0;
	FILE *f = open_memstream(&buf, &buf_size);
	if (f == NULL) {
		cmd_send_messaging(argv[0],MESSAGING_ERROR,"Unable to open memory stream.\n");
		return 1;
	}
	if(nerrors >0){
		arg_print_errors(f,i2s_args.end,desc_dac);
		return 1;
	}
	nerrors+=is_output_gpio(i2s_args.clock, f, &i2s_dac_pin.pin.bck_io_num, true);
	nerrors+=is_output_gpio(i2s_args.wordselect, f, &i2s_dac_pin.pin.ws_io_num, true);
	nerrors+=is_output_gpio(i2s_args.data, f, &i2s_dac_pin.pin.data_out_num, true);
	nerrors+=is_output_gpio(i2s_args.mute_gpio, f, &i2s_dac_pin.mute_gpio, false);
	if(i2s_dac_pin.mute_gpio>0){
		i2s_dac_pin.mute_level = i2s_args.mute_level->count>0?1:0;
	}
	if(i2s_args.dac_sda->count>0 && i2s_args.dac_sda->ival[0]>=0){
		// if SDA specified, then SDA and SCL are both mandatory
		nerrors+=is_output_gpio(i2s_args.dac_sda, f, &i2s_dac_pin.sda, false);
		nerrors+=is_output_gpio(i2s_args.dac_scl, f, &i2s_dac_pin.scl, false);
	}
	if(i2s_args.dac_sda->count==0&& i2s_args.dac_i2c->count>0){
		fprintf(f,"warning: ignoring i2c address, since dac i2c gpios config is incomplete\n");
	}
	else if(i2s_args.dac_i2c->count>0){
		i2s_dac_pin.i2c_addr = i2s_args.dac_i2c->ival[0];
	}
	if(i2s_args.model_name->count>0 && strlen(i2s_args.model_name->sval[0])>0){
		strncpy(i2s_dac_pin.model,i2s_args.model_name->sval[0],sizeof(i2s_dac_pin.model));
	}
	if(!nerrors ){
		fprintf(f,"Storing i2s parameters.\n");
		 nerrors+=(config_i2s_set(&i2s_dac_pin, "dac_config")!=ESP_OK);
	}
	if(!nerrors ){
		fprintf(f,"Done.\n");
	}
	fflush (f);
	cmd_send_messaging(argv[0],nerrors>0?MESSAGING_ERROR:MESSAGING_INFO,"%s", buf);
	fclose(f);
	FREE_AND_NULL(buf);

	return (nerrors==0 && err==ESP_OK)?0:1;
}

cJSON * example_cb(){
	cJSON * values = cJSON_CreateObject();
//	int i2c_port;
//	const i2c_config_t * i2c= config_i2c_get(&i2c_port);
//	if(i2c->scl_io_num>0) {
//		cJSON_AddNumberToObject(values,"scl",i2c->scl_io_num);
//	}
//	if(i2c->sda_io_num>0) {
//		cJSON_AddNumberToObject(values,"sda",i2c->sda_io_num);
//	}
//	if(i2c->master.clk_speed>0) {
//		cJSON_AddNumberToObject(values,"freq",i2c->master.clk_speed);
//	}
//	if(i2c_port>0) {
//		cJSON_AddNumberToObject(values,"port",i2c_port);
//	}
	return values;
}

//const i2s_pin_config_t * 	config_get_spdif_pin_struct( );

cJSON * i2s_cb(){
	cJSON * values = cJSON_CreateObject();
	const i2s_platform_config_t * i2s_conf= 	config_dac_get( );
	if(i2s_conf->pin.bck_io_num>0 ) {
		cJSON_AddNumberToObject(values,"clock",i2s_conf->pin.bck_io_num);
	}
	if(i2s_conf->pin.ws_io_num>=0 ) {
		cJSON_AddNumberToObject(values,"wordselect",i2s_conf->pin.ws_io_num);
	}
	if(i2s_conf->pin.data_out_num>=0 ) {
		cJSON_AddNumberToObject(values,"data",i2s_conf->pin.data_out_num);
	}
	if(i2s_conf->sda>=0 ) {
		cJSON_AddNumberToObject(values,"dac_sda",i2s_conf->sda);
	}
	if(i2s_conf->scl>=0 ) {
		cJSON_AddNumberToObject(values,"dac_scl",i2s_conf->scl);
	}	
	if(i2s_conf->i2c_addr>=0 ) {
		cJSON_AddNumberToObject(values,"dac_i2c",i2s_conf->i2c_addr);
	}		
	if(i2s_conf->mute_gpio>=0 ) {
		cJSON_AddNumberToObject(values,"mute_gpio",i2s_conf->mute_gpio);
	}		
	if(i2s_conf->mute_level>=0 ) {
		cJSON_AddBoolToObject(values,"mute_level",i2s_conf->mute_level>0);
	}		
	if(strlen(i2s_conf->model)>0){
		cJSON_AddStringToObject(values,"model_name",i2s_conf->model);
	}
	else {
		cJSON_AddStringToObject(values,"model_name","I2S");
	}
	
	return values;
}
cJSON * spdif_cb(){
	cJSON * values = cJSON_CreateObject();
	const i2s_platform_config_t * spdif_conf= 	config_spdif_get( );
	if(spdif_conf->pin.bck_io_num>0 ) {
		cJSON_AddNumberToObject(values,"clock",spdif_conf->pin.bck_io_num);
	}
	if(spdif_conf->pin.ws_io_num>=0 ) {
		cJSON_AddNumberToObject(values,"wordselect",spdif_conf->pin.ws_io_num);
	}
	if(spdif_conf->pin.data_out_num>=0 ) {
		cJSON_AddNumberToObject(values,"data",spdif_conf->pin.data_out_num);
	}
		
	return values;
}
cJSON * rotary_cb(){
	cJSON * values = cJSON_CreateObject();
	const rotary_struct_t *rotary= config_rotary_get();
	
	if(GPIO_IS_VALID_GPIO(rotary->A ) && rotary->A>=0 && GPIO_IS_VALID_GPIO(rotary->B) && rotary->B>=0){
		cJSON_AddNumberToObject(values,"A",rotary->A);
		cJSON_AddNumberToObject(values,"B",rotary->B);
		if(GPIO_IS_VALID_GPIO(rotary->SW ) && rotary->SW>=0 ){
			cJSON_AddNumberToObject(values,"SW",rotary->SW);
		}
		cJSON_AddBoolToObject(values,"volume_lock",rotary->volume_lock);
		cJSON_AddBoolToObject(values,"longpress",rotary->longpress);
		cJSON_AddBoolToObject(values,"knobonly",rotary->knobonly);
		cJSON_AddNumberToObject(values,"timer",rotary->timer);
	}
	return values;
}
cJSON * ledvu_cb(){
	cJSON * values = cJSON_CreateObject();
	const ledvu_struct_t *ledvu= config_ledvu_get();
	
	if(GPIO_IS_VALID_GPIO(ledvu->gpio) && ledvu->gpio>=0 && ledvu->length > 0){
		cJSON_AddNumberToObject(values,"gpio",ledvu->gpio);
		cJSON_AddNumberToObject(values,"length",ledvu->length);
	}
	if(strlen(ledvu->type)>0){
		cJSON_AddStringToObject(values,"type",ledvu->type);
	}
	else {
		cJSON_AddStringToObject(values,"type","WS2812");
	}
	return values;
}

cJSON * audio_cb(){
	cJSON * values = cJSON_CreateObject();
	char * 	p = config_alloc_get_default(NVS_TYPE_STR, "jack_mutes_amp", "n", 0);
    cJSON_AddStringToObject(values,"jack_behavior",(strcmp(p,"1") == 0 ||strcasecmp(p,"y") == 0)?"Headphones":"Subwoofer");
    FREE_AND_NULL(p);    
	return values;
}
cJSON * bt_source_cb(){
	cJSON * values = cJSON_CreateObject();
	char * 	p = config_alloc_get_default(NVS_TYPE_STR, "a2dp_sink_name", NULL, 0);
	if(p){
		cJSON_AddStringToObject(values,"sink_name",p);
	}
	FREE_AND_NULL(p);    
	// p = config_alloc_get_default(NVS_TYPE_STR, "a2dp_ctmt", NULL, 0);
	// if(p){
	// 	cJSON_AddNumberToObject(values,"connect_timeout_delay",((double)atoi(p)/1000.0));
	// }
	// FREE_AND_NULL(p);   
	p = config_alloc_get_default(NVS_TYPE_STR, "a2dp_spin", "0000", 0);
	if(p){
		cJSON_AddStringToObject(values,"pin_code",p);
	}
	FREE_AND_NULL(p);   
	// p = config_alloc_get_default(NVS_TYPE_STR, "a2dp_ctrld", NULL, 0);
	// if(p){
	// 	cJSON_AddNumberToObject(values,"control_delay",((double)atoi(p)/1000.0));
	// }
	// FREE_AND_NULL(p);   
	return values;
}


void get_str_parm_json(struct arg_str * parm, cJSON * entry){
	const char * name = parm->hdr.longopts?parm->hdr.longopts:parm->hdr.glossary;
	if(parm->count>0){
		cJSON_AddStringToObject(entry,name,parm->sval[0]);
	}
}
void get_file_parm_json(struct arg_file * parm, cJSON * entry){
	const char * name = parm->hdr.longopts?parm->hdr.longopts:parm->hdr.glossary;
	if(parm->count>0){
		cJSON_AddStringToObject(entry,name,parm->filename[0]);
	}
}
void get_lit_parm_json(struct arg_lit * parm, cJSON * entry){
	const char * name = parm->hdr.longopts?parm->hdr.longopts:parm->hdr.glossary;
	cJSON_AddBoolToObject(entry,name,(parm->count>0));
}
void get_int_parm_json(struct arg_int * parm, cJSON * entry){
	const char * name = parm->hdr.longopts?parm->hdr.longopts:parm->hdr.glossary;
	if(parm->count>0){
		cJSON_AddNumberToObject(entry,name,parm->ival[0]);
	}
}

static int do_squeezelite_cmd(int argc, char **argv)
{
	esp_err_t err=ESP_OK;
	int nerrors = arg_parse_msg(argc, argv,(struct arg_hdr ** )&squeezelite_args);
    char *buf = NULL;
	size_t buf_size = 0;
	FILE *f = open_memstream(&buf, &buf_size);
	if (f == NULL) {
		cmd_send_messaging(argv[0],MESSAGING_ERROR,"Unable to open memory stream.\n");
		return 1;
	}
	fprintf(f,"Not yet implemented!");
	nerrors+=1;
	fflush (f);
	cmd_send_messaging(argv[0],nerrors>0?MESSAGING_ERROR:MESSAGING_INFO,"%s", buf);
	fclose(f);
	FREE_AND_NULL(buf);
	return (nerrors==0 && err==ESP_OK)?0:1;
}

cJSON * squeezelite_cb(){
	cJSON * values = cJSON_CreateObject();
	char * nvs_config= config_alloc_get(NVS_TYPE_STR, "autoexec1");
	char **argv = NULL;
	char *buf = NULL;
	size_t buf_size = 0;
    int nerrors=1;
	FILE *f = open_memstream(&buf, &buf_size);
	if (f == NULL) {
		log_send_messaging(MESSAGING_ERROR,"Unable to parse squeezelite parameters");
	}
	else {

		if(nvs_config && strlen(nvs_config)>0){
			ESP_LOGD(TAG,"Parsing command %s",nvs_config);
			argv = (char **) calloc(22, sizeof(char *));
			if (argv == NULL) {
				FREE_AND_NULL(nvs_config);
				return values;
			}
			size_t argc = esp_console_split_argv(nvs_config, argv,22);
			if (argc != 0) {
				nerrors = arg_parse(argc, argv,(void **)&squeezelite_args);
				ESP_LOGD(TAG,"Parsing completed");
			}
		}
		if (nerrors == 0) {
			get_str_parm_json(squeezelite_args.buffers, values);
			get_str_parm_json(squeezelite_args.codecs, values);
			get_lit_parm_json(squeezelite_args.header_format, values);
			get_str_parm_json(squeezelite_args.log_level, values);
			
			// get_str_parm_json(squeezelite_args.log_level_all, values);
			// get_str_parm_json(squeezelite_args.log_level_decode, values);
			// get_str_parm_json(squeezelite_args.log_level_output, values);
			// get_str_parm_json(squeezelite_args.log_level_slimproto, values);
			// get_str_parm_json(squeezelite_args.log_level_stream, values);
			get_str_parm_json(squeezelite_args.mac_addr, values);
			get_str_parm_json(squeezelite_args.output_device, values);
			get_str_parm_json(squeezelite_args.model_name, values);
			get_str_parm_json(squeezelite_args.name, values);
			get_int_parm_json(squeezelite_args.rate, values);
			get_str_parm_json(squeezelite_args.rates, values);
			get_str_parm_json(squeezelite_args.server, values);
			get_int_parm_json(squeezelite_args.timeout, values);
			char * p = cJSON_Print(values);
			ESP_LOGD(TAG,"%s",p);
			free(p);
		}
		else {
			arg_print_errors(f, squeezelite_args.end, desc_squeezelite);
		}
		fflush (f);
		if(strlen(buf)>0){
			log_send_messaging(nerrors?MESSAGING_ERROR:MESSAGING_INFO,"%s", buf);
		}
		fclose(f);
		FREE_AND_NULL(buf);
	}
	FREE_AND_NULL(nvs_config);
	FREE_AND_NULL(argv);
	return values;
}
static char * get_log_level_options(const char * longname){
	const char * template = "<%s=info|%s=debug|%s=sdebug>";
	char * options = NULL;
	int len = snprintf(NULL,0,template,longname,longname,longname);
	if(len>0){
		options = malloc(len+1);
		snprintf(options,len,template,longname,longname,longname);
	}
	return options;
}
static void register_i2s_config(void){
	i2s_args.model_name = arg_str1(NULL,"model_name","TAS57xx|TAS5713|AC101|WM8978|I2S","DAC Model Name");
	i2s_args.clear = arg_lit0(NULL, "clear", "Clear configuration");
    i2s_args.clock = arg_int1(NULL,"clock","<n>","Clock GPIO. e.g. 33");
    i2s_args.wordselect = arg_int1(NULL,"wordselect","<n>","Word Select GPIO. e.g. 25");
    i2s_args.data = arg_int1(NULL,"data","<n>","Data GPIO. e.g. 32");
    i2s_args.mute_gpio = arg_int0(NULL,"mute_gpio", "<n>", "Mute GPIO. e.g. 14");
	i2s_args.mute_level = arg_lit0(NULL,"mute_level","Mute GPIO level. Checked=HIGH, Unchecked=LOW");
    i2s_args.dac_sda = arg_int0(NULL,"dac_sda", "<n>", "SDA GPIO. e.g. 27");
    i2s_args.dac_scl = arg_int0(NULL,"dac_scl", "<n>", "SCL GPIO. e.g. 26");
    i2s_args.dac_i2c = arg_int0(NULL,"dac_i2c", "<n>", "I2C device address. e.g. 106");
    i2s_args.end = arg_end(6);

	 const esp_console_cmd_t cmd = {
        .command = CFG_TYPE_HW("dac"),
        .help = desc_dac,
        .hint = NULL,
        .func = &do_i2s_cmd,
        .argtable = &i2s_args
    };
    cmd_to_json_with_cb(&cmd,&i2s_cb);
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

static void register_bt_source_config(void){
	
	bt_source_args.sink_name= arg_str1("n","sink_name", "name","Bluetooth audio device name. This applies when output mode is Bluetooth");
	bt_source_args.pin_code= arg_str1("p","pin_code", "pin","Bluetooth security/pin code. Usually 0000. This applies when output mode is Bluetooth");
//	bt_source_args.control_delay= arg_dbl0("d","control_delay","seconds","Control response delay, in seconds. This determines the response time of the system Bluetooth events. The default value should work for the majority of cases and changing this could lead to instabilities.");
//	bt_source_args.connect_timeout_delay= arg_dbl0("t","connect_timeout_delay","seconds","Connection timeout. Determines the maximum amount of time, in seconds, that the system will wait when connecting to a bluetooth device. Beyond this delay, a new connect attempt will be made.");
	bt_source_args.end= arg_end(1);
	const esp_console_cmd_t cmd = {
        .command = CFG_TYPE_AUDIO("bt_source"),
        .help = desc_bt_source,
        .hint = NULL,
        .func = &do_bt_source_cmd,
        .argtable = &bt_source_args
    };
    cmd_to_json_with_cb(&cmd,&bt_source_cb);
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

static void register_rotary_config(void){
	rotary_args.rem = arg_rem("remark","One rotary encoder is supported, quadrature shift with press. Such encoders usually have 2 pins for encoders (A and B), and common C that must be set to ground and an optional SW pin for press. A, B and SW must be pulled up, so automatic pull-up is provided by ESP32, but you can add your own resistors. A bit of filtering on A and B (~470nF) helps for debouncing which is not made by software.\r\nEncoder is normally hard-coded to respectively knob left, right and push on LMS and to volume down/up/play toggle on BT and AirPlay.");
	rotary_args.A = arg_int1(NULL,"A","gpio","A/DT gpio");
	rotary_args.B = arg_int1(NULL,"B","gpio","B/CLK gpio");
	rotary_args.SW = arg_int0(NULL,"SW","gpio","Switch gpio");
	rotary_args.knobonly = arg_lit0(NULL,"knobonly","Single knob full navigation. Left, Right and Press is navigation, with Press always going to lower submenu item. Longpress is 'Play', Double press is 'Back', a quick left-right movement on the encoder is 'Pause'");
	rotary_args.timer = arg_int0(NULL,"timer","ms","The speed of double click (or left-right) when knob only option is enabled. Be aware that the longer you set double click speed, the less responsive the interface will be. ");
	rotary_args.volume_lock = arg_lit0(NULL,"volume_lock", "Force Volume down/up/play toggle all the time (even in LMS). ");
	rotary_args.longpress = arg_lit0(NULL,"longpress","Enable alternate mode mode on long-press. In that mode, left is previous, right is next and press is toggle. Every long press on SW alternates between modes (the main mode actual behavior depends on 'volume').");
	rotary_args.clear = arg_lit0(NULL, "clear", "Clear configuration");
	rotary_args.end = arg_end(3);
	const esp_console_cmd_t cmd = {
        .command = CFG_TYPE_HW("rotary"),
        .help = desc_rotary,
        .hint = NULL,
        .func = &do_rotary_cmd,
        .argtable = &rotary_args
    };
    cmd_to_json_with_cb(&cmd,&rotary_cb);
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

static void register_ledvu_config(void){
	ledvu_args.type = arg_str1(NULL,"type","<none>|WS2812","Led type (supports one rgb strip to display built in effects and allow remote control through 'dmx' messaging)");
	ledvu_args.length = arg_int1(NULL,"length","<1..255>","Strip length (1-255 supported)");
	ledvu_args.gpio = arg_int1(NULL,"gpio","gpio","Data pin");
	ledvu_args.clear = arg_lit0(NULL, "clear", "Clear configuration");
	ledvu_args.end = arg_end(4);

	const esp_console_cmd_t cmd = {
        .command = CFG_TYPE_HW("ledvu"),
        .help = desc_ledvu,
        .hint = NULL,
        .func = &do_ledvu_cmd,
        .argtable = &ledvu_args
    };
	cmd_to_json_with_cb(&cmd,&ledvu_cb);
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

static void register_audio_config(void){
	audio_args.jack_behavior = arg_str0("j", "jack_behavior","Headphones|Subwoofer","On supported DAC, determines the audio jack behavior. Selecting headphones will cause the external amp to be muted on insert, while selecting Subwoofer will keep the amp active all the time.");
    audio_args.end = arg_end(6);
	const esp_console_cmd_t cmd = {
        .command = CFG_TYPE_AUDIO("general"),
        .help = desc_audio,
        .hint = NULL,
        .func = &do_audio_cmd,
        .argtable = &audio_args
    };
    cmd_to_json_with_cb(&cmd,&audio_cb);
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

static void register_spdif_config(void){
	spdif_args.clear = arg_lit0(NULL, "clear", "Clear configuration");
    spdif_args.clock = arg_int1(NULL,"clock","<n>","Clock GPIO. e.g. 33");
    spdif_args.wordselect = arg_int1(NULL,"wordselect","<n>","Word Select GPIO. e.g. 25");
    spdif_args.data = arg_int1(NULL,"data","<n>","Data GPIO. e.g. 32");
    spdif_args.end = arg_end(6);

	 const esp_console_cmd_t cmd = {
        .command = CFG_TYPE_HW("spdif"),
        .help = desc_spdif,
        .hint = NULL,
        .func = &do_spdif_cmd,
        .argtable = &spdif_args
    };
    cmd_to_json_with_cb(&cmd,&spdif_cb);
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
static void register_squeezelite_config(void){
	squeezelite_args.server = arg_str0("s","server","<server>[:<port>]","Connect to specified server, otherwise uses autodiscovery to find server");
	squeezelite_args.buffers = arg_str0("b","buffers","<stream>:<output>","Internal Stream and Output buffer sizes in Kbytes");
	squeezelite_args.codecs = arg_strn("c","codecs","+" CODECS "+",0,20,"Restrict codecs to those specified, otherwise load all available codecs; known codecs: " CODECS );
	squeezelite_args.timeout = arg_int0("C","timeout","<n>","Close output device when idle after timeout seconds, default is to keep it open while player is 'on");
	squeezelite_args.log_level = arg_str0("d","loglevel","log=level","Set logging level, logs: all|slimproto|stream|decode|output|ir, level: info|debug|sdebug"); // "  -d <log>=<level>\tSet logging level, logs: all|slimproto|stream|decode|output|ir, level: info|debug|sdebug\n"
//	squeezelite_args.log_level_all = arg_str0(NULL,"all",get_log_level_options("all"),"Overall Logging Level");
//	squeezelite_args.log_level_slimproto = arg_str0(NULL,"loglevel_slimproto",get_log_level_options("slimproto"),"Slimproto Logging Level");
//	squeezelite_args.log_level_stream= arg_str0(NULL,"loglevel_stream",get_log_level_options("stream"),"Stream Logging Level");
//	squeezelite_args.log_level_decode= arg_str0(NULL,"loglevel_decode",get_log_level_options("decode"),"Decode Logging Level");
//	squeezelite_args.log_level_output= arg_str0(NULL,"loglevel_output",get_log_level_options("output"),"Output Logging Level");
	#if IR
	squeezelite_args.log_level_ir= arg_str0(NULL,"loglevel_ir",get_log_level_options("ir"),"IR Logging Level");
	#endif

	squeezelite_args.output_device = arg_str0("o","output_device","<string>","Output device (BT, I2S or SPDIF)");
	squeezelite_args.mac_addr = arg_str0("m","mac_addr","<string>","Mac address, format: ab:cd:ef:12:34:56.");
	squeezelite_args.model_name = arg_str0("M", "modelname", "<string>","Set the squeezelite player model name sent to the server (default: " MODEL_NAME_STRING ")");
	squeezelite_args.name = arg_str0("n","name","<string>","Player name, if different from the current host name. Name can alternatively be assigned from the system/device name configuration.");
	squeezelite_args.header_format = arg_lit0("W","header_format","Read wave and aiff format from header, ignore server parameters");
	squeezelite_args.rates = arg_str0("r","rates","<rates>[:<delay>]", "Sample rates supported, allows output to be off when squeezelite is started; rates = <maxrate>|<minrate>-<maxrate>|<rate1>,<rate2>,<rate3>; delay = optional delay switching rates in ms\n");
	#if RESAMPLE
	squeezelite_args.resample = arg_lit0("R","resample","Activate Resample");
	squeezelite_args.resample_parms = arg_str0("u","resample_parms","<recipe>:<flags>:<attenuation>:<precision>:<passband_end>:<stopband_start>:<phase_response>","Resample, params");
	#endif
	#if RESAMPLE16
	squeezelite_args.resample = arg_lit0("R","resample","Activate Resample");
	squeezelite_args.resample_parms = arg_str0("u","resample_parms","(b|l|m)[:i]","Resample, params. b = basic linear interpolation, l = 13 taps, m = 21 taps, i = interpolate filter coefficients");
	#endif
	squeezelite_args.rate = arg_int0("Z","max_rate", "<n>", "Report rate to server in helo as the maximum sample rate we can support");
	squeezelite_args.end = arg_end(6);
    const esp_console_cmd_t cmd = {
        .command = CFG_TYPE_AUDIO("squeezelite"),
        .help = desc_squeezelite,
        .hint = NULL,
        .func = &do_squeezelite_cmd,
        .argtable = &squeezelite_args
    };
    cmd_to_json_with_cb(&cmd,&squeezelite_cb);
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

void register_config_cmd(void){
	register_audio_config();
//	register_squeezelite_config();
	register_bt_source_config();
	if(!is_dac_config_locked()){
		register_i2s_config();
	}
	if(!is_spdif_config_locked()){
		register_spdif_config();
	}
	register_rotary_config();
	register_ledvu_config();
}

