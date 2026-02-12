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

#pragma once

#include "stddef.h"
#include "stdbool.h"

struct target_s {
    char* model;
    bool (*init)(void);
};

extern const struct target_s target_muse;
