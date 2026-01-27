/* 
 *  Squeezelite for esp32
 *
 *  (c) Sebastien 2019
 *      Philippe G. 2019, philippe_44@outlook.com
 *
 *  This software is released under the MIT License.
 *  https://opensource.org/licenses/MIT
 *
 */

#include "driver/i2s.h"
#include "freertos/FreeRTOS.h"

typedef enum { ADAC_ON = 0, ADAC_STANDBY, ADAC_OFF } adac_power_e;

struct adac_s {
  bool (*init)(int i2c_port_num, int i2s_num, i2s_config_t* config);
  void (*deinit)(void);
  void (*power)(adac_power_e mode);
  void (*speaker)(bool active);
  void (*headset)(bool active);
  void (*volume)(unsigned left, unsigned right);
};

extern struct adac_s dac_tas57xx;
extern struct adac_s dac_a1s;
extern struct adac_s dac_external;