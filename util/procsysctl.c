/*
Copyright (c) 2017-2025, Feral Interactive and the GameMode contributors
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
#include <unistd.h>
#include "common-logging.h"
#include "common-splitlock.h"

static bool write_value(const char *key, const char *value)
{
	FILE *f = fopen(key, "w");

	if (!f) {
		if (errno != ENOENT)
			LOG_ERROR("Couldn't open file at %s (%s)\n", key, strerror(errno));

		return false;
	}

	if (fputs(value, f) == EOF) {
		LOG_ERROR("Couldn't write to file at %s (%s)\n", key, strerror(errno));
		fclose(f);
		return false;
	}

	fclose(f);
	return true;
}

int main(int argc, char *argv[])
{
	if (geteuid() != 0) {
		LOG_ERROR("This program must be run as root\n");
		return EXIT_FAILURE;
	}

	if (argc == 3) {
		if (strcmp(argv[1], "split_lock_mitigate") == 0) {
			if (!write_value(splitlock_path, argv[2]))
				return EXIT_FAILURE;

			return EXIT_SUCCESS;
		} else {
			fprintf(stderr, "unsupported key: '%s'\n", argv[1]);
			return EXIT_FAILURE;
		}
	}

	fprintf(stderr, "usage: procsysctl KEY VALUE\n");
	fprintf(stderr, "where KEY can by any of 'split_lock_mitigate'\n");
	return EXIT_FAILURE;
}
