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

#pragma once
#define GPU_VALUE_MAX 256

/* Enums for GPU vendors */
enum GPUVendor {
	Vendor_Invalid = 0,
	Vendor_NVIDIA = 0x10de,
	Vendor_AMD = 0x1002,
	Vendor_Intel = 0x8086
};

#define GPUVendorValid(vendor)                                                                     \
	(vendor == Vendor_NVIDIA || vendor == Vendor_AMD || vendor == Vendor_Intel)

/* Storage for GPU info*/
struct GameModeGPUInfo {
	long vendor;
	long device; /* path to device, ie. /sys/class/drm/card#/ */

	long nv_core;            /* Nvidia core clock */
	long nv_mem;             /* Nvidia mem clock */
	long nv_powermizer_mode; /* NV Powermizer Mode */

	char amd_performance_level[GPU_VALUE_MAX]; /* The AMD performance level set to */
};

/* Get the vendor for a device */
enum GPUVendor gamemode_get_gpu_vendor(long device);
