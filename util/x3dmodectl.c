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

#include "common-logging.h"

#include <errno.h>
#include <glob.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define X3D_MODE_GLOB_PATTERN "/sys/bus/platform/drivers/amd_x3d_vcache/*/amd_x3d_mode"

static char x3d_mode_path[PATH_MAX] = { 0 };

/**
 * Find and set the x3d mode sysfs path
 */
static bool find_x3d_mode_path(void)
{
	if (x3d_mode_path[0] != '\0') {
		return access(x3d_mode_path, F_OK) == 0;
	}

	glob_t glob_result;
	if (glob(X3D_MODE_GLOB_PATTERN, GLOB_NOSORT, NULL, &glob_result) != 0) {
		return false;
	}

	if (glob_result.gl_pathc > 0) {
		strncpy(x3d_mode_path, glob_result.gl_pathv[0], PATH_MAX - 1);
		x3d_mode_path[PATH_MAX - 1] = '\0';
	}

	globfree(&glob_result);
	return x3d_mode_path[0] != '\0' && access(x3d_mode_path, F_OK) == 0;
}

/**
 * Check if x3d mode control is available
 */
static bool x3d_mode_available(void)
{
	return find_x3d_mode_path();
}

/**
 * Return the current x3d mode
 */
static const char *get_x3d_mode(void)
{
	static char mode[64] = { 0 };
	memset(mode, 0, sizeof(mode));

	if (!x3d_mode_available()) {
		return "unavailable";
	}

	FILE *f = fopen(x3d_mode_path, "r");
	if (!f) {
		LOG_ERROR("Failed to open x3d mode file for read %s: %s\n", x3d_mode_path, strerror(errno));
		return "error";
	}

	if (fgets(mode, sizeof(mode), f) != NULL) {
		/* Remove trailing newline */
		char *newline = strchr(mode, '\n');
		if (newline) {
			*newline = '\0';
		}
	} else {
		LOG_ERROR("Failed to read x3d mode from %s: %s\n", x3d_mode_path, strerror(errno));
		fclose(f);
		return "error";
	}

	fclose(f);
	return mode;
}

/**
 * Set the x3d mode to the specified value
 */
static int set_x3d_mode(const char *value)
{
	if (!x3d_mode_available()) {
		LOG_ERROR("AMD x3D mode control is not available on this system\n");
		return EXIT_FAILURE;
	}

	/* Validate the mode value */
	if (strcmp(value, "frequency") != 0 && strcmp(value, "cache") != 0) {
		LOG_ERROR("Invalid x3d mode '%s'. Valid modes are 'frequency' or 'cache'\n", value);
		return EXIT_FAILURE;
	}

	FILE *f = fopen(x3d_mode_path, "w");
	if (!f) {
		LOG_ERROR("Failed to open x3d mode file for write %s: %s\n",
		          x3d_mode_path,
		          strerror(errno));
		return EXIT_FAILURE;
	}

	int res = fprintf(f, "%s\n", value);
	if (res < 0) {
		LOG_ERROR("Failed to set x3d mode to %s: %s\n", value, strerror(errno));
		fclose(f);
		return EXIT_FAILURE;
	}

	fclose(f);
	return EXIT_SUCCESS;
}

/**
 * Main entry point, dispatch to the appropriate helper
 */
int main(int argc, char *argv[])
{
	if (argc == 2 && strncmp(argv[1], "get", 3) == 0) {
		printf("%s", get_x3d_mode());
	} else if (argc == 3 && strncmp(argv[1], "set", 3) == 0) {
		const char *value = argv[2];

		if (geteuid() != 0) {
			LOG_ERROR("This program must be run as root\n");
			return EXIT_FAILURE;
		}

		return set_x3d_mode(value);
	} else {
		fprintf(stderr, "usage: x3dmodectl [get] [set VALUE]\n");
		fprintf(stderr, "where VALUE can be 'frequency' or 'cache'\n");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
