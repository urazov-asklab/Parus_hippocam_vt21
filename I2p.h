/*
 * I2p.h
 *
 * Copyright (C) 2010 Texas Instruments Incorporated - http://www.ti.com/ 
 * 
 * 
 *  Redistribution and use in source and binary forms, with or without 
 *  modification, are permitted provided that the following conditions 
 *  are met:
 *
 *    Redistributions of source code must retain the above copyright 
 *    notice, this list of conditions and the following disclaimer.
 *
 *    Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the 
 *    documentation and/or other materials provided with the   
 *    distribution.
 *
 *    Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT 
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT 
 *  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, 
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT 
 *  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
*/

/**
 * Provides a simple interface to IUNIVERSAL (XDM 1.0) based algorithms.
 */

#ifndef _I2P_H_
#define _I2P_H_

#include <xdc/std.h>

#include <ti/sdo/ce/Engine.h>

#include <ti/sdo/dmai/Dmai.h>
#include <ti/sdo/dmai/Buffer.h>
#include <ti/sdo/ce/universal/universal.h>
#include <ti/sdo/codecs/deinterlacer/ii2p_ti.h>


#define CNTBUFSIZE  (128)

/**
 *  Handle through which to reference a Video Deinterlacer algorithm.
 */
typedef struct I2p_Object *I2p_Handle;


/**
 * Opaque handle to a I2P codec.
 */
typedef VISA_Handle I2P_Handle;

/**
 *  This structure defines the parameters necessary to create an
 *  instance of a I2P-based video deinterlacer.
 *
 *     size,
 *     width,
 *     height,
 *     videoFormat,
 *     threshold,
 *     algSelect,
 */
typedef struct II2P_Params I2P_Params;

/**
 *  Default XDM dynamic parameters for a IUNIVERSAL algorithm.
 */
typedef UNIVERSAL_DynamicParams I2P_DynamicParams;

#if defined (__cplusplus)
extern "C" {
#endif

/**
 * Creates a Video Deinterlacer (XDM 1.0) algorithm instance.
 *
 * Parameters:
 * hEngine     An opened engine containing the algorithm to create.
 * codecName   The name of the algorithm to open. Corresponds to
 *             the string name given in the .cfg file.
 * params      XDM parameters to use while creating the codec.
 * dynParams   XDM dynamic parameters to use while creating the
 *             codec.
 *
 * Return value:
 * Handle for use in subsequent operations (see #I2p_Handle).
 * NULL for failure.
 */
extern I2p_Handle I2p_create(Engine_Handle hEngine,
                                 char *codecName,
                                 I2P_Params *params,
                                 I2P_DynamicParams *dynParams);

/**
 * Deinterlaces a video buffer.
 *
 * Parameters:
 * hI2p        The #I2p_Handle to use for encoding.
 * hInBuf      The #Buffer_Handle for the buffer containing the raw
 *             data.
 * hOutBufB    The #Buffer_Handle for the buffer to fill with
 *             previous frame data.
 * hOutBuf     The #Buffer_Handle for the buffer to fill with
 *             encoded data.
 *
 * Return value:
 * Dmai_EOK for success.
 * "Negative value" for failure, see Dmai.h.
 *
 * Remarks:
 * #I2p_create must be called before this function.
 *
 */
extern int I2p_process(I2p_Handle hI2p, Buffer_Handle hInBuf, Buffer_Handle hInBufB, Buffer_Handle hOutBuf);

/**
 * Deletes a deinterlacer algorithm instance.
 *
 * Parameters:
 * hI2p         The #I2p_Handle to delete.
 *
 * Return value:
 * Dmai_EOK for success.
 * "Negative value" for failure, see Dmai.h.
 */
extern int I2p_delete(I2p_Handle hI2p);

/**
 * Gives the frame count for deinterlacer algorithm.
 *
 * Parameters:
 * hI2p         The #I2p_Handle to delete.
 *
 * Return value:
 * Return the framecount.
 * "Negative value" for failure, see Dmai.h.
 */
extern int I2p_framecount(I2p_Handle hI2p);

#if defined (__cplusplus)
}
#endif

#endif /* _I2P_H_ */
