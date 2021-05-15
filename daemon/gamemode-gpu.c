
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

#include "common-external.h"
#include "common-gpu.h"
#include "common-helpers.h"
#include "common-logging.h"

#include "gamemode.h"
#include "gamemode-config.h"

#include "build-config.h"

_Static_assert(CONFIG_VALUE_MAX == GPU_VALUE_MAX, "Config max value and GPU value out of sync!");

/**
 * Attempts to identify the current in use GPU information
 */
int game_mode_initialise_gpu(GameModeConfig *config, GameModeGPUInfo **info)
{
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
	new_info->vendor = gamemode_get_gpu_vendor(new_info->device);
	if (!GPUVendorValid(new_info->vendor)) {
		LOG_ERROR("Found invalid vendor, will not apply optimisations!\n");
		free(new_info);
		return -1;
	}

	/* Load the config based on GPU and also verify the values are sane */
	switch (new_info->vendor) {
	case Vendor_NVIDIA:
		new_info->nv_core = config_get_nv_core_clock_mhz_offset(config);
		new_info->nv_mem = config_get_nv_mem_clock_mhz_offset(config);
		new_info->nv_powermizer_mode = config_get_nv_powermizer_mode(config);

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

		break;
	case Vendor_AMD:
		config_get_amd_performance_level(config, new_info->amd_performance_level);

		/* Error about unsupported "manual" option, for now */
		if (strcmp(new_info->amd_performance_level, "manual") == 0) {
			LOG_ERROR("AMD Performance level set to \"manual\", this is currently unsupported");
			free(new_info);
			return -1;
		}
		break;
	default:
		break;
	}

	/* Give back the new gpu info */
	*info = new_info;
	return 0;
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
int game_mode_apply_gpu(const GameModeGPUInfo *info)
{
	// Null info means don't apply anything
	if (!info)
		return 0;

	LOG_MSG("Requesting GPU optimisations on device:%ld\n", info->device);

	/* Generate the input strings */
	char device[4];
	snprintf(device, 4, "%ld", info->device);

	char nv_core[8];
	snprintf(nv_core, 8, "%ld", info->nv_core);
	char nv_mem[8];
	snprintf(nv_mem, 8, "%ld", info->nv_mem);
	char nv_powermizer_mode[4];
	snprintf(nv_powermizer_mode, 4, "%ld", info->nv_powermizer_mode);

	// Set up our command line to pass to gpuclockctl
	const char *const exec_args[] = {
		"pkexec",
		LIBEXECDIR "/gpuclockctl",
		device,
		"set",
		info->vendor == Vendor_NVIDIA ? nv_core : info->amd_performance_level,
		info->vendor == Vendor_NVIDIA ? nv_mem : NULL,             /* Only use this if Nvidia */
		info->vendor == Vendor_NVIDIA ? nv_powermizer_mode : NULL, /* Only use this if Nvidia */
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
	char device[4];
	snprintf(device, 4, "%ld", info->device);

	// Set up our command line to pass to gpuclockctl
	// This doesn't need pkexec as get does not need elevated perms
	const char *const exec_args[] = {
		LIBEXECDIR "/gpuclockctl",
		device,
		"get",
		NULL,
	};

	char buffer[EXTERNAL_BUFFER_MAX] = { 0 };
	if (run_external_process(exec_args, buffer, -1) != 0) {
		LOG_ERROR("Failed to call gpuclockctl, could not get values!\n");
		return -1;
	}
	strtok(buffer, "\n");

	switch (info->vendor) {
	case Vendor_NVIDIA:
		if (sscanf(buffer,
		           "%ld %ld %ld",
		           &info->nv_core,
		           &info->nv_mem,
		           &info->nv_powermizer_mode) != 3) {
			LOG_ERROR("Failed to parse gpuclockctl output: %s\n", buffer);
			return -1;
		}
		break;
	case Vendor_AMD:
		strncpy(info->amd_performance_level, buffer, sizeof(info->amd_performance_level) - 1);
		info->amd_performance_level[sizeof(info->amd_performance_level) - 1] = '\0';
		break;
	}

	return 0;
}
