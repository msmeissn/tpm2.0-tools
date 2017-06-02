//**********************************************************************;
// Copyright (c) 2015, Intel Corporation
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
// this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
// THE POSSIBILITY OF SUCH DAMAGE.
//**********************************************************************;

#include <stdbool.h>

#include <sapi/tpm20.h>

#include "string-bytes.h"

static UINT32 LoadExternalHMACKey(TSS2_SYS_CONTEXT *sapi_contex, TPMI_ALG_HASH hashAlg, TPM2B *key, TPM_HANDLE *keyHandle, TPM2B_NAME *keyName )
{
    TPM2B keyAuth;
    TPM2B_SENSITIVE inPrivate;
    TPM2B_PUBLIC inPublic;

    keyAuth.size = 0;

    inPrivate.t.sensitiveArea.sensitiveType = TPM_ALG_KEYEDHASH;
    inPrivate.t.size = string_bytes_copy_tpm2b( &(inPrivate.t.sensitiveArea.authValue.b), &keyAuth);
    inPrivate.t.sensitiveArea.seedValue.b.size = 0;
    inPrivate.t.size += string_bytes_copy_tpm2b( &inPrivate.t.sensitiveArea.sensitive.bits.b, key);
    inPrivate.t.size += 2 * sizeof( UINT16 );

    inPublic.t.publicArea.type = TPM_ALG_KEYEDHASH;
    inPublic.t.publicArea.nameAlg = TPM_ALG_NULL;
    *( UINT32 *)&( inPublic.t.publicArea.objectAttributes )= 0;
    inPublic.t.publicArea.objectAttributes.sign = 1;
    inPublic.t.publicArea.objectAttributes.userWithAuth = 1;
    inPublic.t.publicArea.authPolicy.t.size = 0;
    inPublic.t.publicArea.parameters.keyedHashDetail.scheme.scheme = TPM_ALG_HMAC;
    inPublic.t.publicArea.parameters.keyedHashDetail.scheme.details.hmac.hashAlg = hashAlg;
    inPublic.t.publicArea.unique.keyedHash.t.size = 0;

    keyName->t.size = sizeof( TPM2B_NAME ) - 2;
    return Tss2_Sys_LoadExternal(sapi_contex, 0, &inPrivate, &inPublic, TPM_RH_NULL, keyHandle, keyName, 0 );
}


//
// This function does an HMAC on a null-terminated list of input buffers.
//
TPM_RC tpm_hmac(TSS2_SYS_CONTEXT *sapi_context, TPMI_ALG_HASH hashAlg, TPM2B *key, TPM2B **bufferList, TPM2B_DIGEST *result )
{
    TPM2B_AUTH nullAuth;
    TPMI_DH_OBJECT sequenceHandle;
    int i;
    TPM2B emptyBuffer;
    TPMT_TK_HASHCHECK validation;

    TPMS_AUTH_COMMAND *sessionDataArray[1];
    TPMS_AUTH_COMMAND sessionData;
    TSS2_SYS_CMD_AUTHS sessionsData;
    TPM2B_AUTH hmac;
    TPM2B_NONCE nonce;

    TPMS_AUTH_RESPONSE *sessionDataOutArray[1];
    TPMS_AUTH_RESPONSE sessionDataOut;
    TSS2_SYS_RSP_AUTHS sessionsDataOut;

    UINT32 rval;
    TPM_HANDLE keyHandle;
    TPM2B_NAME keyName;

    TPM2B keyAuth;

    sessionDataArray[0] = &sessionData;
    sessionDataOutArray[0] = &sessionDataOut;

    // Set result size to 0, in case any errors occur
    result->b.size = 0;

    keyAuth.size = 0;
    nullAuth.t.size = 0;

    rval = LoadExternalHMACKey(sapi_context, hashAlg, key, &keyHandle, &keyName );
    if( rval != TPM_RC_SUCCESS )
    {
        return( rval );
    }

    // Init input sessions struct
    sessionData.sessionHandle = TPM_RS_PW;
    nonce.t.size = 0;
    sessionData.nonce = nonce;
    string_bytes_copy_tpm2b( &(hmac.b), &keyAuth );
    sessionData.hmac = hmac;
    *( (UINT8 *)((void *)&( sessionData.sessionAttributes ) ) ) = 0;
    sessionsData.cmdAuthsCount = 1;
    sessionsData.cmdAuths = &sessionDataArray[0];

    // Init sessions out struct
    sessionsDataOut.rspAuthsCount = 1;
    sessionsDataOut.rspAuths = &sessionDataOutArray[0];

    emptyBuffer.size = 0;

    rval = Tss2_Sys_HMAC_Start( sapi_context, keyHandle, &sessionsData, &nullAuth, hashAlg, &sequenceHandle, 0 );

    if( rval != TPM_RC_SUCCESS )
        return( rval );

    hmac.t.size = 0;
    sessionData.hmac = hmac;
    for( i = 0; bufferList[i] != 0; i++ )
    {
        rval = Tss2_Sys_SequenceUpdate ( sapi_context, sequenceHandle, &sessionsData, (TPM2B_MAX_BUFFER *)( bufferList[i] ), &sessionsDataOut );

        if( rval != TPM_RC_SUCCESS )
            return( rval );
    }

    result->t.size = sizeof( TPM2B_DIGEST ) - 2;
    rval = Tss2_Sys_SequenceComplete ( sapi_context, sequenceHandle, &sessionsData, ( TPM2B_MAX_BUFFER *)&emptyBuffer,
            TPM_RH_PLATFORM, result, &validation, &sessionsDataOut );

    if( rval != TPM_RC_SUCCESS )
        return( rval );

    rval = Tss2_Sys_FlushContext( sapi_context, keyHandle );

    return rval;
}
