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
#include "external-helper.h"
#include "gamemode_client.h"
#include "governors-query.h"
#include "gpu-control.h"

/* Initial verify step to ensure gamemode isn't already active */
static int verify_gamemode_initial(struct GameModeConfig *config)
{
	int status = 0;

	if ((status = gamemode_query_status()) != 0 && status != -1) {
		long reaper = config_get_reaper_frequency(config);
		LOG_MSG("GameMode was active, waiting for the reaper thread (%ld seconds)!\n", reaper);
		sleep(1);

		/* Try again after waiting */
		for (int i = 0; i < reaper; i++) {
			if ((status = gamemode_query_status()) == 0) {
				status = 0;
				break;
			} else if (status == -1) {
				goto status_error;
			}
			LOG_MSG("Waiting...\n");
			sleep(1);
		}
		if (status == 1)
			LOG_ERROR("GameMode still active, cannot run tests!\n");
	} else if (status == -1) {
		goto status_error;
	} else {
		status = 0;
	}

	return status;
status_error:
	LOG_ERROR("gamemode_query_status failed: %s!\n", gamemode_error_string());
	LOG_ERROR("is gamemode installed correctly?\n");
	return -1;
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

	/* Give the child a chance to request gamemode */
	usleep(10000);

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

/* Check gamemoderun works */
static int run_gamemoderun_and_reaper_tests(struct GameModeConfig *config)
{
	int status = 0;

	LOG_MSG(":: Gamemoderun and reaper thread tests\n");

	/* Fork so that the child can request gamemode */
	int child = fork();
	if (child == 0) {
		/* Close stdout, we don't care if sh prints anything */
		fclose(stdout);
		/* Preload into sh and then kill it */
		if (execl("/usr/bin/gamemoderun", "/usr/bin/gamemoderun", "sh", (char *)NULL) == -1) {
			LOG_ERROR("failed to launch gamemoderun with execl: %s\n", strerror(errno));
			return -1;
		}
	}

	/* Give the child a chance to reqeust gamemode */
	usleep(10000);

	/* Check that when we request gamemode, it replies that the other client is connected */
	if (verify_other_client_connected() != 0)
		status = -1;

	/* Send SIGTERM to the child to stop it*/
	if (kill(child, SIGTERM) == -1) {
		LOG_ERROR("failed to send continue signal to other client: %s\n", strerror(errno));
		status = -1;
	}

	/* Wait for the child to clean up */
	int wstatus;
	while (waitpid(child, &wstatus, WNOHANG) == 0) {
		LOG_MSG("...Waiting for child to quit...\n");
		usleep(100000);
	}

	/* And give gamemode a chance to reap the process */
	long freq = config_get_reaper_frequency(config);
	LOG_MSG("...Waiting for reaper thread (reaper_frequency set to %ld seconds)...\n", freq);
	sleep((unsigned int)freq);

	/* Verify that gamemode is now innactive */
	if (verify_deactivated() != 0)
		return -1;

	if (status == 0)
		LOG_MSG(":: Passed\n\n");

	return status;
}

/* Check the cpu governor setting works */
static int run_cpu_governor_tests(struct GameModeConfig *config)
{
	/* get the two config parameters we care about */
	char desiredgov[CONFIG_VALUE_MAX] = { 0 };
	config_get_desired_governor(config, desiredgov);

	if (desiredgov[0] == '\0')
		strcpy(desiredgov, "performance");

	char defaultgov[CONFIG_VALUE_MAX] = { 0 };

	if (defaultgov[0] == '\0') {
		const char *currentgov = get_gov_state();
		if (currentgov) {
			strncpy(defaultgov, currentgov, CONFIG_VALUE_MAX);
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
		LOG_ERROR("Governor was not set to %s (was actually %s)!\n", desiredgov, currentgov);
		gamemode_request_end();
		return -1;
	}

	/* End gamemode */
	gamemode_request_end();

	/* Verify the governor has been set back */
	currentgov = get_gov_state();
	if (strncmp(currentgov, defaultgov, CONFIG_VALUE_MAX) != 0) {
		LOG_ERROR("Governor was not set back to %s (was actually %s)!\n", defaultgov, currentgov);
		return -1;
	}

	return 0;
}

static int run_custom_scripts_tests(struct GameModeConfig *config)
{
	int scriptstatus = 0;
	long timeout = config_get_script_timeout(config);

	/* Grab and test the start scripts */
	char startscripts[CONFIG_LIST_MAX][CONFIG_VALUE_MAX];
	memset(startscripts, 0, sizeof(startscripts));
	config_get_gamemode_start_scripts(config, startscripts);

	if (startscripts[0][0] != '\0') {
		int i = 0;
		while (*startscripts[i] != '\0' && i < CONFIG_LIST_MAX) {
			LOG_MSG(":::: Running start script [%s]\n", startscripts[i]);

			const char *args[] = { "/bin/sh", "-c", startscripts[i], NULL };
			int ret = run_external_process(args, NULL, (int)timeout);

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

			const char *args[] = { "/bin/sh", "-c", endscripts[i], NULL };
			int ret = run_external_process(args, NULL, (int)timeout);

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
	GameModeGPUInfo *gpuinfo;
	game_mode_initialise_gpu(config, &gpuinfo);

	if (!gpuinfo) {
		LOG_ERROR("Failed to initialise gpuinfo!\n");
		return -1;
	}

	/* Grab the expected values */
	long expected_core = gpuinfo->nv_core;
	long expected_mem = gpuinfo->nv_mem;
	char expected_amd_performance_level[CONFIG_VALUE_MAX];
	strncpy(expected_amd_performance_level, gpuinfo->amd_performance_level, CONFIG_VALUE_MAX);

	/* Get current stats */
	game_mode_get_gpu(gpuinfo);
	long original_nv_core = gpuinfo->nv_core;
	long original_nv_mem = gpuinfo->nv_mem;
	char original_amd_performance_level[CONFIG_VALUE_MAX];
	strncpy(original_amd_performance_level, gpuinfo->amd_performance_level, CONFIG_VALUE_MAX);

	/* Start gamemode and check the new values */
	gamemode_request_start();

	if (game_mode_get_gpu(gpuinfo) != 0) {
		LOG_ERROR("Could not get current GPU info, see above!\n");
		gamemode_request_end();
		game_mode_free_gpu(&gpuinfo);
		return -1;
	}

	if (gpuinfo->vendor == Vendor_NVIDIA &&
	    (gpuinfo->nv_core != expected_core || gpuinfo->nv_mem != expected_mem)) {
		LOG_ERROR(
		    "Current Nvidia GPU clocks during gamemode do not match requested values!\n"
		    "\tnv_core - expected:%ld was:%ld | nv_mem - expected:%ld was:%ld\n",
		    expected_core,
		    gpuinfo->nv_core,
		    expected_mem,
		    gpuinfo->nv_mem);
		gpustatus = -1;
	} else if (gpuinfo->vendor == Vendor_AMD &&
	           strcmp(expected_amd_performance_level, gpuinfo->amd_performance_level) != 0) {
		LOG_ERROR(
		    "Current AMD GPU performance level during gamemode does not match requested value!\n"
		    "\texpected:%s was:%s\n",
		    expected_amd_performance_level,
		    gpuinfo->amd_performance_level);
		gpustatus = -1;
	}

	/* End gamemode and check the values have returned */
	gamemode_request_end();

	if (game_mode_get_gpu(gpuinfo) != 0) {
		LOG_ERROR("Could not get current GPU info, see above!\n");
		game_mode_free_gpu(&gpuinfo);
		return -1;
	}

	if (gpuinfo->vendor == Vendor_NVIDIA &&
	    (gpuinfo->nv_core != original_nv_core || gpuinfo->nv_mem != original_nv_mem)) {
		LOG_ERROR(
		    "Current Nvidia GPU clocks after gamemode do not matcch original values!\n"
		    "\tcore - original:%ld was:%ld | nv_mem - original:%ld was:%ld\n",
		    original_nv_core,
		    gpuinfo->nv_core,
		    original_nv_mem,
		    gpuinfo->nv_mem);
		gpustatus = -1;
	} else if (gpuinfo->vendor == Vendor_AMD &&
	           strcmp(original_amd_performance_level, gpuinfo->amd_performance_level) != 0) {
		LOG_ERROR(
		    "Current AMD GPU performance level after gamemode does not match requested value!\n"
		    "\texpected:%s was:%s\n",
		    original_amd_performance_level,
		    gpuinfo->amd_performance_level);
		gpustatus = -1;
	}

	return gpustatus;
}

/**
 * game_mode_run_feature_tests runs a set of tests for each current feature (based on the current
 * config) returns 0 for success, -1 for failure
 */
static int game_mode_run_feature_tests(struct GameModeConfig *config)
{
	int status = 0;
	LOG_MSG(":: Feature tests\n");

	/* If we reach here, we should assume the basic requests and register functions are working */

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

/* Run a set of tests on the supervisor code */
static int run_supervisor_tests(void)
{
	int supervisortests = 0;
	int ret = 0;

	LOG_MSG(":: Supervisor tests\n");

	/* Launch an external dummy process we can leave running and request gamemode for it */
	pid_t pid = fork();
	if (pid == 0) {
		/* Child simply pauses and exits */
		pause();
		exit(EXIT_SUCCESS);
	}

	/* Request gamemode for our dummy process */
	ret = gamemode_request_start_for(pid);
	if (ret != 0) {
		LOG_ERROR("gamemode_request_start_for gave unexpected value %d, (expected 0)!\n", ret);
		if (ret == -1)
			LOG_ERROR("GameMode error string: %s!\n", gamemode_error_string());
		supervisortests = -1;
	}

	/* Check it's active */
	ret = gamemode_query_status();
	if (ret != 1) {
		LOG_ERROR(
		    "gamemode_query_status after start request gave unexpected value %d, (expected 1)!\n",
		    ret);
		if (ret == -1)
			LOG_ERROR("GameMode error string: %s!\n", gamemode_error_string());
		supervisortests = -1;
	}

	/* Check it's active for the dummy */
	ret = gamemode_query_status_for(pid);
	if (ret != 2) {
		LOG_ERROR(
		    "gamemode_query_status_for after start request gave unexpected value %d, (expected "
		    "2)!\n",
		    ret);
		if (ret == -1)
			LOG_ERROR("GameMode error string: %s!\n", gamemode_error_string());
		supervisortests = -1;
	}

	/* request gamemode end for the client */
	ret = gamemode_request_end_for(pid);
	if (ret != 0) {
		LOG_ERROR("gamemode_request_end_for gave unexpected value %d, (expected 0)!\n", ret);
		if (ret == -1)
			LOG_ERROR("GameMode error string: %s!\n", gamemode_error_string());
		supervisortests = -1;
	}

	/* Verify it's not active */
	ret = gamemode_query_status();
	if (ret != 0) {
		LOG_ERROR(
		    "gamemode_query_status after end request gave unexpected value %d, (expected 0)!\n",
		    ret);
		if (ret == -1)
			LOG_ERROR("GameMode error string: %s!\n", gamemode_error_string());
		supervisortests = -1;
	}

	/* Wake up the child process */
	if (kill(pid, SIGUSR1) == -1) {
		LOG_ERROR("failed to send continue signal to other child process: %s\n", strerror(errno));
		supervisortests = -1;
	}

	// Wait for the child to finish up
	int wstatus;
	usleep(100000);
	while (waitpid(pid, &wstatus, WNOHANG) == 0) {
		LOG_MSG("...Waiting for child to quit...\n");
		usleep(100000);
	}

	if (supervisortests == 0)
		LOG_MSG(":: Passed\n\n");
	else
		LOG_ERROR(":: Failed!\n");

	return supervisortests;
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

	LOG_MSG(": Loading config\n");
	/* Grab the config */
	/* Note: this config may pick up a local gamemode.ini, or the daemon may have one, we may need
	 * to cope with that */
	GameModeConfig *config = config_create();
	config_init(config);

	LOG_MSG(": Running tests\n\n");

	/* First verify that gamemode is not currently active on the system
	 * As well as it being currently installed and queryable
	 */
	if (verify_gamemode_initial(config) != 0)
		return -1;

	/* Controls whether we require a supervisor to actually make requests */
	/* TODO: This effects all tests below */
	if (config_get_require_supervisor(config) != 0) {
		LOG_ERROR("Tests currently unsupported when require_supervisor is set\n");
		return -1;
	}

	/* TODO: Also check blacklist/whitelist values as these may mess up the tests below */

	/* Run the basic tests */
	if (run_basic_client_tests() != 0)
		status = -1;

	/* Run the dual client tests */
	if (run_dual_client_tests() != 0)
		status = -1;

	/* Check gamemoderun and the reaper thread work */
	if (run_gamemoderun_and_reaper_tests(config) != 0)
		status = -1;

	/* Run the supervisor tests */
	if (run_supervisor_tests() != 0)
		status = -1;

	if (status != 0) {
		LOG_MSG(": Client tests failed, skipping feature tests\n");
	} else {
		/* Run the feature tests */
		status = game_mode_run_feature_tests(config);
	}

	if (status >= 0)
		LOG_MSG(": All Tests Passed%s!\n", status > 0 ? " (with optional failures)" : "");
	else
		LOG_MSG(": Tests Failed!\n");

	return status;
}
