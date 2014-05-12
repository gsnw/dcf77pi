/*
Copyright (c) 2013-2014 René Ladan. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
SUCH DAMAGE.
*/

#include "decode_alarm.h"

void
decode_alarm(uint8_t *buf, struct alm *alarm)
{
	alarm->ds1 = buf[0] + 2 * buf[1] + 4 * buf[3];
	alarm->ps1 = buf[2] + 2 * buf[4] + 4 * buf[5];
	alarm->dl1 = buf[12] + 2 * buf[13] + 4 * buf[14] + 8 * buf[15] +
	    16 * buf[16] + 32 * buf[17] + 64 * buf[19] + 128 * buf[20] +
	    256 * buf[21] + 512 * buf[23];
	alarm->pl1 = buf[18] + 2 * buf[22] + 4 * buf[24] + 8 * buf[25];

	alarm->ds2 = buf[6] + 2 * buf[7] + 4 * buf[9];
	alarm->ps2 = buf[8] + 2 * buf[10] + 4 * buf[11];
	alarm->dl2 = buf[26] + 2 * buf[27] + 4 * buf[28] + 8 * buf[29] +
	    16 * buf[30] + 32 * buf[31] + 64 * buf[33] + 128 * buf[34] +
	    256 * buf[35] + 512 * buf[37];
	alarm->pl2 = buf[32] + 2 * buf[36] + 4 * buf[38] + 8 * buf[39];
}
