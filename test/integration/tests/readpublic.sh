#!/bin/bash
#;**********************************************************************;
#
# Copyright (c) 2016-2018, Intel Corporation
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice,
# this list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice,
# this list of conditions and the following disclaimer in the documentation
# and/or other materials provided with the distribution.
#
# 3. Neither the name of Intel Corporation nor the names of its contributors
# may be used to endorse or promote products derived from this software without
# specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
# THE POSSIBILITY OF SUCH DAMAGE.
#;**********************************************************************;

source helpers.sh

alg_primary_obj=0x000B
alg_primary_key=0x0001
alg_create_obj=0x000B
alg_create_key=0x0008

file_primary_key_ctx=context.p_"$alg_primary_obj"_"$alg_primary_key"
file_readpub_key_pub=opu_"$alg_create_obj"_"$alg_create_key"
file_readpub_key_priv=opr_"$alg_create_obj"_"$alg_create_key"
file_readpub_key_name=name.load_"$alg_primary_obj"_"$alg_primary_key"-"$alg_create_obj"_"$alg_create_key"
file_readpub_key_ctx=ctx_load_out_"$alg_primary_obj"_"$alg_primary_key"-"$alg_create_obj"_"$alg_create_key"
file_readpub_output=readpub_"$file_readpub_key_ctx"

Handle_readpub=0x81010014

cleanup() {
    rm -f $file_primary_key_ctx $file_readpub_key_pub $file_readpub_key_priv \
    $file_readpub_key_name $file_readpub_key_ctx $file_readpub_output

    tpm2_evictcontrol -Q -a o -c $Handle_readpub 2>/dev/null || true

    if [ "$1" != "no-shut-down" ]; then
       shut_down
    fi
}
trap cleanup EXIT

start_up

cleanup "no-shut-down"

tpm2_clear

tpm2_createprimary -Q -a e -g $alg_primary_obj -G $alg_primary_key -o $file_primary_key_ctx

tpm2_create -Q -g $alg_create_obj -G $alg_create_key -u $file_readpub_key_pub -r $file_readpub_key_priv  -C file:$file_primary_key_ctx

tpm2_load -Q -C file:$file_primary_key_ctx  -u $file_readpub_key_pub  -r $file_readpub_key_priv -n $file_readpub_key_name -o $file_readpub_key_ctx

tpm2_readpublic -Q -c file:$file_readpub_key_ctx -o $file_readpub_output

tpm2_evictcontrol -Q -a o -c file:$file_readpub_key_ctx -p $Handle_readpub

rm -f $file_readpub_output
tpm2_readpublic -Q -c $Handle_readpub -o $file_readpub_output

exit 0
