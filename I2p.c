/*
 * I2p.c
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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <xdc/std.h>
#include <ti/sdo/ce/Engine.h>

#include <ti/sdo/dmai/Dmai.h>
#include <ti/sdo/dmai/BufferGfx.h>
#include <ti/sdo/ce/osal/Memory.h>

#include <ti/sdo/ce/universal/universal.h>
#include <ti/sdo/codecs/deinterlacer/ii2p_ti.h>

#include "common.h"

#include "I2p.h"

#define MODULE_NAME     "I2p" // требуется для взаимодействия с фреймворком DMAI

typedef struct I2p_Object 
{
    I2P_Handle              hDeinterlace;
    int                     frameCount;
} I2p_Object;

int I2p_process(I2p_Handle hI2p, Buffer_Handle hInBuf, Buffer_Handle hInBufB, Buffer_Handle hOutBuf)
{
    XDM1_BufDesc            universalInBufDesc;
    XDM1_BufDesc            universalOutBufDesc;
    IDEINTER_InArgs         inputArgs;
    IDEINTER_OutArgs        outputArgs;
    UNIVERSAL_InArgs        universalInArgs;
    UNIVERSAL_OutArgs       universalOutArgs;
    Bool                    test_ela_5_dir_only;
    XDAS_Int32              status;
    XDAS_Int8               *Ydata;
    XDAS_Int8               *YBufFieldB;
    XDAS_Int8               *YdataOut;
    BufferGfx_Dimensions    dim;

    BufferGfx_getDimensions(hInBuf, &dim);

    Ydata                                 = Buffer_getUserPtr(hInBuf);
    YdataOut                              = Buffer_getUserPtr(hOutBuf);

    assert(hI2p);
    assert(hInBuf);
    assert(hOutBuf);
    assert(Ydata);
    assert(YdataOut);
    assert(Buffer_getSize(hInBuf));
    assert(Buffer_getSize(hOutBuf));

    universalOutArgs.extendedError        = 0;
    inputArgs.universalInArgs             = universalInArgs;
    outputArgs.universalOutArgs           = universalOutArgs;
    inputArgs.universalInArgs.size        = sizeof(inputArgs);
    outputArgs.universalOutArgs.size      = sizeof(outputArgs);

    /* Intialize Default Input Parameters*/
    inputArgs.inObject.width              = dim.width;
    inputArgs.inObject.height             = dim.height;
    inputArgs.inObject.imageFormat        = II2P_FOUR22_ILE; // II2P_FOUR22_ILE =1
    inputArgs.inObject.pitch_cur_frame    = dim.width*2;
    inputArgs.inObject.pitch_prev_fld     = dim.width*2;
    inputArgs.inObject.pitch_out_frame    = dim.width*2;
    universalInBufDesc.numBufs            = 2;
    universalOutBufDesc.numBufs           = 1;
    outputArgs.done                       = 0;

    universalInBufDesc.descs[0].bufSize   = (dim.width*2*dim.height)*sizeof(XDAS_UInt8);
    universalInBufDesc.descs[1].bufSize   = (dim.width*2*dim.height)*sizeof(XDAS_UInt8);
    universalOutBufDesc.descs[0].bufSize  = (dim.width*2*dim.height)*sizeof(XDAS_UInt8);

    /*Input and output Arguments*/
    universalInBufDesc.descs[0].buf       = Ydata;
    universalOutBufDesc.descs[0].buf      = YdataOut;

    //test_ela_5_dir_only = FALSE;
    test_ela_5_dir_only = TRUE;

    if ((!hI2p->frameCount) || (test_ela_5_dir_only == TRUE)) 
    {
        inputArgs.inObject.useHistoryData = FALSE;
        universalInBufDesc.descs[1].buf   = Ydata;
    }
    else 
    {
        YBufFieldB                        = Buffer_getUserPtr(hInBufB);
        inputArgs.inObject.useHistoryData = TRUE;
        universalInBufDesc.descs[1].buf   = YBufFieldB;
    }

    /* call the i2p frame apply */
    status = UNIVERSAL_process(hI2p->hDeinterlace, &universalInBufDesc, &universalOutBufDesc, NULL, 
                               (IUNIVERSAL_InArgs *)&inputArgs, (IUNIVERSAL_OutArgs *)&outputArgs);

    if (status!=UNIVERSAL_EOK)
    {
       ERR("I2p: Failed UNIVERSAL_process=%d and Algodone = %u, o.extError:%i\n",
            (int)status, (unsigned int) outputArgs.done, (int) outputArgs.universalOutArgs.extendedError);
       //Dmai_err2("I2p: Failed UNIVERSAL_process=%d and Algodone = %d\n", (int)status, (unsigned int) outputArgs.done);
       //exit(0);
       return -1;
    }

    hI2p->frameCount++;

    return Dmai_EOK;
}

I2p_Handle I2p_create (Engine_Handle hEngine, char *codecName, I2P_Params *params,
                          I2P_DynamicParams *dynParams) {
    I2p_Handle           hI2p;
    UNIVERSAL_Status     universalStatus;
    XDAS_Int32           status;
    UNIVERSAL_Handle     hDeinterlace;
    XDAS_Int8           *cntBuf;

    if (hEngine == NULL || codecName == NULL ||
        params == NULL  || dynParams == NULL) {
        Dmai_err0("Cannot pass null for engine, codec name, params or "
                  "dynamic params\n");
        return NULL;
    }

    /* Allocate space for the object */
    hI2p = (I2p_Handle)calloc(1, sizeof(I2p_Object));

    /* Allocating control buffer */
    cntBuf = (XDAS_Int8 *)Memory_contigAlloc(CNTBUFSIZE, 0);

    if (hI2p == NULL) {
        Dmai_err0("Failed to allocate space for I2p Object\n");
        return NULL;
    }

    /* Create Universal instance */
    hDeinterlace = UNIVERSAL_create(hEngine, codecName, (IUNIVERSAL_Params *)params);

    if (hDeinterlace == NULL) {
        Dmai_err1("Failed to open deinterlacer algorithm: %s \n", codecName);
        free(hI2p);
        return NULL;
    }

    /* Set video encoder dynamic parameters */
    universalStatus.size = sizeof(universalStatus);
    universalStatus.data.numBufs = 1;
    universalStatus.data.descs[0].bufSize = CNTBUFSIZE;
    universalStatus.data.descs[0].buf = cntBuf;

    status = UNIVERSAL_control(hDeinterlace, XDM_SETPARAMS, dynParams, &universalStatus);

    if (status != UNIVERSAL_EOK) {
        Dmai_err1("XDM_SETPARAMS failed, status=%d\n", status);
        UNIVERSAL_delete(hDeinterlace);
        free(hI2p);
        return NULL;
    }

    Dmai_dbg0("Made XDM_SETPARAMS control call\n");

    hI2p->hDeinterlace    = hDeinterlace;
    hI2p->frameCount      = 0;

    return hI2p;
}

int I2p_delete (I2p_Handle hI2p) {
    if (hI2p) {
       if (hI2p->hDeinterlace) {
           UNIVERSAL_delete(hI2p->hDeinterlace);
       }
       free(hI2p);
    }
    return Dmai_EOK;
}

int I2p_framecount (I2p_Handle hI2p) {
    assert(hI2p);
    return hI2p->frameCount;
}

