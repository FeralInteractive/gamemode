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

#define _GNU_SOURCE

#include "common-governors.h"
#include "common-logging.h"

#include <assert.h>
#include <glob.h>

/**
 * Discover all governers on the system.
 *
 * Located at /sys/devices/system/cpu/cpu(*)/cpufreq/scaling_governor
 */
int fetch_governors(char governors[MAX_GOVERNORS][MAX_GOVERNOR_LENGTH])
{
	glob_t glo = { 0 };
	static const char *path = "/sys/devices/system/cpu/cpu*/cpufreq/scaling_governor";

	/* Assert some sanity on this glob */
	if (glob(path, GLOB_NOSORT, NULL, &glo) != 0) {
		LOG_ERROR("glob failed for cpu governors: (%s)\n", strerror(errno));
		return 0;
	}

	if (glo.gl_pathc < 1) {
		globfree(&glo);
		LOG_ERROR("no cpu governors found\n");
		return 0;
	}

	int num_governors = 0;

	/* Walk the glob set */
	for (size_t i = 0; i < glo.gl_pathc; i++) {
		if (i >= MAX_GOVERNORS) {
			break;
		}

		/* Get the real path to the file.
		 * Traditionally cpufreq symlinks to a policy directory that can
		 * be shared, so let's prevent duplicates.
		 */
		char fullpath[PATH_MAX] = { 0 };
		const char *ptr = realpath(glo.gl_pathv[i], fullpath);
		if (fullpath != ptr) {
			continue;
		}

		/* Only add this governor if it is unique */
		for (int j = 0; j < num_governors; j++) {
			if (strncmp(fullpath, governors[i], PATH_MAX) == 0) {
				continue;
			}
		}

		/* Copy this governor into the output set */
		static_assert(MAX_GOVERNOR_LENGTH > PATH_MAX, "possible string truncation");
		strncpy(governors[num_governors], fullpath, MAX_GOVERNOR_LENGTH);
		num_governors++;
	}

	globfree(&glo);
	return num_governors;
}

/**
 * Return the current governor state
 */
const char *get_gov_state(void)
{
	/* Persistent governor state */
	static char governor[64] = { 0 };
	memset(governor, 0, sizeof(governor));

	/* State for all governors */
	char governors[MAX_GOVERNORS][MAX_GOVERNOR_LENGTH] = { { 0 } };
	int num = fetch_governors(governors);

	/* Check the list */
	for (int i = 0; i < num; i++) {
		const char *gov = governors[i];

		FILE *f = fopen(gov, "r");
		if (!f) {
			LOG_ERROR("Failed to open file for read %s\n", gov);
			continue;
		}

		/* Grab the file length */
		fseek(f, 0, SEEK_END);
		long length = ftell(f);
		fseek(f, 0, SEEK_SET);

		if (length == -1) {
			LOG_ERROR("Failed to seek file %s\n", gov);
		} else {
			char contents[length];

			if (fread(contents, 1, (size_t)length, f) > 0) {
				/* Files have a newline */
				strtok(contents, "\n");
				if (strlen(governor) > 0 && strncmp(governor, contents, 64) != 0) {
					/* Don't handle the mixed case, this shouldn't ever happen
					 * But it is a clear sign we shouldn't carry on */
					LOG_ERROR("Governors malformed: got \"%s\", expected \"%s\"",
					          contents,
					          governor);
					fclose(f);
					return "malformed";
				}

				strncpy(governor, contents, sizeof(governor) - 1);
			} else {
				LOG_ERROR("Failed to read contents of %s\n", gov);
			}
		}

		fclose(f);
	}

	return governor;
}
