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
------------------------------------------------------------------------------
 Filename: pv_audio_type_defs.h

------------------------------------------------------------------------------
 INCLUDE DESCRIPTION

 This file was derived from a number of standards bodies. The type
 definitions below were created from some of the best practices observed
 in the standards bodies.

 This file is dependent on limits.h for defining the bit widths. In an
 ANSI C environment limits.h is expected to always be present and contain
 the following definitions:

     SCHAR_MIN
     SCHAR_MAX
     UCHAR_MAX

     INT_MAX
     INT_MIN
     UINT_MAX

     SHRT_MIN
     SHRT_MAX
     USHRT_MAX

     LONG_MIN
     LONG_MAX
     ULONG_MAX

------------------------------------------------------------------------------
*/

#ifndef PV_AUDIO_TYPE_DEFS_H
#define PV_AUDIO_TYPE_DEFS_H

#include    <inttypes.h>

#ifndef Char
typedef int8_t      Char;
#endif

#ifndef Int8
typedef int8_t      Int8;
#endif

#ifndef UChar
typedef uint8_t     UChar;
#endif

#ifndef UInt8
typedef uint8_t      UInt8;
#endif



/*----------------------------------------------------------------------------
; Define generic signed and unsigned int
----------------------------------------------------------------------------*/
#ifndef Int
typedef signed int  Int;
#endif

#ifndef UInt
typedef unsigned int    UInt;
#endif


/*----------------------------------------------------------------------------
; Define 16 bit signed and unsigned words
----------------------------------------------------------------------------*/


#ifndef Int16
typedef int16_t     Int16;
#endif

#ifndef INT16_MIN
#define INT16_MIN   (-32768)
#endif

#ifndef INT16_MAX
#define INT16_MAX   32767
#endif

#ifndef UInt16
typedef uint16_t    UInt16;

#endif


/*----------------------------------------------------------------------------
; Define 32 bit signed and unsigned words
----------------------------------------------------------------------------*/


#ifndef Int32
typedef int32_t     Int32;
#endif

#ifndef INT32_MIN
#define INT32_MIN   (-2147483647 - 1)
#endif
#ifndef INT32_MAX
#define INT32_MAX   2147483647
#endif

#ifndef UInt32
typedef uint32_t    UInt32;
#endif

#ifndef UINT32_MIN
#define UINT32_MIN  0
#endif
#ifndef UINT32_MAX
#define UINT32_MAX  0xffffffff
#endif


#ifndef INT_MAX
#define INT_MAX  INT32_MAX      /*  for 32 bit  */
#endif

/*----------------------------------------------------------------------------
; Define 64 bit signed and unsigned words
----------------------------------------------------------------------------*/
#ifndef Int64
typedef int64_t     Int64;
#endif

#ifndef UInt64
typedef uint64_t    UInt64;
#endif

/*----------------------------------------------------------------------------
; Define boolean type
----------------------------------------------------------------------------*/
#ifndef Bool
typedef Int     Bool;
#endif
#ifndef FALSE
#define FALSE       0
#endif

#ifndef TRUE
#define TRUE        1
#endif

#ifndef OFF
#define OFF     0
#endif
#ifndef ON
#define ON      1
#endif

#ifndef NO
#define NO      0
#endif
#ifndef YES
#define YES     1
#endif

#ifndef SUCCESS
#define SUCCESS     0
#endif

#ifndef  NULL
#define  NULL       0
#endif

#ifndef  OSCL_IMPORT_REF
#define  OSCL_IMPORT_REF
#endif

#ifndef  OSCL_EXPORT_REF
#define  OSCL_EXPORT_REF
#endif

#ifndef  OSCL_IMPORT_REF
#define  OSCL_IMPORT_REF
#endif

#ifndef  OSCL_UNUSED_ARG
#define  OSCL_UNUSED_ARG(x) (void)(x)
#endif

#endif  /* PV_AUDIO_TYPE_DEFS_H */
