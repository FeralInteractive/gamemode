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

#include <unistd.h>

/**
 * Sets all governors to a value
 */
static int set_gov_state(const char *value)
{
	char governors[MAX_GOVERNORS][MAX_GOVERNOR_LENGTH] = { { 0 } };
	int num = fetch_governors(governors);
	int retval = EXIT_SUCCESS;
	int res = 0;

	for (int i = 0; i < num; i++) {
		const char *gov = governors[i];
		FILE *f = fopen(gov, "w");
		if (!f) {
			LOG_ERROR("Failed to open file for write %s\n", gov);
			continue;
		}

		res = fprintf(f, "%s\n", value);
		if (res < 0) {
			LOG_ERROR("Failed to set governor %s to %s: %s", gov, value, strerror(errno));
			retval = EXIT_FAILURE;
		}
		fclose(f);
	}

	return retval;
}

/**
 * Main entry point, dispatch to the appropriate helper
 */
int main(int argc, char *argv[])
{
	if (argc == 2 && strncmp(argv[1], "get", 3) == 0) {
		printf("%s", get_gov_state());
	} else if (argc == 3 && strncmp(argv[1], "set", 3) == 0) {
		const char *value = argv[2];

		/* Must be root to set the state */
		if (geteuid() != 0) {
			LOG_ERROR("This program must be run as root\n");
			return EXIT_FAILURE;
		}

		return set_gov_state(value);
	} else {
		fprintf(stderr, "usage: cpugovctl [get] [set VALUE]\n");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
