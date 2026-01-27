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

 Filename: huffdecode.cpp

------------------------------------------------------------------------------
 INPUT AND OUTPUT DEFINITIONS

 Inputs:

    id_syn_ele  = identification flag for channel syntactic element, Int

    pInputStream= pointer to input bitstream, BITS.

    pVars       = pointer to structure that holds information for decoding,
                  tDec_Int_File

    pChVars[]   = pointer to structure that holds channel information,
                  tDec_Int_Chan


 Local Stores/Buffers/Pointers Needed:
    None

 Global Stores/Buffers/Pointers Needed:
    None

 Outputs:
    status = 0  if success
             non-zero  otherwise

 Pointers and Buffers Modified:
    pChVars->sect   contents updated by newly decoded section information
                    of current frame

    pChVars->factors contents updated by newly decoded scalefactors

    pChVars->ch_coef contents updated by newly decoded spectral coefficients

    PChVars->tns    contents updated by newly decoded TNS information

    pVars->hasmask  contents updated by newly decoded Mid/Side mask
                    information

    pVars->pulseInfo contents updated by newly decoded pulse data information

 Local Stores Modified:
    None

 Global Stores Modified:
    None

------------------------------------------------------------------------------
 FUNCTION DESCRIPTION

  This function offers a framework for decoding the data of the next 1024
  samples. It maps the channel configuration according to the id_syn_ele flag,
  configures the channel information, and calls getics to do huffman decoding
  The function returns 1 if there was an error

------------------------------------------------------------------------------
 REQUIREMENTS

 This function should set up the channel configuration for huffman decoding

------------------------------------------------------------------------------
 REFERENCES

 (1) MPEG-2 NBC Audio Decoder
   "This software module was originally developed by AT&T, Dolby
   Laboratories, Fraunhofer Gesellschaft IIS in the course of development
   of the MPEG-2 NBC/MPEG-4 Audio standard ISO/IEC 13818-7, 14496-1,2 and
   3. This software module is an implementation of a part of one or more
   MPEG-2 NBC/MPEG-4 Audio tools as specified by the MPEG-2 NBC/MPEG-4
   Audio standard. ISO/IEC  gives users of the MPEG-2 NBC/MPEG-4 Audio
   standards free license to this software module or modifications thereof
   for use in hardware or software products claiming conformance to the
   MPEG-2 NBC/MPEG-4 Audio  standards. Those intending to use this software
   module in hardware or software products are advised that this use may
   infringe existing patents. The original developer of this software
   module and his/her company, the subsequent editors and their companies,
   and ISO/IEC have no liability for use of this software module or
   modifications thereof in an implementation. Copyright is not released
   for non MPEG-2 NBC/MPEG-4 Audio conforming products.The original
   developer retains full right to use the code for his/her own purpose,
   assign or donate the code to a third party and to inhibit third party
   from using the code for non MPEG-2 NBC/MPEG-4 Audio conforming products.
   This copyright notice must be included in all copies or derivative
   works."
   Copyright(c)1996.

 (2) ISO/IEC 14496-3: 1999(E)
    Subpart 4       p15     (single_channel_element, channel_pair_element)
                    p15     (Table 4.4.5    getmask)
                    p16     (Table 4.4.6    get_ics_info)
                    p24     (Table 4.4.24   getics)

------------------------------------------------------------------------------
 PSEUDO-CODE

    tag = CALL getbits(LEN_TAG,pInputStream)
                MODIFYING(pInputStream)
                RETURNING(tag)

    common_window = 0;

    IF (id_syn_ele == ID_CPE)
    THEN
        common_window = CALL getbits(LEN_COM_WIN,pInputStream);
                                MODIFYING(pInputStream)
                                RETURNING(common_window)
    ENDIF

    pMcInfo = &pVars->mc_info;

    IF ( (pMcInfo->ch_info[0].cpe != id_syn_ele) OR
         (pMcInfo->ch_info[0].tag != tag) )
    THEN
        status = 1;
    ENDIF


    IF (status == SUCCESS)
    THEN
        IF (id_syn_ele == ID_SCE)
        THEN

            leftCh  = 0;
            RIGHT = 0;
            pChVars[leftCh]->hasmask = 0;
        ELSEIF (id_syn_ele == ID_CPE)

            leftCh = 0;
            rightCh  = 1;

            IF (common_window != FALSE)
            THEN

                CALL get_ics_info(
                        audioObjectType = pVars->mc_info.audioObjectType,
                        pInputStream = pInputStream,
                        common_window = common_window,
                        pWindowSequence = &pChVars[leftCh]->wnd,
                        &pChVars[leftCh]->wnd_shape_this_bk,
                        pChVars[leftCh]->group,
                        &pChVars[leftCh]->max_sfb,
                        pVars->winmap,
                        &pChVars[leftCh]->lt_status,
                        &pChVars[rightCh]->lt_status);
                     MODIFYING(pInputStream, wnd, wnd_shape_this_bk,group,
                               max_sfb, lt_status)
                     RETURNING(status)

                IF (status == SUCCESS)
                THEN

                    pChVars[rightCh]->wnd = pChVars[leftCh]->wnd;
                    pChVars[rightCh]->wnd_shape_this_bk =
                        pChVars[leftCh]->wnd_shape_this_bk;
                    pChVars[rightCh]->max_sfb = pChVars[leftCh]->max_sfb;
                    pv_memcpy(
                        pChVars[rightCh]->group,
                        pChVars[leftCh]->group,
                        NSHORT*sizeof(pChVars[leftCh]->group[0]));

                    hasmask = CALL getmask(
                                    pVars->winmap[pChVars[leftCh]->wnd],
                                    pInputStream,
                                    pChVars[leftCh]->group,
                                    pChVars[leftCh]->max_sfb,
                                    pChVars[leftCh]->mask);
                                MODIFYING(pInputStream, mask)
                                RETURNING(hasmask)

                    IF (hasmask == MASK_ERROR)
                    THEN
                        status = 1;
                    ENDIF
                    pChVars[leftCh]->hasmask  = hasmask;
                    pChVars[rightCh]->hasmask = hasmask;

                ENDIF

            ELSE

                 pChVars[leftCh]->hasmask  = 0;
                 pChVars[rightCh]->hasmask = 0;
            ENDIF(common_window)

        ENDIF(id_syn_ele)

    ENDIF (status)

    ch = leftCh;

    WHILE((ch <= rightCh) AND (status == SUCCESS))

        status = CALL getics(
                        pInputStream,
                        common_window,
                        pVars,
                        pChVars[ch],
                        pChVars[ch]->group,
                        &pChVars[ch]->max_sfb,
                        pChVars[ch]->cb_map,
                        &pChVars[ch]->tns,
                        pVars->winmap,
                        &pVars->pulseInfo,
                        pChVars[ch]->sect);
                    MODIFYING(pInputStream,pVarsp,ChVars[ch],group,
                              max_sfb,tns,pulseInfo,sect)
                    RETURNING(status)

        ch++;

    ENDWHILE

    RETURN status;

------------------------------------------------------------------------------
*/


/*----------------------------------------------------------------------------
; INCLUDES
----------------------------------------------------------------------------*/
#include    "pv_audio_type_defs.h"
#include    "aac_mem_funcs.h"
#include    "huffman.h"
#include    "e_maskstatus.h"
#include    "e_elementid.h"
#include    "get_ics_info.h"

/*----------------------------------------------------------------------------
; MACROS
; Define module specific macros here
----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
; DEFINES
; Include all pre-processor statements here. Include conditional
; compile variables also.
----------------------------------------------------------------------------*/
#define LEFT  (0)
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
Int huffdecode(
    Int           id_syn_ele,
    BITS          *pInputStream,
    tDec_Int_File *pVars,
    tDec_Int_Chan *pChVars[])

{
    /*----------------------------------------------------------------------------
    ; Define all local variables
    ----------------------------------------------------------------------------*/
    Int      ch;
    Int      common_window;
    Int      hasmask;
    Int      status   = SUCCESS;
    Int      num_channels = 0;
    MC_Info  *pMcInfo;

    per_chan_share_w_fxpCoef *pChLeftShare;  /* Helper pointer */
    per_chan_share_w_fxpCoef *pChRightShare; /* Helper pointer */
    /*----------------------------------------------------------------------------
    ; Function body here
    ----------------------------------------------------------------------------*/

    get9_n_lessbits(
        LEN_TAG,
        pInputStream);

    /* suppose an un-supported id_syn_ele will never be passed */

    common_window = 0;

    if (id_syn_ele == ID_CPE)
    {
        common_window =
            get1bits(pInputStream);
    }

    pMcInfo = &pVars->mc_info;

    /*
     *  check if provided info (num of channels) on audio config,
     *  matches read bitstream data, if not, allow update only once.
     *  In almost all cases it should match.
     */
    if ((pMcInfo->ch_info[0].cpe != id_syn_ele))
    {
        if (pVars->mc_info.implicit_channeling)     /* check done only once */
        {
            pMcInfo->ch_info[0].cpe = id_syn_ele & 1; /*  collect info from bitstream
                                                     *  implicit_channeling flag is locked
                                                     *  after 1st frame, to avoid toggling
                                                     *  parameter in the middle of the clip
                                                     */
            pMcInfo->nch = (id_syn_ele & 1) + 1;     /* update number of channels */
        }
        else
        {
            status = 1; /* ERROR break if syntax error persist  */
        }
    }

    if (status == SUCCESS)
    {
        if (id_syn_ele == ID_SCE)
        {

            num_channels = 1;
            pVars->hasmask = 0;
        }
        else if (id_syn_ele == ID_CPE)
        {
            pChLeftShare = pChVars[LEFT]->pShareWfxpCoef;
            pChRightShare = pChVars[RIGHT]->pShareWfxpCoef;
            num_channels = 2;

            if (common_window != FALSE)
            {

                status = get_ics_info(
                             (tMP4AudioObjectType) pVars->mc_info.audioObjectType,
                             pInputStream,
                             (Bool)common_window,
                             (WINDOW_SEQUENCE *) & pChVars[LEFT]->wnd,
                             (WINDOW_SHAPE *) & pChVars[LEFT]->wnd_shape_this_bk,
                             pChLeftShare->group,
                             (Int *) & pChLeftShare->max_sfb,
                             pVars->winmap,
                             (LT_PRED_STATUS *) & pChLeftShare->lt_status,
                             (LT_PRED_STATUS *) & pChRightShare->lt_status);

                if (status == SUCCESS)
                {
                    /* copy left channel info to right channel */
                    pChVars[RIGHT]->wnd = pChVars[LEFT]->wnd;
                    pChVars[RIGHT]->wnd_shape_this_bk =
                        pChVars[LEFT]->wnd_shape_this_bk;
                    pChRightShare->max_sfb = pChLeftShare->max_sfb;
                    pv_memcpy(
                        pChRightShare->group,
                        pChLeftShare->group,
                        NSHORT*sizeof(pChLeftShare->group[0]));

                    hasmask = getmask(
                                  pVars->winmap[pChVars[LEFT]->wnd],
                                  pInputStream,
                                  pChLeftShare->group,
                                  pChLeftShare->max_sfb,
                                  pVars->mask);

                    if (hasmask == MASK_ERROR)
                    {
                        status = 1; /* ERROR code */
                    }
                    pVars->hasmask  = hasmask;

                } /* if (status == 0) */
            }
            else
            {
                pVars->hasmask  = 0;
            } /* if (common_window) */

        } /* if (id_syn_ele) */

    } /* if (status) */

    ch = 0;
    while ((ch < num_channels) && (status == SUCCESS))
    {
        pChLeftShare = pChVars[ch]->pShareWfxpCoef;

        status = getics(
                     pInputStream,
                     common_window,
                     pVars,
                     pChVars[ch],
                     pChLeftShare->group,
                     &pChLeftShare->max_sfb,
                     pChLeftShare->cb_map,
                     &pChLeftShare->tns,
                     pVars->winmap,
                     &pVars->share.a.pulseInfo,
                     pVars->share.a.sect);

        ch++;

    } /* while (ch) */

    /*----------------------------------------------------------------------------
    ; Return status
    ----------------------------------------------------------------------------*/

    return status;

} /* huffdecode */

