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

#include <linux/limits.h>
#include <sched.h>
#include <unistd.h>

#include "common-cpu.h"
#include "common-logging.h"

static int write_state(char *path, int state)
{
	FILE *f = fopen(path, "w");

	if (!f) {
		LOG_ERROR("Couldn't open file at %s (%s)\n", path, strerror(errno));
		return 0;
	}

	if (putc(state, f) == EOF) {
		LOG_ERROR("Couldn't write to file at %s (%s)\n", path, strerror(errno));
		fclose(f);
		return 0;
	}

	fclose(f);
	return 1;
}

static void log_state(const int state, const long first, const long last)
{
	if (state == '0') {
		if (first == last)
			LOG_MSG("parked core %ld\n", first);
		else
			LOG_MSG("parked cores %ld - %ld\n", first, last);
	} else {
		if (first == last)
			LOG_MSG("unparked core %ld\n", first);
		else
			LOG_MSG("unparked cores %ld - %ld\n", first, last);
	}
}

static int set_state(char *cpulist, int state)
{
	char path[PATH_MAX];
	long from, to;
	char *list = cpulist;

	long first = -1, last = -1;

	while ((list = parse_cpulist(list, &from, &to))) {
		for (long cpu = from; cpu < to + 1; cpu++) {
			if (snprintf(path, PATH_MAX, "/sys/devices/system/cpu/cpu%ld/online", cpu) < 0) {
				LOG_ERROR("snprintf failed, will not apply cpu core parking!\n");
				return 0;
			}

			if (!write_state(path, state)) {
				/* on some systems one cannot park core #0 */
				if (cpu != 0) {
					if (state == '0') {
						LOG_ERROR("unable to park core #%ld, will not apply cpu core parking!\n",
						          cpu);
						return -1;
					}

					LOG_ERROR("unable to unpark core #%ld\n", cpu);
				}
			} else {
				if (first == -1) {
					first = cpu;
					last = cpu;
				} else if (last + 1 == cpu) {
					last = cpu;
				} else {
					log_state(state, first, last);
					first = cpu;
					last = cpu;
				}
			}
		}
	}

	if (first != -1)
		log_state(state, first, last);

	return 1;
}

int main(int argc, char *argv[])
{
	if (geteuid() != 0) {
		LOG_ERROR("This program must be run as root\n");
		return EXIT_FAILURE;
	}

	if (argc == 3 && strncmp(argv[1], "online", 6) == 0) {
		if (!set_state(argv[2], '1'))
			return EXIT_FAILURE;
	} else if (argc == 3 && strncmp(argv[1], "offline", 7) == 0) {
		if (!set_state(argv[2], '0'))
			return EXIT_FAILURE;
	} else {
		fprintf(stderr, "usage: cpucorectl [online]|[offline] VALUE]\n");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
