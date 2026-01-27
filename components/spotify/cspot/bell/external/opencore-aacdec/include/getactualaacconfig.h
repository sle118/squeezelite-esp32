/* ------------------------------------------------------------------
 * Copyright (C) 1998-2009 PacketVideo
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 * -------------------------------------------------------------------
 */
/*

 Filename: getactualaacconfig.h

------------------------------------------------------------------------------
 INCLUDE DESCRIPTION

 getaacaudioinfo definition function

------------------------------------------------------------------------------
*/

/*----------------------------------------------------------------------------
; CONTINUE ONLY IF NOT ALREADY DEFINED
----------------------------------------------------------------------------*/

#ifndef GETACTUALAACCONFIG_H
#define GETACTUALAACCONFIG_H

#include "pv_audio_type_defs.h"

OSCL_IMPORT_REF Int32 GetActualAacConfig(UInt8 *aConfigHeader,
        UInt8 *aAudioObjectType,
        Int32 *aConfigHeaderSize,
        UInt8 *SamplingRateIndex,
        UInt32 *NumChannels);

#endif
