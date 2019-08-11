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
#include "common-gpu.h"
#include "common-logging.h"

/* Get the vendor for a device */
enum GPUVendor gamemode_get_gpu_vendor(long device)
{
	enum GPUVendor vendor = Vendor_Invalid;

	/* Fill in GPU vendor */
	char path[64] = { 0 };
	if (snprintf(path, 64, "/sys/class/drm/card%ld/device/vendor", device) < 0) {
		LOG_ERROR("snprintf failed, will not apply gpu optimisations!\n");
		return Vendor_Invalid;
	}
	FILE *file = fopen(path, "r");
	if (!file) {
		LOG_ERROR("Couldn't open vendor file at %s, will not apply gpu optimisations!\n", path);
		return Vendor_Invalid;
	}
	char buff[64];
	bool got_line = fgets(buff, 64, file) != NULL;
	fclose(file);

	if (got_line) {
		vendor = strtol(buff, NULL, 0);
	} else {
		LOG_ERROR("Couldn't read contents of file %s, will not apply optimisations!\n", path);
		return Vendor_Invalid;
	}

	/* verify GPU vendor */
	if (!GPUVendorValid(vendor)) {
		LOG_ERROR("Unknown vendor value (0x%04x) found, cannot apply optimisations!\n",
		          (unsigned int)vendor);
		LOG_ERROR("Known values are: 0x%04x (NVIDIA) 0x%04x (AMD) 0x%04x (Intel)\n",
		          (unsigned int)Vendor_NVIDIA,
		          (unsigned int)Vendor_AMD,
		          (unsigned int)Vendor_Intel);
		return Vendor_Invalid;
	}

	return vendor;
}
