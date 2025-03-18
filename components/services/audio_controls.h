/* 
 *  (c) Philippe G. 2019, philippe_44@outlook.com
 *
 *  This software is released under the MIT License.
 *  https://opensource.org/licenses/MIT
 *
 */
 
#pragma once

#include "buttons.h"
#include "Buttons.pb.h"

// BEWARE: this is the index of the array of action below (change actrls_action_s as well!)
// typedef enum { 	ACTRLS_NONE = -1, ACTRLS_POWER, ACTRLS_VOLUP, ACTRLS_VOLDOWN, ACTRLS_TOGGLE, ACTRLS_PLAY, 
// 				ACTRLS_PAUSE, ACTRLS_STOP, ACTRLS_REW, ACTRLS_FWD, ACTRLS_PREV, ACTRLS_NEXT, 
// 				BCTRLS_UP, BCTRLS_DOWN, BCTRLS_LEFT, BCTRLS_RIGHT, 
// 				BCTRLS_PS0,BCTRLS_PS1,BCTRLS_PS2,BCTRLS_PS3,BCTRLS_PS4,BCTRLS_PS5,BCTRLS_PS6,BCTRLS_PS7,BCTRLS_PS8,BCTRLS_PS9,
// 				KNOB_LEFT, KNOB_RIGHT, KNOB_PUSH,
//                 ACTRLS_SLEEP,
// 				ACTRLS_REMAP, ACTRLS_MAX 
// 		} actrls_action_e;

typedef void (*actrls_handler)(bool pressed);
typedef actrls_handler actrls_t[sys_btns_actions_MAX- sys_btns_actions_A_NONE - 1];
typedef bool actrls_hook_t(int gpio, sys_btns_action *action, button_event_e event, button_press_e press, bool long_press);
typedef bool actrls_ir_handler_t(uint16_t addr, uint16_t cmd);

// BEWARE any change to struct below must be mapped to actrls_config_map
// typedef struct {
// 	actrls_action_e action;
// 	const char * name;
// } actrls_action_detail_t;
typedef struct actrl_config_s {
	int gpio;
	int type;
	bool pull;
	int	debounce;
	int long_press;
	int shifter_gpio;
	sys_btns_press normal, longpress, shifted, longshifted;	// [0] keypressed, [1] keyreleased
} actrls_config_t;

esp_err_t actrls_init(const char *profile_name);

/* 
Set hook function to non-null to be set your own direct managemet function, 
which should return true if it managed the control request, false if the
normal handling should be done
The add_release boolean forces a release event to be sent if a press action has been 
set, whether a release action has been set or not
*/
void actrls_set_default(const actrls_t controls, bool raw_controls, actrls_hook_t *hook, actrls_ir_handler_t *ir_handler);
void actrls_set(const actrls_t controls, bool raw_controls, actrls_hook_t *hook, actrls_ir_handler_t *ir_handler);
void actrls_unset(void);
bool actrls_ir_action(uint16_t addr, uint16_t code);
