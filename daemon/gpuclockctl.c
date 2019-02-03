/*

Copyright (c) 2017-2018, Feral Interactive
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

#include "logging.h"

#include "gpu-query.h"

/**
 * Main entry point, dispatch to the appropriate helper
 */
int main(int argc, char *argv[])
{
	if (argc == 4 && strncmp(argv[1], "get", 3) == 0) {
		const char *vendor = argv[1];
		const char *device = argv[2];

		struct GameModeGPUInfo info;
		/* TODO Populate with vendor and device */

		get_gpu_state(&info);

		printf("%ld %ld\n", info.core, info.mem);

	} else if (argc == 6 && strncmp(argv[3], "set", 3) == 0) {
		const char *vendor = argv[1];
		const char *device = argv[2];
		const char *core = argv[4];
		const char *mem = argv[5];

		/* Must be root to set the state */
		if (geteuid() != 0) {
			fprintf(stderr, "This program must be run as root\n");
			return EXIT_FAILURE;
		}

		struct GameModeGPUInfo info;
		/* TODO Populate with vendor, device and clocks */

		return set_gpu_state(&info);
	} else {
		fprintf(stderr, "usage: gpuclockctl PCI_ID DEVICE [get] [set CORE MEM]\n");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
