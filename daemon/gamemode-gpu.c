
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

#include "config.h"
#include "external-helper.h"
#include "gamemode.h"
#include "helpers.h"
#include "logging.h"

#include "daemon_config.h"
#include "gpu-control.h"

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
	char apply[CONFIG_VALUE_MAX];
	config_get_apply_gpu_optimisations(config, apply);
	if (strlen(apply) == 0) {
		return 0;
	} else if (strncmp(apply, "accept-responsibility", CONFIG_VALUE_MAX) != 0) {
		LOG_ERROR(
		    "apply_gpu_optimisations set to value other than \"accept-responsibility\" (%s), will "
		    "not apply GPU optimisations!\n",
		    apply);
		return -1;
	}

	/* Create the context */
	GameModeGPUInfo *new_info = malloc(sizeof(GameModeGPUInfo));
	memset(new_info, 0, sizeof(GameModeGPUInfo));

	/* Get the config parameters */
	config_get_gpu_vendor(config, &new_info->vendor);
	config_get_gpu_device(config, &new_info->device);

	/* TODO: Detect the GPU vendor and device automatically when these aren't set */

	/* verify device ID */
	if (new_info->device == -1) {
		LOG_ERROR(
		    "ERROR: Invalid gpu_device value set in configuration, will not apply "
		    "optimisations!\n");
		free(new_info);
		return -1;
	}

	/* verify GPU vendor */
	if (!GPUVendorValid(new_info->vendor)) {
		LOG_ERROR(
		    "ERROR: Invalid gpu_vendor value (0x%04x) set in configuration, will not apply "
		    "optimisations!\n",
		    (unsigned int)new_info->vendor);
		LOG_ERROR("Possible values are: 0x%04x (NVIDIA) 0x%04x (AMD) 0x%04x (Intel)\n",
		          Vendor_NVIDIA,
		          Vendor_AMD,
		          Vendor_Intel);
		free(new_info);
		return -1;
	}

	/* Load the config based on GPU and also verify the values are sane */
	switch (new_info->vendor) {
	case Vendor_NVIDIA:
		config_get_nv_core_clock_mhz_offset(config, &new_info->core);
		config_get_nv_mem_clock_mhz_offset(config, &new_info->mem);

		/* Reject values over some guessed values
		 * If a user wants to go into very unsafe levels they can recompile
		 */
		const int nv_core_hard_limit = 200;
		const int nv_mem_hard_limit = 2000;
		if (new_info->core > nv_core_hard_limit || new_info->mem > nv_mem_hard_limit) {
			LOG_ERROR(
			    "ERROR NVIDIA Overclock value above safety levels of +%d (core) +%d (mem), will "
			    "not overclock!\n",
			    nv_core_hard_limit,
			    nv_mem_hard_limit);
			LOG_ERROR("nv_core_clock_mhz_offset:%ld nv_mem_clock_mhz_offset:%ld\n",
			          new_info->core,
			          new_info->mem);
			free(new_info);
			return -1;
		}

		break;
	case Vendor_AMD:
		config_get_amd_core_clock_percentage(config, &new_info->core);
		config_get_amd_mem_clock_percentage(config, &new_info->mem);

		/* Reject values over 25%
		 * If a user wants to go into very unsafe levels they can recompile
		 */
		const int amd_hard_limit = 25;
		if (new_info->core > amd_hard_limit || new_info->mem > amd_hard_limit) {
			LOG_ERROR("ERROR AMD Overclock value above safety level of %d%%, will not overclock!\n",
			          amd_hard_limit);
			LOG_ERROR("amd_core_clock_percentage:%ld amd_mem_clock_percentage:%ld\n",
			          new_info->core,
			          new_info->mem);
			free(new_info);
			return -1;
		}
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

//#include <linux/limits.h>
//#include <stdio.h>
//#include <sys/wait.h>
//#include <unistd.h>

/**
 * Applies GPU optimisations when gamemode is active and removes them after
 */
int game_mode_apply_gpu(const GameModeGPUInfo *info, bool apply)
{
	// Null info means don't apply anything
	if (!info)
		return 0;

	LOG_MSG("Requesting GPU optimisations on device:%ld with settings core:%ld clock:%ld\n",
	        info->device,
	        info->core,
	        info->mem);

	/* Generate the input strings */
	char vendor[7];
	snprintf(vendor, 7, "0x%04x", (short)info->vendor);
	char device[4];
	snprintf(device, 4, "%ld", info->device);
	char core[8];
	snprintf(core, 8, "%ld", info->core);
	char mem[8];
	snprintf(mem, 8, "%ld", info->mem);

	// TODO: Actually pass right arguments
	const char *const exec_args[] = {
		"/usr/bin/pkexec",
		LIBEXECDIR "/gpuclockctl",
		vendor,
		device,
		"set",
		apply ? core : "0", /* For now simply reset to zero */
		apply ? mem : "0",  /* could in the future store default values for reset */
		NULL,
	};

	if (run_external_process(exec_args) != 0) {
		LOG_ERROR("ERROR: Failed to call gpuclockctl, could not apply optimisations!\n");
		return -1;
	}

	return 0;
}
