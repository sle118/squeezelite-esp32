/* test_mean.c: Implementation of a testable component.

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <limits.h>
#include "unity.h"
#include "platform_console.h"
#include "platform_esp32.h"
// #include "Configurator.h"
#pragma message("fixme: search for TODO below")
#include "string.h"
struct arg_lit *arglit;
struct arg_int *argint;
struct arg_str *argstr;
struct arg_end *end;

extern int is_output_gpio(struct arg_int * gpio, FILE * f, int * gpio_out, bool mandatory);
extern void initialize_console();
extern esp_err_t run_command(char * line);
static char *buf = NULL;
static char * s_tmp_line_buf=NULL;
static size_t buf_size = 0;
static FILE * f;
static size_t argc=1;
static char ** argv=NULL;
static bool config_initialized=false;
void init_console(){
    if(config_initialized) return;
    initialize_console();
    config_initialized=true;
}

/****************************************************************************************
 * 
 */
void open_mem_stream_file(){
	f = open_memstream(&buf, &buf_size);
}

/****************************************************************************************
 * 
 */
void close_flush_all(void * argtable, int count,bool print){
    fflush (f);
    if(print){
        printf("%s", buf);
    }
    fclose(f);
    free(buf);
    arg_freetable(argtable,count);
    free(argv);   
}

/****************************************************************************************
 * 
 */
int alloc_split_command_line(char * cmdline){
    argv = (char **) calloc(22, sizeof(char *));
    if(!s_tmp_line_buf){
        s_tmp_line_buf= calloc(strlen(cmdline), 1);
    }
    strlcpy(s_tmp_line_buf, cmdline, 22);
    argc = esp_console_split_argv(s_tmp_line_buf, argv,22);

    return 0;
}

/****************************************************************************************
 * 
 */
int alloc_split_parse_command_line(char * cmdline, void ** args){
    alloc_split_command_line(cmdline);
    return arg_parse(argc, argv,args);
}

/****************************************************************************************
 * 
 */
TEST_CASE("Invalid GPIO detected", "[config][ui]")
{
    char * cmdline =  "test -i 55\n";
    void *argtable[] = {
        argint = arg_int1("i","int","<gpio>","GPIO number"),
        end  = arg_end(6)
    };
    open_mem_stream_file();
    alloc_split_parse_command_line(cmdline, &argtable);
    int out_val = 0;
    TEST_ASSERT_EQUAL_INT_MESSAGE(1,is_output_gpio(argtable[0], f, &out_val, true),"Invalid GPIO not detected");
    TEST_ASSERT_EQUAL_INT_MESSAGE(-1,out_val,"GPIO Should be set to -1");
    fflush (f);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Invalid int gpio: [55] is not a GPIO\n",buf,"Invalid GPIO message wrong");
    close_flush_all(argtable,sizeof(argtable)/sizeof(argtable[0]),false);
}

/****************************************************************************************
 * 
 */
TEST_CASE("Input Only GPIO detected", "[config][ui]")
{
    char * cmdline =  "test -i 35\n";
    void *argtable[] = {
        argint = arg_int1("i","int","<gpio>","GPIO number"),
        end  = arg_end(6)
    };
    open_mem_stream_file();
    alloc_split_parse_command_line(cmdline, &argtable);
    int out_val = 0;
    TEST_ASSERT_EQUAL_INT_MESSAGE(1,is_output_gpio(argtable[0], f, &out_val, true),"Input only GPIO not detected");
    TEST_ASSERT_EQUAL_INT_MESSAGE(-1,out_val,"GPIO Should be set to -1");
    fflush (f);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Invalid int gpio: [35] has input capabilities only\n",buf,"Missing GPIO message wrong");
    close_flush_all(argtable,sizeof(argtable)/sizeof(argtable[0]),false);
}

/****************************************************************************************
 * 
 */
TEST_CASE("Valid GPIO Processed", "[config][ui]")
{
    char * cmdline =  "test -i 33\n";
    void *argtable[] = {
        argint = arg_int1("i","int","<gpio>","GPIO number"),
        end  = arg_end(6)
    };
    open_mem_stream_file();
    alloc_split_parse_command_line(cmdline, &argtable);
    int out_val = 0;
    TEST_ASSERT_EQUAL_INT_MESSAGE(0,is_output_gpio(argtable[0], f, &out_val, true),"Valid GPIO not recognized");
    TEST_ASSERT_EQUAL_INT_MESSAGE(33,out_val,"GPIO Should be set to 33");
    fflush (f);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("",buf,"Valid GPIO shouldn't produce a message");
    close_flush_all(argtable,sizeof(argtable)/sizeof(argtable[0]),false);
}

/****************************************************************************************
 * 
 */
TEST_CASE("Missing mandatory GPIO detected", "[config][ui]")
{
    char * cmdline =  "test \n";
    void *argtable[] = {
        argint = arg_int1("i","int","<gpio>","GPIO number"),
        end  = arg_end(6)
    };
    open_mem_stream_file();
    alloc_split_parse_command_line(cmdline, &argtable);
    int out_val = 0;
    TEST_ASSERT_EQUAL_INT_MESSAGE(1,is_output_gpio(argtable[0], f, &out_val, true),"Missing GPIO not detected");
    fflush (f);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Missing: int\n",buf,"Missing GPIO parameter message wrong");
    TEST_ASSERT_EQUAL_INT_MESSAGE(-1,out_val,"GPIO Should be set to -1");
    close_flush_all(argtable,sizeof(argtable)/sizeof(argtable[0]),false);
}
/****************************************************************************************
 * 
 */
TEST_CASE("Missing mandatory parameter detected", "[config][ui]")
{
    char * cmdline =  "test \n";
    void *argtable[] = {
        argint = arg_int1("i","int","<gpio>","GPIO number"),
        end  = arg_end(6)
    };
    open_mem_stream_file();
    alloc_split_parse_command_line(cmdline, &argtable);
    int out_val = 0;
    TEST_ASSERT_EQUAL_INT_MESSAGE(1,is_output_gpio(argtable[0], f, &out_val, true),"Missing parameter not detected");
    fflush (f);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Missing: int\n",buf,"Missing parameter message wrong");
    TEST_ASSERT_EQUAL_INT_MESSAGE(-1,out_val,"GPIO Should be set to -1");
    close_flush_all(argtable,sizeof(argtable)/sizeof(argtable[0]),false);
}
/****************************************************************************************
 * 
 */
TEST_CASE("dac config command", "[config_cmd]")
{
    config_set_value(NVS_TYPE_STR, "dac_config", "");
    esp_err_t err=run_command("cfg-hw-dac\n");
    char * nvs_value =  config_alloc_get_str("dac_config", NULL,NULL);
    TEST_ASSERT_NOT_NULL(nvs_value);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK,err,"Running command failed");
    free(nvs_value);
}
