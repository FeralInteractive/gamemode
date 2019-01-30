
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

#include "gamemode.h"
#include "helpers.h"
#include "logging.h"

#include "daemon_config.h"

// TODO
// Gather GPU type and information
// Allow configuration file specifying of gpu info
// Apply Nvidia GPU settings (CoolBits will be needed)
// Apply AMD GPU settings (Will need user changing pwm1_enable)
// Intel?
// Provide documentation on optimisations

/* Enums for GPU vendors */
enum GPUVendor {
	Vendor_Invalid = 0,
	Vendor_NVIDIA = 0x10de,
	Vendor_AMD = 0x1002,
	Vendor_Intel = 0x8086
};

/* Storage for static GPU info gathered at start */
struct GameModeGPUInfo {
	enum GPUVendor vendor;
	int device; /* path to device, ie. /sys/class/drm/card#/ */

	long core; /* Core clock to apply */
	long mem;  /* Mem clock to apply */
};

/**
 * Applies or removes Nvidia optimisations
 */
static int apply_gpu_nvidia(const GameModeGPUInfo *info, bool apply)
{
	if (!info)
		return 0;

	// Running these commands:
	// nvidia-settings -a '[gpu:0]/GPUMemoryTransferRateOffset[3]=1400'
	// nvidia-settings -a '[gpu:0]/GPUGraphicsClockOffset[3]=50'
	if (apply) {
	} else {
	}

	return 0;
}

/**
 * Applies or removes AMD optimisations
 */
static int apply_gpu_amd(const GameModeGPUInfo *info, bool apply)
{
	if (!info)
		return 0;

	// We'll want to set both the following:
	// core: device/pp_sclk_od (0%+ additional)
	// mem: device/pp_mclk_od (0%+ additional)
	// Guide from https://www.maketecheasier.com/overclock-amd-gpu-linux/
	if (apply) {
	} else {
	}

	return 0;
}

/**
 * Attempts to identify the current in use GPU information
 */
int game_mode_initialise_gpu(GameModeConfig *config, GameModeGPUInfo **info)
{
	int status = 0;

	/* Verify input, this is programmer error */
	if (!info || *info)
		FATAL_ERROR("Invalid GameModeGPUInfo passed to %s", __func__);

	/* Early out if we have this feature turned off */
	long apply = 0;
	config_get_apply_gpu_optimisations(config, &apply);
	if (apply == 0)
		return 0;

	/* Create the context */
	GameModeGPUInfo *new_info = malloc(sizeof(GameModeGPUInfo));
	memset(new_info, 0, sizeof(GameModeGPUInfo));

	// TODO: Fill in the GPU info

	/* Load the config based on GPU */
	switch (new_info->vendor) {
	case Vendor_NVIDIA:
		config_get_nv_core_clock_mhz_offset(config, &new_info->core);
		config_get_nv_mem_clock_mhz_offset(config, &new_info->mem);
		break;
	case Vendor_AMD:
		config_get_amd_core_clock_percentage(config, &new_info->core);
		config_get_amd_mem_clock_percentage(config, &new_info->mem);
		break;
	default:
		break;
	}

	/* Give back the new gpu info */
	*info = new_info;

	return status;
}

/* Simply used to free the GPU info object */
void game_mode_free_gpu(GameModeGPUInfo **info)
{
	/* Simply free the object */
	free(*info);
	*info = NULL;
}

/**
 * Applies GPU optimisations when gamemode is active and removes them after
 */
int game_mode_apply_gpu(const GameModeGPUInfo *info, bool apply)
{
	// Null info means don't apply anything
	if (!info)
		return 0;

	switch (info->vendor) {
	case Vendor_NVIDIA:
		return apply_gpu_nvidia(info, apply);
	case Vendor_AMD:
		return apply_gpu_amd(info, apply);
	default:
		break;
	}

	/* Unsupported GPU vendor, do nothing */
	return 0;
}
