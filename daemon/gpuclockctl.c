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

#include "gpu-control.h"

/* Helper to quit with usage */
static const char *usage_text = "usage: gpuclockctl PCI_ID DEVICE [get] [set CORE MEM]";
static void print_usage_and_exit(void)
{
	fprintf(stderr, "%s\n", usage_text);
	exit(EXIT_FAILURE);
}

/* Helper to get and verify vendor value */
static long get_vendor(const char *val)
{
	char *end;
	long ret = strtol(val, &end, 0);
	if (!GPUVendorValid(ret) || end == val) {
		LOG_ERROR("ERROR: Invalid GPU Vendor passed (0x%04x)!\n", (unsigned short)ret);
		print_usage_and_exit();
	}
	return ret;
}

/* Helper to get and verify device value */
static long get_device(const char *val)
{
	char *end;
	long ret = strtol(val, &end, 10);
	if (ret < 0 || end == val) {
		LOG_ERROR("ERROR: Invalid GPU device passed (%ld)!\n", ret);
		print_usage_and_exit();
	}
	return ret;
}

/* Helper to get and verify core and mem value */
static long get_coremem(const char *val)
{
	char *end;
	long ret = strtol(val, &end, 10);
	if (ret < 0 || end == val) {
		LOG_ERROR("ERROR: Invalid core or mem value passed (%ld)!\n", ret);
		print_usage_and_exit();
	}
	return ret;
}

/**
 * Main entry point, dispatch to the appropriate helper
 */
int main(int argc, char *argv[])
{
	if (argc == 4 && strncmp(argv[3], "get", 3) == 0) {
		/* Get and verify the vendor and device */
		struct GameModeGPUInfo info;
		memset(&info, 0, sizeof(info));
		info.vendor = get_vendor(argv[1]);
		info.device = get_device(argv[2]);

		/* Fetch the state and print it out */
		get_gpu_state(&info);
		printf("%ld %ld\n", info.core, info.mem);

	} else if (argc == 6 && strncmp(argv[3], "set", 3) == 0) {
		/* Must be root to set the state */
		if (geteuid() != 0) {
			fprintf(stderr, "gpuclockctl must be run as root to set values\n");
			print_usage_and_exit();
		}

		/* Get and verify the vendor and device */
		struct GameModeGPUInfo info;
		memset(&info, 0, sizeof(info));
		info.vendor = get_vendor(argv[1]);
		info.device = get_device(argv[2]);
		info.core = get_coremem(argv[4]);
		info.mem = get_coremem(argv[5]);

		printf("gpuclockctl setting core:%ld mem:%ld on device:%ld with vendor 0x%04x\n",
		       info.core,
		       info.mem,
		       info.device,
		       (unsigned short)info.vendor);

		return set_gpu_state(&info);
	} else {
		print_usage_and_exit();
	}

	return EXIT_SUCCESS;
}
