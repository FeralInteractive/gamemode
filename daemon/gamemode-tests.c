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

#include <libgen.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "daemon_config.h"
#include "gamemode_client.h"
#include "governors-query.h"
#include "gpu-control.h"

/* Initial verify step to ensure gamemode isn't already active */
static int verify_gamemode_initial(void)
{
	int status = 0;

	if ((status = gamemode_query_status()) != 0 && status != -1) {
		LOG_ERROR("gamemode is currently active, tests require gamemode to start deactivated!\n");
		status = -1;
	} else if (status == -1) {
		LOG_ERROR("gamemode_query_status failed: %s!\n", gamemode_error_string());
		LOG_ERROR("is gamemode installed correctly?\n");
		status = -1;
	} else {
		status = 0;
	}

	return status;
}

/* Check if gamemode is active and this client is registered */
static int verify_active_and_registered(void)
{
	int status = gamemode_query_status();

	if (status != 2) {
		if (status == -1) {
			LOG_ERROR("gamemode_query_status failed: %s\n", gamemode_error_string());
		} else if (status == 1) {
			LOG_ERROR("gamemode was active but did not have this process registered\n");
		}
		LOG_ERROR("gamemode failed to activate correctly when requested (expected 2)!\n");
		status = -1;
	} else {
		status = 0;
	}

	return status;
}

/* Ensure gamemode is deactivated when it should be */
static int verify_deactivated(void)
{
	int status = gamemode_query_status();

	if (status != 0) {
		if (status == -1) {
			LOG_ERROR("gamemode_query_status failed: %s\n", gamemode_error_string());
		}
		LOG_ERROR("gamemode failed to deactivate when requested (expected 0)!\n");
		status = -1;
	} else {
		status = 0;
	}

	return status;
}

/* Ensure another client is connected */
static int verify_other_client_connected(void)
{
	int status = gamemode_query_status();

	if (status != 1) {
		if (status == -1) {
			LOG_ERROR("gamemode_query_status failed: %s\n", gamemode_error_string());
		}
		LOG_ERROR("gamemode_query_status failed to return other client connected (expected 1)!\n");
		status = -1;
	} else {
		status = 0;
	}

	return status;
}

/* Run basic client tests
 * Tests a simple request_start and request_end works
 */
static int run_basic_client_tests(void)
{
	LOG_MSG(":: Basic client tests\n");

	/* First verify that gamemode is not currently active on the system
	 * As well as it being currently installed and queryable
	 */
	if (verify_gamemode_initial() != 0)
		return -1;

	/* Verify that gamemode_request_start correctly start gamemode */
	if (gamemode_request_start() != 0) {
		LOG_ERROR("gamemode_request_start failed: %s\n", gamemode_error_string());
		return -1;
	}

	/* Verify that gamemode is now active and this client is registered*/
	if (verify_active_and_registered() != 0)
		return -1;

	/* Verify that gamemode_request_end corrently de-registers gamemode */
	if (gamemode_request_end() != 0) {
		LOG_ERROR("gamemode_request_end failed: %s!\n", gamemode_error_string());
		return -1;
	}

	/* Verify that gamemode is now innactive */
	if (verify_deactivated() != 0)
		return -1;

	LOG_MSG(":: Passed\n\n");

	return 0;
}

/* Run some dual client tests
 * This also tests that the "-r" argument works correctly and cleans up correctly
 */
static int run_dual_client_tests(void)
{
	int status = 0;

	/* Try running some process interop tests */
	LOG_MSG(":: Dual client tests\n");

	/* Get the current path to this binary */
	char mypath[PATH_MAX];
	memset(mypath, 0, sizeof(mypath));
	if (readlink("/proc/self/exe", mypath, PATH_MAX) == -1) {
		LOG_ERROR("could not read current exe path: %s\n", strerror(errno));
		return -1;
	}

	/* Fork so that the child can request gamemode */
	int child = fork();
	if (child == 0) {
		/* Relaunch self with -r (request and wait for signal) */
		if (execl(mypath, mypath, "-r", (char *)NULL) == -1) {
			LOG_ERROR("failed to re-launch self (%s) with execl: %s\n", mypath, strerror(errno));
			return -1;
		}
	}

	/* Parent process */
	/* None of these should early-out as we need to clean up the child */

	/* Give the child a chance to reqeust gamemode */
	usleep(1000);

	/* Check that when we request gamemode, it replies that the other client is connected */
	if (verify_other_client_connected() != 0)
		status = -1;

	/* Verify that gamemode_request_start correctly start gamemode */
	if (gamemode_request_start() != 0) {
		LOG_ERROR("gamemode_request_start failed: %s\n", gamemode_error_string());
		status = -1;
	}

	/* Verify that gamemode is now active and this client is registered*/
	if (verify_active_and_registered() != 0)
		status = -1;

	/* Request end of gamemode (de-register ourselves) */
	if (gamemode_request_end() != 0) {
		LOG_ERROR("gamemode_request_end failed: %s!\n", gamemode_error_string());
		status = -1;
	}

	/* Check that when we request gamemode, it replies that the other client is connected */
	if (verify_other_client_connected() != 0)
		status = -1;

	/* Send SIGINT to child to wake it up*/
	if (kill(child, SIGINT) == -1) {
		LOG_ERROR("failed to send continue signal to other client: %s\n", strerror(errno));
		status = -1;
	}

	/* Give the child a chance to finish */
	usleep(100000);

	// Wait for the child to finish up
	int wstatus;
	while (waitpid(child, &wstatus, WNOHANG) == 0) {
		LOG_MSG("...Waiting for child to quit...\n");
		usleep(100000);
	}

	/* Verify that gamemode is now innactive */
	if (verify_deactivated() != 0)
		return -1;

	if (status == 0)
		LOG_MSG(":: Passed\n\n");

	return status;
}

static int run_cpu_governor_tests(struct GameModeConfig *config)
{
	/* get the two config parameters we care about */
	char desiredgov[CONFIG_VALUE_MAX] = { 0 };
	config_get_desired_governor(config, desiredgov);

	if (desiredgov[0] == '\0')
		strcpy(desiredgov, "performance");

	char defaultgov[CONFIG_VALUE_MAX] = { 0 };
	config_get_default_governor(config, defaultgov);

	if (desiredgov[0] == '\0') {
		const char *currentgov = get_gov_state();
		if (currentgov) {
			strncpy(desiredgov, currentgov, CONFIG_VALUE_MAX);
		} else {
			LOG_ERROR(
			    "Could not get current CPU governor state, this indicates an error! See rest "
			    "of log.\n");
			return -1;
		}
	}

	/* Start gamemode */
	gamemode_request_start();

	/* Verify the governor is the desired one */
	const char *currentgov = get_gov_state();
	if (strncmp(currentgov, desiredgov, CONFIG_VALUE_MAX) != 0) {
		LOG_ERROR("Govenor was not set to %s (was actually %s)!", desiredgov, currentgov);
		gamemode_request_end();
		return -1;
	}

	/* End gamemode */
	gamemode_request_end();

	/* Verify the governor has been set back */
	currentgov = get_gov_state();
	if (strncmp(currentgov, defaultgov, CONFIG_VALUE_MAX) != 0) {
		LOG_ERROR("Govenor was not set back to %s (was actually %s)!", defaultgov, currentgov);
		return -1;
	}

	return 0;
}

static int run_custom_scripts_tests(struct GameModeConfig *config)
{
	int scriptstatus = 0;

	/* Grab and test the start scripts */
	char startscripts[CONFIG_LIST_MAX][CONFIG_VALUE_MAX];
	memset(startscripts, 0, sizeof(startscripts));
	config_get_gamemode_start_scripts(config, startscripts);

	if (startscripts[0][0] != '\0') {
		int i = 0;
		while (*startscripts[i] != '\0' && i < CONFIG_LIST_MAX) {
			LOG_MSG(":::: Running start script [%s]\n", startscripts[i]);

			int ret = system(startscripts[i]);

			if (ret == 0)
				LOG_MSG(":::: Passed\n");
			else {
				LOG_MSG(":::: Failed!\n");
				scriptstatus = -1;
			}
			i++;
		}
	}

	/* Grab and test the end scripts */
	char endscripts[CONFIG_LIST_MAX][CONFIG_VALUE_MAX];
	memset(endscripts, 0, sizeof(endscripts));
	config_get_gamemode_end_scripts(config, endscripts);

	if (endscripts[0][0] != '\0') {
		int i = 0;
		while (*endscripts[i] != '\0' && i < CONFIG_LIST_MAX) {
			LOG_MSG(":::: Running end script [%s]\n", endscripts[i]);

			int ret = system(endscripts[i]);

			if (ret == 0)
				LOG_MSG(":::: Passed\n");
			else {
				LOG_MSG(":::: Failed!\n");
				scriptstatus = -1;
			}
			i++;
		}
	}

	/* Specal value for no scripts */
	if (endscripts[0][0] == '\0' && startscripts[0][0] == '\0')
		return 1;

	return scriptstatus;
}

int run_gpu_optimisation_tests(struct GameModeConfig *config)
{
	int gpustatus = 0;

	/* First check if these are turned on */
	char apply[CONFIG_VALUE_MAX];
	config_get_apply_gpu_optimisations(config, apply);
	if (strlen(apply) == 0) {
		/* Special value for disabled */
		return 1;
	} else if (strncmp(apply, "accept-responsibility", CONFIG_VALUE_MAX) != 0) {
		LOG_ERROR(
		    "apply_gpu_optimisations set to value other than \"accept-responsibility\" (%s), will "
		    "not apply GPU optimisations!\n",
		    apply);
		return -1;
	}

	/* Get current GPU values */
	GameModeGPUInfo gpuinfo;
	gpuinfo.device = config_get_gpu_device(config);
	gpuinfo.vendor = config_get_gpu_vendor(config);

	if (gpuinfo.vendor == Vendor_NVIDIA)
		gpuinfo.nv_perf_level = config_get_nv_perf_level(config);

	if (game_mode_get_gpu(&gpuinfo) != 0) {
		LOG_ERROR("Could not get current GPU info, see above!\n");
		return -1;
	}

	/* Store the original values */
	long original_core = gpuinfo.core;
	long original_mem = gpuinfo.mem;

	/* Grab the expected values */
	long expected_core = 0;
	long expected_mem = 0;
	switch (gpuinfo.vendor) {
	case Vendor_NVIDIA:
		expected_core = config_get_nv_core_clock_mhz_offset(config);
		expected_mem = config_get_nv_mem_clock_mhz_offset(config);
		break;
	case Vendor_AMD:
		expected_core = config_get_amd_core_clock_percentage(config);
		expected_mem = config_get_amd_mem_clock_percentage(config);
		break;
	default:
		LOG_ERROR("Configured for unsupported GPU vendor 0x%04x!\n", (unsigned int)gpuinfo.vendor);
		return -1;
	}

	LOG_MSG("Configured with vendor:0x%04x device:%ld core:%ld mem:%ld (nv_perf_level:%ld)\n",
	        (unsigned int)gpuinfo.vendor,
	        gpuinfo.device,
	        expected_core,
	        expected_mem,
	        gpuinfo.nv_perf_level);

	/* Start gamemode and check the new values */
	gamemode_request_start();

	if (game_mode_get_gpu(&gpuinfo) != 0) {
		LOG_ERROR("Could not get current GPU info, see above!\n");
		gamemode_request_end();
		return -1;
	}

	if (gpuinfo.core != expected_core || gpuinfo.mem != expected_mem) {
		LOG_ERROR(
		    "Current GPU clocks during gamemode do not match requested values!\n"
		    "\tcore - expected:%ld was:%ld | mem - expected:%ld was:%ld\n",
		    expected_core,
		    gpuinfo.core,
		    expected_mem,
		    gpuinfo.mem);
		gpustatus = -1;
	}

	/* End gamemode and check the values have returned */
	gamemode_request_end();

	if (game_mode_get_gpu(&gpuinfo) != 0) {
		LOG_ERROR("Could not get current GPU info, see above!\n");
		return -1;
	}

	if (gpuinfo.core != original_core || gpuinfo.mem != original_mem) {
		LOG_ERROR(
		    "Current GPU clocks after gamemode do not matcch original values!\n"
		    "\tcore - original:%ld was:%ld | mem - original:%ld was:%ld\n",
		    original_core,
		    gpuinfo.core,
		    original_mem,
		    gpuinfo.mem);
		gpustatus = -1;
	}

	return gpustatus;
}

/**
 * game_mode_run_feature_tests runs a set of tests for each current feature (based on the current
 * config) returns 0 for success, -1 for failure
 */
static int game_mode_run_feature_tests(void)
{
	int status = 0;
	LOG_MSG(":: Feature tests\n");

	/* If we reach here, we should assume the basic requests and register functions are working */

	/* Grab the config */
	/* Note: this config may pick up a local gamemode.ini, or the daemon may have one, we may need
	 * to cope with that */
	GameModeConfig *config = config_create();
	config_init(config);

	/* Does the CPU governor get set properly? */
	{
		LOG_MSG("::: Verifying CPU governor setting\n");

		int cpustatus = run_cpu_governor_tests(config);

		if (cpustatus == 0)
			LOG_MSG("::: Passed\n");
		else {
			LOG_MSG("::: Failed!\n");
			// Consider the CPU governor feature required
			status = 1;
		}
	}

	/* Do custom scripts run? */
	{
		LOG_MSG("::: Verifying Scripts\n");
		int scriptstatus = run_custom_scripts_tests(config);

		if (scriptstatus == 1)
			LOG_MSG("::: Passed (no scripts configured to run)\n");
		else if (scriptstatus == 0)
			LOG_MSG("::: Passed\n");
		else {
			LOG_MSG("::: Failed!\n");
			// Any custom scripts should be expected to work
			status = 1;
		}
	}

	/* Do GPU optimisations get applied? */
	{
		LOG_MSG("::: Verifying GPU Optimisations\n");
		int gpustatus = run_gpu_optimisation_tests(config);

		if (gpustatus == 1)
			LOG_MSG("::: Passed (gpu optimisations not configured to run)\n");
		else if (gpustatus == 0)
			LOG_MSG("::: Passed\n");
		else {
			LOG_MSG("::: Failed!\n");
			// Any custom scripts should be expected to work
			status = 1;
		}
	}

	/* Does the screensaver get inhibited? */
	/* TODO: Unknown if this is testable, org.freedesktop.ScreenSaver has no query method */

	/* Was the process reniced? */
	/* Was the scheduling applied? */
	/* Were io priorities changed? */
	/* Note: These don't get cleared up on un-register, so will have already been applied */
	/* TODO */

	if (status != -1)
		LOG_MSG(":: Passed%s\n\n", status > 0 ? " (with optional failures)" : "");
	else
		LOG_ERROR(":: Failed!\n");

	return status;
}

/**
 * game_mode_run_client_tests runs a set of tests of the client code
 * we simply verify that the client can request the status and recieves the correct results
 *
 * returns 0 for success, -1 for failure
 */
int game_mode_run_client_tests()
{
	int status = 0;
	LOG_MSG(": Running tests\n\n");

	/* Run the basic tests */
	if (run_basic_client_tests() != 0)
		status = -1;

	/* Run the dual client tests */
	if (run_dual_client_tests() != 0)
		status = -1;

	if (status != 0) {
		LOG_MSG(": Client tests failed, skipping feature tests\n");
	} else {
		/* Run the feature tests */
		status = game_mode_run_feature_tests();
	}

	if (status >= 0)
		LOG_MSG(": All Tests Passed%s!\n", status > 0 ? " (with optional failures)" : "");
	else
		LOG_MSG(": Tests Failed!\n");

	return status;
}
