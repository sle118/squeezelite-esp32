/* 
 *  (c) Philippe G. 2019, philippe_44@outlook.com
 *
 *  This software is released under the MIT License.
 *  https://opensource.org/licenses/MIT
 *
 */

#pragma once

#include "gds.h"


/* 
 The displayer is not thread-safe and the caller must ensure use its own 
 mutexes if it wants something better. Especially, text() line() and draw()
 are not protected against each other.
 In text mode (text/line) when using DISPLAY_SUSPEND, the displayer will 
 refreshed line 2 one last time before suspending itself. As a result if it
 is in a long sleep (scrolling pause), the refresh will happen after wakeup. 
 So it can conflict with other display direct writes that have been made during
 sleep. Note that if DISPLAY_SHUTDOWN has been called meanwhile, it (almost) 
 never happens
 The display_bus() shall be subscribed by other displayers so that at least
 when this one (the main) wants to take control over display, it can signal
 that to others
*/ 
extern struct GDS_Device *display;
  
enum displayer_cmd_e 	{ DISPLAYER_SHUTDOWN, DISPLAYER_ACTIVATE, DISPLAYER_SUSPEND, DISPLAYER_TIMER_PAUSE, DISPLAYER_TIMER_RUN };
enum displayer_time_e 	{ DISPLAYER_ELAPSED, DISPLAYER_REMAINING };

enum display_bus_cmd_e { DISPLAY_BUS_TAKE, DISPLAY_BUS_GIVE };
extern bool (*display_bus)(void *from, enum display_bus_cmd_e cmd);

void displayer_scroll(char *string, int speed, int pause);
void displayer_control(enum displayer_cmd_e cmd, ...);
void displayer_metadata(char *artist, char *album, char *title);
void displayer_artwork(uint8_t *data);
void displayer_timer(enum displayer_time_e mode, int elapsed, int duration);
bool displayer_can_artwork(void);
