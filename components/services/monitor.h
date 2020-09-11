/* 
 *  Squeezelite for esp32
 *
 *  (c) Philippe G. 2019, philippe_44@outlook.com
 *
 *  This software is released under the MIT License.
 *  https://opensource.org/licenses/MIT
 *
 */
 
#pragma once

extern void (*jack_handler_svc)(bool inserted);
extern bool jack_inserted_svc(void);

extern void (*spkfault_handler_svc)(bool inserted);
extern bool spkfault_svc(void);

extern float battery_value_svc(void);
extern uint8_t battery_level_svc(void);

