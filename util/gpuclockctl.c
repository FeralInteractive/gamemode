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
#include "common-logging.h"

#include <limits.h>
#include <unistd.h>

/* NV constants */
#define NV_CORE_OFFSET_ATTRIBUTE "GPUGraphicsClockOffset"
#define NV_MEM_OFFSET_ATTRIBUTE "GPUMemoryTransferRateOffset"
#define NV_POWERMIZER_MODE_ATTRIBUTE "GPUPowerMizerMode"
#define NV_PERFMODES_ATTRIBUTE "GPUPerfModes"
#define NV_PCIDEVICE_ATTRIBUTE "PCIDevice"
#define NV_ATTRIBUTE_FORMAT "[gpu:%ld]/%s"
#define NV_PERF_LEVEL_FORMAT "[%ld]"
#define NV_ARG_MAX 128

/* AMD constants */
#define AMD_DRM_PATH "/sys/class/drm/card%ld/device/%s"

/* Plausible extras to add:
 * Intel support - https://blog.ffwll.ch/2013/03/overclocking-your-intel-gpu-on-linux.html
 * AMD - Allow setting fan speed as well
 * Store baseline values with get_gpu_state to apply when leaving gamemode
 */

/* Helper to quit with usage */
static const char *usage_text =
    "usage: gpuclockctl DEVICE {arg}\n\t\tget - return current values\n\t\tset [NV_CORE NV_MEM "
    "NV_POWERMIZER_MODE | AMD_PERFORMANCE_LEVEL] - set current values";
static void print_usage_and_exit(void)
{
	fprintf(stderr, "%s\n", usage_text);
	exit(EXIT_FAILURE);
}

static const char *get_nv_attr(const char *attr)
{
	static char out[EXTERNAL_BUFFER_MAX];
	const char *exec_args[] = { "nvidia-settings", "-q", attr, "-t", NULL };
	if (run_external_process(exec_args, out, -1) != 0) {
		LOG_ERROR("Failed to get %s!\n", attr);
		out[0] = 0;
		return NULL;
	}

	return &out[0];
}

static int set_nv_attr(const char *attr)
{
	const char *exec_args_core[] = { "nvidia-settings", "-a", attr, NULL };
	if (run_external_process(exec_args_core, NULL, -1) != 0) {
		LOG_ERROR("Failed to set %s!\n", attr);
		return -1;
	}

	return 0;
}

/* Get the nvidia driver index for the current GPU */
static long get_gpu_index_id_nv(struct GameModeGPUInfo *info)
{
	if (info->vendor != Vendor_NVIDIA)
		return -1;

	/* NOTE: This is currently based off of a best guess of how the NVidia gpu index works
	 * ie. that the index is simply the index into available NV gpus in the same order as drm
	 * If that is not the case then this may fail to discern the correct GPU
	 */

	int device = 0;
	int nv_device = -1;
	while (device <= info->device) {
		/* Get the vendor for each gpu sequentially */
		enum GPUVendor vendor = gamemode_get_gpu_vendor(device++);

		switch (vendor) {
		case Vendor_NVIDIA:
			/* If we've found an nvidia device, increment our counter */
			nv_device++;
			break;
		case Vendor_Invalid:
			/* Bail out, we've gone too far */
			LOG_ERROR("Failed to find Nvidia GPU with expected index!\n");
			break;
		default:
			/* Non-NV gpu, continue */
			break;
		}
	};

	return nv_device;
}

/* Get the max nvidia perf level */
static long get_max_perf_level_nv(struct GameModeGPUInfo *info)
{
	if (info->vendor != Vendor_NVIDIA)
		return -1;

	if (!getenv("DISPLAY"))
		LOG_ERROR("Getting Nvidia parameters requires DISPLAY to be set - will likely fail!\n");

	char arg[NV_ARG_MAX] = { 0 };
	const char *attr;

	snprintf(arg, NV_ARG_MAX, NV_ATTRIBUTE_FORMAT, info->device, NV_PERFMODES_ATTRIBUTE);
	if ((attr = get_nv_attr(arg)) == NULL) {
		return -1;
	}

	char *ptr = strrchr(attr, ';');
	long level = -1;
	if (!ptr || sscanf(ptr, "; perf=%ld", &level) != 1) {
		LOG_ERROR(
		    "Output didn't match expected format, couldn't discern highest perf level from "
		    "nvidia-settings!\n");
		LOG_ERROR("Output:%s\n", attr);
		return -1;
	}

	return level;
}

/* Get the nvidia gpu state */
static int get_gpu_state_nv(struct GameModeGPUInfo *info)
{
	if (info->vendor != Vendor_NVIDIA)
		return -1;

	if (!getenv("DISPLAY"))
		LOG_ERROR("Getting Nvidia parameters requires DISPLAY to be set - will likely fail!\n");

	long perf_level = get_max_perf_level_nv(info);

	char arg[NV_ARG_MAX] = { 0 };
	const char *attr;
	char *end;

	/* Get the GPUGraphicsClockOffset parameter */
	snprintf(arg,
	         NV_ARG_MAX,
	         NV_ATTRIBUTE_FORMAT NV_PERF_LEVEL_FORMAT,
	         info->device,
	         NV_CORE_OFFSET_ATTRIBUTE,
	         perf_level);
	if ((attr = get_nv_attr(arg)) == NULL) {
		return -1;
	}

	info->nv_core = strtol(attr, &end, 10);
	if (end == attr) {
		LOG_ERROR("Failed to parse output for \"%s\" output was \"%s\"!\n", arg, attr);
		return -1;
	}

	/* Get the GPUMemoryTransferRateOffset parameter */
	snprintf(arg,
	         NV_ARG_MAX,
	         NV_ATTRIBUTE_FORMAT NV_PERF_LEVEL_FORMAT,
	         info->device,
	         NV_MEM_OFFSET_ATTRIBUTE,
	         perf_level);
	if ((attr = get_nv_attr(arg)) == NULL) {
		return -1;
	}

	info->nv_mem = strtol(attr, &end, 10);
	if (end == attr) {
		LOG_ERROR("Failed to parse output for \"%s\" output was \"%s\"!\n", arg, attr);
		return -1;
	}

	/* Get the GPUPowerMizerMode parameter */
	snprintf(arg, NV_ARG_MAX, NV_ATTRIBUTE_FORMAT, info->device, NV_POWERMIZER_MODE_ATTRIBUTE);
	if ((attr = get_nv_attr(arg)) == NULL) {
		return -1;
	}

	info->nv_powermizer_mode = strtol(attr, &end, 10);
	if (end == attr) {
		LOG_ERROR("Failed to parse output for \"%s\" output was \"%s\"!\n", arg, attr);
		return -1;
	}

	return 0;
}

/**
 * Set the gpu state based on input parameters on Nvidia
 */
static int set_gpu_state_nv(struct GameModeGPUInfo *info)
{
	int status = 0;

	if (info->vendor != Vendor_NVIDIA)
		return -1;

	if (!getenv("DISPLAY") || !getenv("XAUTHORITY"))
		LOG_ERROR(
		    "Setting Nvidia parameters requires DISPLAY and XAUTHORITY to be set - will likely "
		    "fail!\n");

	long perf_level = get_max_perf_level_nv(info);

	char arg[NV_ARG_MAX] = { 0 };

	/* Set the GPUGraphicsClockOffset parameter */
	if (info->nv_core != -1) {
		snprintf(arg,
		         NV_ARG_MAX,
		         NV_ATTRIBUTE_FORMAT NV_PERF_LEVEL_FORMAT "=%ld",
		         info->device,
		         NV_CORE_OFFSET_ATTRIBUTE,
		         perf_level,
		         info->nv_core);
		if (set_nv_attr(arg) != 0) {
			status = -1;
		}
	}

	/* Set the GPUMemoryTransferRateOffset parameter */
	if (info->nv_mem != -1) {
		snprintf(arg,
		         NV_ARG_MAX,
		         NV_ATTRIBUTE_FORMAT NV_PERF_LEVEL_FORMAT "=%ld",
		         info->device,
		         NV_MEM_OFFSET_ATTRIBUTE,
		         perf_level,
		         info->nv_mem);
		if (set_nv_attr(arg) != 0) {
			status = -1;
		}
	}

	/* Set the GPUPowerMizerMode parameter if requested */
	if (info->nv_powermizer_mode != -1) {
		snprintf(arg,
		         NV_ARG_MAX,
		         NV_ATTRIBUTE_FORMAT "=%ld",
		         info->device,
		         NV_POWERMIZER_MODE_ATTRIBUTE,
		         info->nv_powermizer_mode);
		if (set_nv_attr(arg) != 0) {
			status = -1;
		}
	}

	return status;
}

/**
 * Get the gpu state
 * Populates the struct with the GPU info on the system
 */
static int get_gpu_state_amd(struct GameModeGPUInfo *info)
{
	if (info->vendor != Vendor_AMD)
		return -1;

	/* Get the contents of power_dpm_force_performance_level */
	char path[PATH_MAX];
	snprintf(path, PATH_MAX, AMD_DRM_PATH, info->device, "power_dpm_force_performance_level");

	FILE *file = fopen(path, "r");
	if (!file) {
		LOG_ERROR("Could not open %s for read (%s)!\n", path, strerror(errno));
		return -1;
	}

	int ret = 0;

	char buff[GPU_VALUE_MAX] = { 0 };
	if (!fgets(buff, GPU_VALUE_MAX, file)) {
		LOG_ERROR("Could not read file %s (%s)!\n", path, strerror(errno));
		ret = -1;
	}

	if (fclose(file) != 0) {
		LOG_ERROR("Could not close %s after reading (%s)!\n", path, strerror(errno));
		ret = -1;
	}

	if (ret == 0) {
		/* Copy in the value from the file */
		strncpy(info->amd_performance_level, buff, GPU_VALUE_MAX - 1);
		info->amd_performance_level[GPU_VALUE_MAX - 1] = '\0';
	}

	return ret;
}

/*
 * Simply set an amd drm file to a value
 */
static int set_gpu_state_amd_file(const char *filename, long device, const char *value)
{
	char path[PATH_MAX];
	snprintf(path, PATH_MAX, AMD_DRM_PATH, device, filename);

	FILE *file = fopen(path, "w");
	if (!file) {
		LOG_ERROR("Could not open %s for write (%s)!\n", path, strerror(errno));
		return -1;
	}

	int ret = 0;

	if (fprintf(file, "%s", value) < 0) {
		LOG_ERROR("Could not write to %s (%s)!\n", path, strerror(errno));
		ret = -1;
	}

	if (fclose(file) != 0) {
		LOG_ERROR("Could not close %s after writing (%s)!\n", path, strerror(errno));
		ret = -1;
	}

	return ret;
}

/**
 * Set the gpu state based on input parameters on amd
 */
static int set_gpu_state_amd(struct GameModeGPUInfo *info)
{
	if (info->vendor != Vendor_AMD)
		return -1;

	/* Must be root to set the state */
	if (geteuid() != 0) {
		fprintf(stderr, "gpuclockctl must be run as root to set AMD values\n");
		print_usage_and_exit();
	}

	/* First set the amd_performance_level to the chosen setting */
	if (set_gpu_state_amd_file("power_dpm_force_performance_level",
	                           info->device,
	                           info->amd_performance_level) != 0)
		return -1;

	/* TODO: If amd_performance_level is set to "manual" we need to adjust pp_table and/or
	   pp_od_clk_voltage see
	   https://dri.freedesktop.org/docs/drm/gpu/amdgpu.html#gpu-power-thermal-controls-and-monitoring
	*/

	return 0;
}

/* Helper to get and verify device value */
static long get_device(const char *val)
{
	char *end;
	long ret = strtol(val, &end, 10);
	if (ret < 0 || end == val) {
		LOG_ERROR("Invalid GPU device passed (%ld)!\n", ret);
		print_usage_and_exit();
	}
	return ret;
}

/* Helper to get and verify nv_core and nv_mem value */
static long get_generic_value(const char *val)
{
	char *end;
	long ret = strtol(val, &end, 10);
	if (end == val) {
		LOG_ERROR("Invalid value passed (%ld)!\n", ret);
		print_usage_and_exit();
	}
	return ret;
}

/**
 * Main entry point, dispatch to the appropriate helper
 */
int main(int argc, char *argv[])
{
	struct GameModeGPUInfo info;
	memset(&info, 0, sizeof(info));

	if (argc == 3 && strncmp(argv[2], "get", 3) == 0) {
		/* Get and verify the vendor and device */
		info.device = get_device(argv[1]);
		info.vendor = gamemode_get_gpu_vendor(info.device);

		/* Fetch the state and print it out */
		switch (info.vendor) {
		case Vendor_NVIDIA:
			/* Adjust the device number to the gpu index for Nvidia */
			info.device = get_gpu_index_id_nv(&info);

			if (get_gpu_state_nv(&info) != 0)
				exit(EXIT_FAILURE);
			printf("%ld %ld %ld\n", info.nv_core, info.nv_mem, info.nv_powermizer_mode);
			break;
		case Vendor_AMD:
			if (get_gpu_state_amd(&info) != 0)
				exit(EXIT_FAILURE);
			printf("%s\n", info.amd_performance_level);
			break;
		default:
			LOG_ERROR("Currently unsupported GPU vendor 0x%04x, doing nothing!\n",
			          (unsigned short)info.vendor);
			break;
		}

	} else if (argc >= 4 && argc <= 7 && strncmp(argv[2], "set", 3) == 0) {
		/* Get and verify the vendor and device */
		info.device = get_device(argv[1]);
		info.vendor = gamemode_get_gpu_vendor(info.device);

		switch (info.vendor) {
		case Vendor_NVIDIA:
			if (argc < 4) {
				LOG_ERROR("Must pass at least 4 arguments for nvidia gpu!\n");
				print_usage_and_exit();
			}
			info.nv_core = get_generic_value(argv[3]);
			info.nv_mem = get_generic_value(argv[4]);

			/* Adjust the device number to the gpu index for Nvidia */
			info.device = get_gpu_index_id_nv(&info);

			/* Optional */
			info.nv_powermizer_mode = -1;
			if (argc >= 6)
				info.nv_powermizer_mode = get_generic_value(argv[5]);

			return set_gpu_state_nv(&info);
			break;
		case Vendor_AMD:
			if (argc < 3) {
				LOG_ERROR("Must pass performance level for AMD gpu!\n");
				print_usage_and_exit();
			}
			strncpy(info.amd_performance_level, argv[3], GPU_VALUE_MAX - 1);
			return set_gpu_state_amd(&info);
			break;
		default:
			LOG_ERROR("Currently unsupported GPU vendor 0x%04x, doing nothing!\n",
			          (unsigned short)info.vendor);
			print_usage_and_exit();
			break;
		}

	} else {
		print_usage_and_exit();
	}

	return EXIT_SUCCESS;
}
