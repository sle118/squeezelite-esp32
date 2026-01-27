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

 Filename: getactualaacconfig.cpp

------------------------------------------------------------------------------
 INPUT AND OUTPUT DEFINITIONS

 Inputs:
    pExt = pointer to the external interface structure. See the file
           PVMP4AudioDecoder_API.h for a description of each field.
           Data type of pointer to a tPVMP4AudioDecoderExternal
           structure.

           pExt->pInputBuffer: pointer to input buffer containing input
                               bitstream

           pExt->inputBufferCurrentLength: number of bytes in the input buffer

           pExt->inputBufferUsedLength: number of bytes already consumed in
                                        input buffer

           pExt->remainderBits: number of bits consumed in addition to
                                pExt->inputBufferUsedLength

    pMem = void pointer to hide the internal implementation of the library
           It is cast back to a tDec_Int_File structure. This structure
           contains information that needs to persist between calls to
           this function, or is too big to be placed on the stack, even
           though the data is only needed during execution of this function
           Data type void pointer, internally pointer to a tDec_Int_File
           structure.

    aStream = pointer to a UChar* stream that holds the input stream.

    aProperty = pointer to a structure DecodeProperties (define in include file)

 Local Stores/Buffers/Pointers Needed: None
           (The memory set aside in pMem performs this task)

 Global Stores/Buffers/Pointers Needed: None

 Outputs:
     status = 0                       if no error occurred
              MP4AUDEC_NONRECOVERABLE if a non-recoverable error occurred
              MP4AUDEC_RECOVERABLE    if a recoverable error occurred.
              Presently a recoverable error does not exist, but this
              was a requirement.


 Pointers and Buffers Modified:
    pMem contents are modified.
    pExt: (more detail in the file PVMP4AudioDecoder_API.h)
    inputBufferUsedLength - number of array elements used up by the stream.
    remainderBits - remaining bits in the next UInt32 buffer
    samplingRate - sampling rate in samples per sec
    encodedChannels - channels found on the file (informative)
    frameLength - length of the frame

 Local Stores Modified: None.

 Global Stores Modified: None.

------------------------------------------------------------------------------
 FUNCTION DESCRIPTION


------------------------------------------------------------------------------
------------------------------------------------------------------------------
*/


/*----------------------------------------------------------------------------
; INCLUDES
----------------------------------------------------------------------------*/

#include "getactualaacconfig.h"
#include "pv_audio_type_defs.h"
#include "s_tdec_int_file.h"
#include "ibstream.h"           /* where #define INBUF_ARRAY_INDEX_SHIFT */
#include "sfb.h"                   /* Where samp_rate_info[] is declared */

#include "get_audio_specific_config.h"
#include "pvmp4audiodecoder_api.h"   /* Where this function is declared */

#include "oscl_mem.h"
#include "e_elementid.h"
#include "e_sbr_element_id.h"
#include "get_dse.h"
#include "get_sbr_bitstream.h"
#include "get_prog_config.h"
#include "huffman.h"            /* where huffdecode is declared */
#include "e_sbr_error.h"
#include "sbr_read_data.h"
#include "sbr_open.h"

/*----------------------------------------------------------------------------
; MACROS
; Define module specific macros here
----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
; DEFINES
; Include all pre-processor statements here. Include conditional
; compile variables also.
----------------------------------------------------------------------------*/

#define ERROR_BUFFER_OVERRUN (-2)
#define KCODEC_INIT_FAILURE (-1)
#define LEFT (0)
#define RIGHT (1)

/*----------------------------------------------------------------------------
; LOCAL FUNCTION DEFINITIONS
; Function Prototype declaration
----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
; LOCAL STORE/BUFFER/POINTER DEFINITIONS
; Variable declaration - defined here and used outside this module
----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
; EXTERNAL FUNCTION REFERENCES
; Declare functions defined elsewhere and referenced in this module
----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
; EXTERNAL GLOBAL STORE/BUFFER/POINTER REFERENCES
; Declare variables used in this module but defined elsewhere
----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
; FUNCTION CODE
----------------------------------------------------------------------------*/

OSCL_EXPORT_REF Int32 GetActualAacConfig(UInt8* aConfigHeader,
        UInt8* aAudioObjectType,
        Int32* aConfigHeaderSize,
        UInt8* SamplingRateIndex,
        UInt32* NumChannels)
{

    tPVMP4AudioDecoderExternal * iAACDecExt = NULL;
    UInt           initialUsedBits;  /* Unsigned for C55x */
    tDec_Int_File *pVars;            /* Helper pointer */
    MC_Info       *pMC_Info;         /* Helper pointer */


    Int            status = ERROR_BUFFER_OVERRUN;


    /*
     *  Allocate memory to decode one AAC frame
     */


    iAACDecExt = malloc(sizeof(tPVMP4AudioDecoderExternal));
    if (!iAACDecExt)
    {
        return 1;
    }
    iAACDecExt->inputBufferCurrentLength = 0;


    iAACDecExt->pInputBuffer = aConfigHeader;
    iAACDecExt->inputBufferMaxLength = PVMP4AUDIODECODER_INBUFSIZE;


    iAACDecExt->inputBufferUsedLength    = 0;
    iAACDecExt->remainderBits            = 0;

    Int32 memreq =  PVMP4AudioDecoderGetMemRequirements();

    UInt8 *pMem = calloc(memreq, sizeof(UInt8));

    if (pMem == 0)
    {
        return KCODEC_INIT_FAILURE;
    }

    if (PVMP4AudioDecoderInitLibrary(iAACDecExt, pMem) != 0)
    {
        return KCODEC_INIT_FAILURE;
    }

    iAACDecExt->inputBufferCurrentLength =  *aConfigHeaderSize;


    /*
     * Initialize "helper" pointers to existing memory.
     */

    pVars = (tDec_Int_File *)pMem;


    pMC_Info = &pVars->mc_info;



    /*
     * Translate input buffer variables.
     */
    pVars->inputStream.pBuffer = iAACDecExt->pInputBuffer;

    pVars->inputStream.availableBits =
        (UInt)(iAACDecExt->inputBufferCurrentLength << INBUF_ARRAY_INDEX_SHIFT);

    initialUsedBits =
        (UInt)((iAACDecExt->inputBufferUsedLength << INBUF_ARRAY_INDEX_SHIFT) +
               iAACDecExt->remainderBits);

    pVars->inputStream.inputBufferCurrentLength =
        (UInt)iAACDecExt->inputBufferCurrentLength;

    pVars->inputStream.usedBits = initialUsedBits;

    pVars->aacPlusEnabled = TRUE;   /* Always enable aacplus decoding */


    if (initialUsedBits <= pVars->inputStream.availableBits)
    {
        /*
         * Buffer is not overrun, then
         * decode the AudioSpecificConfig() structure
         */
        pVars->aacConfigUtilityEnabled = TRUE;  /* set aac utility mode */

        status = get_audio_specific_config(pVars);

    }


    byte_align(&pVars->inputStream);

    *aConfigHeaderSize = (Int32)((pVars->inputStream.usedBits) >> 3);


    *SamplingRateIndex = pVars->prog_config.sampling_rate_idx;

    *NumChannels = pVars->mc_info.nch;

    /*
     *  Set the audio object type to the extended type, only if this is different
     *  from the baseline object type (this only applies to explicit signaling)
     */
    if (pVars->mc_info.audioObjectType != pVars->mc_info.ExtendedAudioObjectType)
    {
        *aAudioObjectType = pVars->mc_info.ExtendedAudioObjectType;
    }
    else
    {
        *aAudioObjectType = pVars->mc_info.audioObjectType;
    }

    /*
     *  Set parameters  based on the explicit information from the
     *  audio specific config
     */

    if (pVars->mc_info.sbrPresentFlag)
    {
        if (pVars->mc_info.psPresentFlag)
        {
            *NumChannels += 1;
        }
    }

    pVars->status = status;

    /*
     *  Clear allocated memory
     */
    if (pMem != NULL)
    {
        free(pMem);
        pMem = NULL;
    }


    free(iAACDecExt);
    iAACDecExt = NULL;

    return status;
}
