
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
#include "helpers.h"
#include "logging.h"

#include "gamemode.h"

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
	new_info->device = config_get_gpu_device(config);

	/* verify device ID */
	if (new_info->device == -1) {
		LOG_ERROR(
		    "Invalid gpu_device value set in configuration, will not apply "
		    "optimisations!\n");
		free(new_info);
		return -1;
	}

	/* Fill in GPU vendor */
	char path[64] = { 0 };
	if (snprintf(path, 64, "/sys/class/drm/card%ld/device/vendor", new_info->device) < 0) {
		LOG_ERROR("snprintf failed, will not apply gpu optimisations!\n");
		return -1;
	}
	FILE *vendor = fopen(path, "r");
	if (!vendor) {
		LOG_ERROR("Couldn't open vendor file at %s, will not apply gpu optimisations!\n", path);
		return -1;
	}
	char buff[64];
	if (fgets(buff, 64, vendor) != NULL) {
		new_info->vendor = strtol(buff, NULL, 0);
	} else {
		LOG_ERROR("Coudn't read contents of file %s, will not apply optimisations!\n", path);
		return -1;
	}

	/* verify GPU vendor */
	if (!GPUVendorValid(new_info->vendor)) {
		LOG_ERROR("Unknown vendor value (0x%04x) found, cannot apply optimisations!\n",
		          (unsigned int)new_info->vendor);
		LOG_ERROR("Known values are: 0x%04x (NVIDIA) 0x%04x (AMD) 0x%04x (Intel)\n",
		          Vendor_NVIDIA,
		          Vendor_AMD,
		          Vendor_Intel);
		free(new_info);
		return -1;
	}

	/* Load the config based on GPU and also verify the values are sane */
	switch (new_info->vendor) {
	case Vendor_NVIDIA:
		new_info->nv_core = config_get_nv_core_clock_mhz_offset(config);
		new_info->nv_mem = config_get_nv_mem_clock_mhz_offset(config);

		/* Reject values over some guessed values
		 * If a user wants to go into very unsafe levels they can recompile
		 */
		const int nv_core_hard_limit = 200;
		const int nv_mem_hard_limit = 2000;
		if (new_info->nv_core > nv_core_hard_limit || new_info->nv_mem > nv_mem_hard_limit) {
			LOG_ERROR(
			    "NVIDIA Overclock value above safety levels of +%d (core) +%d (mem), will "
			    "not overclock!\n",
			    nv_core_hard_limit,
			    nv_mem_hard_limit);
			LOG_ERROR("nv_core_clock_mhz_offset:%ld nv_mem_clock_mhz_offset:%ld\n",
			          new_info->nv_core,
			          new_info->nv_mem);
			free(new_info);
			return -1;
		}

		/* Sanity check the performance level value as well */
		new_info->nv_perf_level = config_get_nv_perf_level(config);
		if (new_info->nv_perf_level < 0 || new_info->nv_perf_level > 16) {
			LOG_ERROR(
			    "NVIDIA Performance level value likely invalid (%ld), will not apply "
			    "optimisations!\n",
			    new_info->nv_perf_level);
			free(new_info);
			return -1;
		}

		break;
	case Vendor_AMD:
		new_info->nv_core = config_get_amd_core_clock_percentage(config);
		new_info->nv_mem = config_get_amd_mem_clock_percentage(config);

		/* Reject values over 20%
		 * If a user wants to go into very unsafe levels they can recompile
		 * As far as I can tell the driver doesn't allow values over 20 anyway
		 */
		const int amd_hard_limit = 20;
		if (new_info->nv_core > amd_hard_limit || new_info->nv_mem > amd_hard_limit) {
			LOG_ERROR("AMD Overclock value above safety level of %d%%, will not overclock!\n",
			          amd_hard_limit);
			LOG_ERROR("amd_core_clock_percentage:%ld amd_mem_clock_percentage:%ld\n",
			          new_info->nv_core,
			          new_info->nv_mem);
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

	LOG_MSG("Requesting GPU optimisations on device:%ld with settings nv_core:%ld clock:%ld\n",
	        info->device,
	        info->nv_core,
	        info->nv_mem);

	/* Generate the input strings */
	char vendor[7];
	snprintf(vendor, 7, "0x%04x", (short)info->vendor);
	char device[4];
	snprintf(device, 4, "%ld", info->device);
	char nv_core[8];
	snprintf(nv_core, 8, "%ld", info->nv_core);
	char nv_mem[8];
	snprintf(nv_mem, 8, "%ld", info->nv_mem);
	char nv_perf_level[4];
	snprintf(nv_perf_level, 4, "%ld", info->nv_perf_level);

	// Set up our command line to pass to gpuclockctl
	const char *const exec_args[] = {
		"/usr/bin/pkexec",
		LIBEXECDIR "/gpuclockctl",
		vendor,
		device,
		"set",
		apply ? nv_core : "0",
		apply ? nv_mem : "0",
		info->vendor == Vendor_NVIDIA ? nv_perf_level : NULL, /* Only use this if Nvidia */
		NULL,
	};

	if (run_external_process(exec_args, NULL, -1) != 0) {
		LOG_ERROR("Failed to call gpuclockctl, could not apply optimisations!\n");
		return -1;
	}

	return 0;
}

int game_mode_get_gpu(GameModeGPUInfo *info)
{
	if (!info)
		return 0;

	/* Generate the input strings */
	char vendor[7];
	snprintf(vendor, 7, "0x%04x", (short)info->vendor);
	char device[4];
	snprintf(device, 4, "%ld", info->device);
	char nv_perf_level[4];
	snprintf(nv_perf_level, 4, "%ld", info->nv_perf_level);

	// Set up our command line to pass to gpuclockctl
	// This doesn't need pkexec as get does not need elevated perms
	const char *const exec_args[] = {
		LIBEXECDIR "/gpuclockctl",
		vendor,
		device,
		"get",
		info->vendor == Vendor_NVIDIA ? nv_perf_level : NULL, /* Only use this if Nvidia */
		NULL,
	};

	char buffer[EXTERNAL_BUFFER_MAX] = { 0 };
	if (run_external_process(exec_args, buffer, -1) != 0) {
		LOG_ERROR("Failed to call gpuclockctl, could get values!\n");
		return -1;
	}

	int nv_core = 0;
	int nv_mem = 0;
	if (sscanf(buffer, "%i %i", &nv_core, &nv_mem) != 2) {
		LOG_ERROR("Failed to parse gpuclockctl output: %s\n", buffer);
		return -1;
	}

	info->nv_core = nv_core;
	info->nv_mem = nv_mem;

	return 0;
}
