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

#include "external-helper.h"
#include "gpu-control.h"

/* NV constants */
#define NV_CORE_OFFSET_ATTRIBUTE "GPUGraphicsClockOffset"
#define NV_MEM_OFFSET_ATTRIBUTE "GPUMemoryTransferRateOffset"
#define NV_POWERMIZER_MODE_ATTRIBUTE "GPUPowerMizerMode"
#define NV_ATTRIBUTE_FORMAT "[gpu:%ld]/%s"
#define NV_PERF_LEVEL_FORMAT "[%ld]"

/* AMD constants */
#define AMD_DRM_PATH "/sys/class/drm/card%ld/device/%s"

/* Plausible extras to add:
 * Intel support - https://blog.ffwll.ch/2013/03/overclocking-your-intel-gpu-on-linux.html
 * AMD - Allow setting fan speed as well
 * Store baseline values with get_gpu_state to apply when leaving gamemode
 */

/* Helper to quit with usage */
static const char *usage_text =
    "usage: gpuclockctl PCI_ID DEVICE [get] [set CORE MEM [PERF_LEVEL]]]";
static void print_usage_and_exit(void)
{
	fprintf(stderr, "%s\n", usage_text);
	exit(EXIT_FAILURE);
}

static int get_gpu_state_nv(struct GameModeGPUInfo *info)
{
	if (info->vendor != Vendor_NVIDIA)
		return -1;

	if (!getenv("DISPLAY"))
		LOG_ERROR("Getting Nvidia parameters requires DISPLAY to be set - will likely fail!\n");

	char arg[128] = { 0 };
	char buf[EXTERNAL_BUFFER_MAX] = { 0 };
	char *end;

	/* Get the GPUGraphicsClockOffset parameter */
	snprintf(arg,
	         128,
	         NV_ATTRIBUTE_FORMAT NV_PERF_LEVEL_FORMAT,
	         info->device,
	         NV_CORE_OFFSET_ATTRIBUTE,
	         info->nv_perf_level);
	const char *exec_args_core[] = { "/usr/bin/nvidia-settings", "-q", arg, "-t", NULL };
	if (run_external_process(exec_args_core, buf, -1) != 0) {
		LOG_ERROR("Failed to get %s!\n", arg);
		return -1;
	}

	info->nv_core = strtol(buf, &end, 10);
	if (end == buf) {
		LOG_ERROR("Failed to parse output for \"%s\" output was \"%s\"!\n", arg, buf);
		return -1;
	}

	/* Get the GPUMemoryTransferRateOffset parameter */
	snprintf(arg,
	         128,
	         NV_ATTRIBUTE_FORMAT NV_PERF_LEVEL_FORMAT,
	         info->device,
	         NV_MEM_OFFSET_ATTRIBUTE,
	         info->nv_perf_level);
	const char *exec_args_mem[] = { "/usr/bin/nvidia-settings", "-q", arg, "-t", NULL };
	if (run_external_process(exec_args_mem, buf, -1) != 0) {
		LOG_ERROR("Failed to get %s!\n", arg);
		return -1;
	}

	info->nv_mem = strtol(buf, &end, 10);
	if (end == buf) {
		LOG_ERROR("Failed to parse output for \"%s\" output was \"%s\"!\n", arg, buf);
		return -1;
	}

	/* Get the GPUPowerMizerMode parameter */
	snprintf(arg, 128, NV_ATTRIBUTE_FORMAT, info->device, NV_POWERMIZER_MODE_ATTRIBUTE);
	const char *exec_args_pm[] = { "/usr/bin/nvidia-settings", "-q", arg, "-t", NULL };
	if (run_external_process(exec_args_pm, buf, -1) != 0) {
		LOG_ERROR("Failed to get %s!\n", arg);
		return -1;
	}

	info->nv_powermizer_mode = strtol(buf, &end, 10);
	if (end == buf) {
		LOG_ERROR("Failed to parse output for \"%s\" output was \"%s\"!\n", arg, buf);
		return -1;
	}

	return 0;
}

/**
 * Set the gpu state based on input parameters on Nvidia
 */
static int set_gpu_state_nv(struct GameModeGPUInfo *info)
{
	if (info->vendor != Vendor_NVIDIA)
		return -1;

	if (!getenv("DISPLAY") || !getenv("XAUTHORITY"))
		LOG_ERROR(
		    "Setting Nvidia parameters requires DISPLAY and XAUTHORITY to be set - will likely "
		    "fail!\n");

	char arg[128] = { 0 };

	/* Set the GPUGraphicsClockOffset parameter */
	snprintf(arg,
	         128,
	         NV_ATTRIBUTE_FORMAT NV_PERF_LEVEL_FORMAT "=%ld",
	         info->device,
	         NV_CORE_OFFSET_ATTRIBUTE,
	         info->nv_perf_level,
	         info->nv_core);
	const char *exec_args_core[] = { "/usr/bin/nvidia-settings", "-a", arg, NULL };
	if (run_external_process(exec_args_core, NULL, -1) != 0) {
		LOG_ERROR("Failed to set %s!\n", arg);
		return -1;
	}

	/* Set the GPUMemoryTransferRateOffset parameter */
	snprintf(arg,
	         128,
	         NV_ATTRIBUTE_FORMAT NV_PERF_LEVEL_FORMAT "=%ld",
	         info->device,
	         NV_MEM_OFFSET_ATTRIBUTE,
	         info->nv_perf_level,
	         info->nv_mem);
	const char *exec_args_mem[] = { "/usr/bin/nvidia-settings", "-a", arg, NULL };
	if (run_external_process(exec_args_mem, NULL, -1) != 0) {
		LOG_ERROR("Failed to set %s!\n", arg);
		return -1;
	}

	/* Set the GPUPowerMizerMode parameter if requested */
	if (info->nv_powermizer_mode == -1)
		return 0;

	snprintf(arg,
	         128,
	         NV_ATTRIBUTE_FORMAT "=%ld",
	         info->device,
	         NV_POWERMIZER_MODE_ATTRIBUTE,
	         info->nv_powermizer_mode);
	const char *exec_args_pm[] = { "/usr/bin/nvidia-settings", "-a", arg, NULL };
	if (run_external_process(exec_args_pm, NULL, -1) != 0) {
		LOG_ERROR("Failed to set %s!\n", arg);
		return -1;
	}

	return 0;
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
	char path[64];
	snprintf(path, 64, AMD_DRM_PATH, info->device, "power_dpm_force_performance_level");

	FILE *file = fopen(path, "r");
	if (!file) {
		LOG_ERROR("Could not open %s for read (%s)!\n", path, strerror(errno));
		return -1;
	}

	char buff[CONFIG_VALUE_MAX];
	if (!fgets(buff, CONFIG_VALUE_MAX, file)) {
		LOG_ERROR("Could not read file %s (%s)!\n", path, strerror(errno));
		return -1;
	}

	if (fclose(file) != 0) {
		LOG_ERROR("Could not close %s after reading (%s)!\n", path, strerror(errno));
		return -1;
	}

	/* Copy in the value from the file */
	strncpy(info->amd_performance_level, buff, CONFIG_VALUE_MAX);

	return info == NULL;
}

/*
 * Simply set an amd drm file to a value
 */
static int set_gpu_state_amd_file(const char *filename, long device, const char *value)
{
	char path[64];
	snprintf(path, 64, AMD_DRM_PATH, device, filename);

	FILE *file = fopen(path, "w");
	if (!file) {
		LOG_ERROR("Could not open %s for write (%s)!\n", path, strerror(errno));
		return -1;
	}

	if (fprintf(file, "%s", value) < 0) {
		LOG_ERROR("Could not write to %s (%s)!\n", path, strerror(errno));
		return -1;
	}

	if (fclose(file) != 0) {
		LOG_ERROR("Could not close %s after writing (%s)!\n", path, strerror(errno));
		return -1;
	}

	return 0;
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

/* Helper to get and verify vendor value */
static long get_vendor(const char *val)
{
	char *end;
	long ret = strtol(val, &end, 0);
	if (!GPUVendorValid(ret) || end == val) {
		LOG_ERROR("Invalid GPU Vendor passed (0x%04x)!\n", (unsigned short)ret);
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
	if (ret < 0 || end == val) {
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
	if (argc >= 4 && strncmp(argv[3], "get", 3) == 0) {
		/* Get and verify the vendor and device */
		struct GameModeGPUInfo info;
		memset(&info, 0, sizeof(info));
		info.vendor = get_vendor(argv[1]);
		info.device = get_device(argv[2]);

		if (info.vendor == Vendor_NVIDIA && argc > 4)
			info.nv_perf_level = get_generic_value(argv[4]);

		/* Fetch the state and print it out */
		switch (info.vendor) {
		case Vendor_NVIDIA:
			/* Get nvidia power level */
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
			printf("Currently unsupported GPU vendor 0x%04x, doing nothing!\n", (short)info.vendor);
			break;
		}

	} else if (argc >= 5 && argc <= 8 && strncmp(argv[3], "set", 3) == 0) {
		/* Get and verify the vendor and device */
		struct GameModeGPUInfo info;
		memset(&info, 0, sizeof(info));
		info.vendor = get_vendor(argv[1]);
		info.device = get_device(argv[2]);

		switch (info.vendor) {
		case Vendor_NVIDIA:
			info.nv_core = get_generic_value(argv[4]);
			info.nv_mem = get_generic_value(argv[5]);
			if (argc > 6)
				info.nv_perf_level = get_generic_value(argv[6]);
			if (argc > 7)
				info.nv_powermizer_mode = get_generic_value(argv[7]);
			else
				info.nv_powermizer_mode = -1;
			break;
		case Vendor_AMD:
			strncpy(info.amd_performance_level, argv[4], CONFIG_VALUE_MAX);
			break;
		default:
			printf("Currently unsupported GPU vendor 0x%04x, doing nothing!\n", (short)info.vendor);
			break;
		}

		printf("gpuclockctl setting values on device:%ld with vendor 0x%04x",
		       info.device,
		       (unsigned short)info.vendor);

		switch (info.vendor) {
		case Vendor_NVIDIA:
			return set_gpu_state_nv(&info);
		case Vendor_AMD:
			return set_gpu_state_amd(&info);
		default:
			printf("Currently unsupported GPU vendor 0x%04x, doing nothing!\n", (short)info.vendor);
			break;
		}
	} else {
		print_usage_and_exit();
	}

	return EXIT_SUCCESS;
}
