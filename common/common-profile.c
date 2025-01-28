/*

Copyright (c) 2025, MithicSpirit
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

#include "common-profile.h"
#include "common-logging.h"

/**
 * Path for platform profile
 */
const char *profile_path = "/sys/firmware/acpi/platform_profile";

/**
 * Return the current platform profile state
 */
const char *get_profile_state(void)
{
	/* Persistent profile state */
	static char profile[64] = { 0 };
	memset(profile, 0, sizeof(profile));

	FILE *f = fopen(profile_path, "r");
	if (!f) {
		LOG_ERROR("Failed to open file for read %s\n", profile_path);
		return "none";
	}

	/* Grab the file length */
	fseek(f, 0, SEEK_END);
	long length = ftell(f);
	fseek(f, 0, SEEK_SET);

	if (length == -1) {
		LOG_ERROR("Failed to seek file %s\n", profile_path);
	} else {
		char contents[length + 1];

		if (fread(contents, 1, (size_t)length, f) > 0) {
			strtok(contents, "\n");
			strncpy(profile, contents, sizeof(profile) - 1);
		} else {
			LOG_ERROR("Failed to read contents of %s\n", profile_path);
		}
	}

	fclose(f);

	return profile;
}
