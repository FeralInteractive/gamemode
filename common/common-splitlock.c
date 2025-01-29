/*

Copyright (c) 2025, the GameMode contributors
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

#define _GNU_SOURCE

#include "common-splitlock.h"
#include "common-logging.h"

/**
 * Path for the split lock mitigation state
 */
const char *splitlock_path = "/proc/sys/kernel/split_lock_mitigate";

/**
 * Return the current split lock mitigation state
 */
long get_splitlock_state(void)
{
	FILE *f = fopen(splitlock_path, "r");
	if (!f) {
		LOG_ERROR("Failed to open file for read %s\n", splitlock_path);
		return -1;
	}

	char contents[41] = { 0 };
	long value = -1;

	if (fread(contents, 1, sizeof contents - 1, f) > 0) {
		value = strtol(contents, NULL, 10);
	} else {
		LOG_ERROR("Failed to read contents of %s\n", splitlock_path);
	}
	fclose(f);

	return value;
}
