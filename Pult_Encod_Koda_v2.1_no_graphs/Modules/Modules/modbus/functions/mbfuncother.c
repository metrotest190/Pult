/*
 * FreeModbus Libary: A portable Modbus implementation for Modbus ASCII/RTU.
 * Copyright (c) 2006-2018 Christian Walter <cwalter@embedded-solutions.at>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/* ----------------------- System includes ----------------------------------*/
#include "stdlib.h"
#include "string.h"

/* ----------------------- Platform includes --------------------------------*/
#include "port.h"

/* ----------------------- Modbus includes ----------------------------------*/
#include "mb.h"
#include "mbframe.h"
#include "mbproto.h"
#include "mbconfig.h"

#if MB_FUNC_OTHER_REP_SLAVEID_ENABLED > 0

/* ----------------------- Static variables ---------------------------------*/
static UCHAR    ucMBSlaveID[MB_FUNC_OTHER_REP_SLAVEID_BUF];
static USHORT   usMBSlaveIDLen;

/* ----------------------- Start implementation -----------------------------*/

eMBErrorCode
eMBSetSlaveID( UCHAR ucSlaveID, BOOL xIsRunning,
               UCHAR const *pucAdditional, USHORT usAdditionalLen )
{
    /* The buffer stores Slave ID, run status and optional additional data. */
    if( usAdditionalLen > ( MB_FUNC_OTHER_REP_SLAVEID_BUF - 2U ) )
    {
        return MB_ENORES;
    }
    if( ( usAdditionalLen != 0U ) && ( pucAdditional == NULL ) )
    {
        return MB_EINVAL;
    }

    ucMBSlaveID[0] = ucSlaveID;
    ucMBSlaveID[1] = ( UCHAR )( xIsRunning ? 0xFFU : 0x00U );
    if( usAdditionalLen != 0U )
    {
        memcpy( &ucMBSlaveID[2], pucAdditional, ( size_t )usAdditionalLen );
    }
    usMBSlaveIDLen = ( USHORT )( usAdditionalLen + 2U );

    return MB_ENOERR;
}

eMBException
eMBFuncReportSlaveID( UCHAR * pucFrame, USHORT * usLen )
{
    /* FC17 response: function code, byte count, Slave ID payload. The
       response is written into the same buffer that held the request, so
       the function code must be written explicitly and not be relied on
       to survive from the request frame. */
    pucFrame[MB_PDU_FUNC_OFF] = MB_FUNC_OTHER_REPORT_SLAVEID;
    pucFrame[MB_PDU_DATA_OFF] = ( UCHAR )usMBSlaveIDLen;
    memcpy( &pucFrame[MB_PDU_DATA_OFF + 1U], &ucMBSlaveID[0],
            ( size_t )usMBSlaveIDLen );
    *usLen = ( USHORT )( MB_PDU_DATA_OFF + 1U + usMBSlaveIDLen );

    return MB_EX_NONE;
}

#endif
