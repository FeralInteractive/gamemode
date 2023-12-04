/*

Copyright (c) 2017-2019, Feral Interactive
Copyright (c) 2019, Intel Corporation
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

#include "common-power.h"
#include "common-logging.h"

#include <linux/limits.h>
#include <assert.h>
#include <ctype.h>
#include <glob.h>
#include <stdio.h>
#include <string.h>

static bool read_file_in_dir(const char *dir, const char *file, char *dest, size_t n)
{
	char path[PATH_MAX];
	int ret = snprintf(path, sizeof(path), "%s/%s", dir, file);
	if (ret < 0 || ret >= (int)sizeof(path)) {
		LOG_ERROR("Path length overrun");
		return false;
	}

	FILE *f = fopen(path, "r");
	if (!f) {
		LOG_ERROR("Failed to open file for read %s\n", path);
		return false;
	}

	size_t read = fread(dest, 1, n, f);

	/* Close before we do any error checking */
	fclose(f);

	if (read <= 0) {
		LOG_ERROR("Failed to read contents of %s: (%s)\n", path, strerror(errno));
		return false;
	}

	if (read >= n) {
		LOG_ERROR("File contained more data than expected %s\n", path);
		return false;
	}

	/* Ensure we're null terminated */
	dest[read] = '\0';

	/* Trim whitespace off the end */
	while (read > 0 && isspace(dest[read - 1])) {
		dest[read - 1] = '\0';
		read--;
	}

	return true;
}

static bool get_energy_uj(const char *rapl_name, uint32_t *energy_uj)
{
	glob_t glo = { 0 };
	static const char *path = "/sys/class/powercap/intel-rapl/intel-rapl:0/intel-rapl:0:*";

	/* Assert some sanity on this glob */
	if (glob(path, GLOB_NOSORT, NULL, &glo) != 0) {
		LOG_ERROR("glob failed for RAPL paths: (%s)\n", strerror(errno));
		return false;
	}

	/* If the glob doesn't find anything, this most likely means we don't
	 * have an Intel CPU or we have a kernel which does not support RAPL on
	 * our CPU.
	 */
	if (glo.gl_pathc < 1) {
		LOG_ONCE(MSG,
		         "Intel RAPL interface not found in sysfs. "
		         "This is only problematic if you expected Intel iGPU "
		         "power threshold optimization.");
		globfree(&glo);
		return false;
	}

	/* Walk the glob set */
	for (size_t i = 0; i < glo.gl_pathc; i++) {
		char name[32];
		if (!read_file_in_dir(glo.gl_pathv[i], "name", name, sizeof(name))) {
			return false;
		}

		/* We're searching for the directory where the file named "name"
		 * contains the contents rapl_name. */
		if (strncmp(name, rapl_name, sizeof(name)) != 0) {
			continue;
		}

		char energy_uj_str[32];
		if (!read_file_in_dir(glo.gl_pathv[i], "energy_uj", energy_uj_str, sizeof(energy_uj_str))) {
			return false;
		}

		char *end = NULL;
		long long energy_uj_ll = strtoll(energy_uj_str, &end, 10);
		if (end == energy_uj_str) {
			LOG_ERROR("Invalid energy_uj contents: %s\n", energy_uj_str);
			return false;
		}

		if (energy_uj_ll < 0) {
			LOG_ERROR("Value of energy_uj is out of expected bounds: %lld\n", energy_uj_ll);
			return false;
		}

		/* Go ahead and clamp to 32 bits.  We assume 32 bits later when
		 * taking deltas and wrapping at 32 bits is exactly what the Linux
		 * kernel's turbostat utility does so it's probably right.
		 */
		*energy_uj = (uint32_t)energy_uj_ll;
		return true;
	}

	/* If we got here then the CPU and Kernel support RAPL and all our file
	 * access has succeeded but we failed to find an entry with the right
	 * name.  This most likely means we're asking for "uncore" but are on a
	 * machine that doesn't have an integrated GPU.
	 */
	return false;
}

bool get_cpu_energy_uj(uint32_t *energy_uj)
{
	return get_energy_uj("core", energy_uj);
}

bool get_igpu_energy_uj(uint32_t *energy_uj)
{
	return get_energy_uj("uncore", energy_uj);
}
