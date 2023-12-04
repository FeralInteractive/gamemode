/*

Copyright (c) 2017-2019, Feral Interactive
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

 * Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
 * Neither the name of Feral Interactive nor the names of its contributors
   may be used to endorse or promote products derived from this software
   without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

 */

#include "common-cpu.h"
#include "common-logging.h"

char *parse_cpulist(char *cpulist, long *from, long *to)
{
	if (!cpulist || *cpulist == '\0')
		return NULL;

	char *endp;
	*from = strtol(cpulist, &endp, 10);

	if (endp == cpulist)
		return NULL;

	if (*endp == '\0' || *endp == ',') {
		*to = *from;

		if (*endp == '\0')
			return endp;

		return endp + 1;
	}

	if (*endp != '-')
		return NULL;

	cpulist = endp + 1;
	*to = strtol(cpulist, &endp, 10);

	if (endp == cpulist)
		return NULL;

	if (*to < *from)
		return NULL;

	if (*endp == '\0')
		return endp;

	return endp + 1;
}
