/*
 *  Squeezelite for esp32
 *
 *  PWM master volume backend for external analog control paths.
 *
 */

#pragma once

void pwm_mastervol_init(void);
void pwm_mastervol_set(unsigned left, unsigned right);
