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

#include "gamemode_client.h"

/* Initial verify step to ensure gamemode isn't already active */
static int verify_gamemode_initial(void)
{
	int status = 0;

	if ((status = gamemode_query_status()) != 0 && status != -1) {
		fprintf(
		    stderr,
		    "ERROR: gamemode is currently active, tests require gamemode to start deactivated!\n");
		status = -1;
	} else if (status == -1) {
		fprintf(stderr, "ERROR: gamemode_query_status failed: %s!\n", gamemode_error_string());
		fprintf(stderr, "ERROR: is gamemode installed correctly?\n");
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
			fprintf(stderr, "ERROR: gamemode_query_status failed: %s\n", gamemode_error_string());
		} else if (status == 1) {
			fprintf(stderr,
			        "ERROR: gamemode was active but did not have this process registered\n");
		}
		fprintf(stderr,
		        "ERROR: gamemode failed to activate correctly when requested (expected 2)!\n");
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
			fprintf(stderr, "ERROR: gamemode_query_status failed: %s\n", gamemode_error_string());
		}
		fprintf(stderr, "ERROR: gamemode failed to deactivate when requested (expected 0)!\n");
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
			fprintf(stderr, "ERROR: gamemode_query_status failed: %s\n", gamemode_error_string());
		}
		fprintf(
		    stderr,
		    "ERROR: gamemode_query_status failed to return other client connected (expected 1)!\n");
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
	fprintf(stdout, "   *basic client tests*\n");

	/* First verify that gamemode is not currently active on the system
	 * As well as it being currently installed and queryable
	 */
	if (verify_gamemode_initial() != 0)
		return -1;

	/* Verify that gamemode_request_start correctly start gamemode */
	if (gamemode_request_start() != 0) {
		fprintf(stderr, "ERROR: gamemode_request_start failed: %s\n", gamemode_error_string());
		return -1;
	}

	/* Verify that gamemode is now active and this client is registered*/
	if (verify_active_and_registered() != 0)
		return -1;

	/* Verify that gamemode_request_end corrently de-registers gamemode */
	if (gamemode_request_end() != 0) {
		fprintf(stderr, "ERROR: gamemode_request_end failed: %s!\n", gamemode_error_string());
		return -1;
	}

	/* Verify that gamemode is now innactive */
	if (verify_deactivated() != 0)
		return -1;

	fprintf(stdout, "       *passed*\n");

	return 0;
}

/* Run some dual client tests
 * This also tests that the "-r" argument works correctly and cleans up correctly
 */
static int run_dual_client_tests(void)
{
	int status = 0;

	/* Try running some process interop tests */
	fprintf(stdout, "   *dual clients tests*\n");

	/* Get the current path to this binary */
	char mypath[PATH_MAX];
	if (readlink("/proc/self/exe", mypath, PATH_MAX) == -1) {
		fprintf(stderr, "ERROR: could not read current exe path: %s\n", strerror(errno));
		return -1;
	}

	/* Fork so that the child can request gamemode */
	int child = fork();
	if (child == 0) {
		/* Relaunch self with -r (request and wait for signal) */
		if (execl(mypath, mypath, "-r", (char *)NULL) == -1) {
			fprintf(stderr,
			        "ERROR: failed to re-launch self (%s) with execv: %s\n",
			        mypath,
			        strerror(errno));
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
		fprintf(stderr, "ERROR: gamemode_request_start failed: %s\n", gamemode_error_string());
		status = -1;
	}

	/* Verify that gamemode is now active and this client is registered*/
	if (verify_active_and_registered() != 0)
		status = -1;

	/* Request end of gamemode (de-register ourselves) */
	if (gamemode_request_end() != 0) {
		fprintf(stderr, "ERROR: gamemode_request_end failed: %s!\n", gamemode_error_string());
		status = -1;
	}

	/* Check that when we request gamemode, it replies that the other client is connected */
	if (verify_other_client_connected() != 0)
		status = -1;

	/* Send SIGINT to child to wake it up*/
	if (kill(child, SIGINT) == -1) {
		fprintf(stderr,
		        "ERROR: failed to send continue signal to other client: %s\n",
		        strerror(errno));
		status = -1;
	}

	/* Give the child a chance to finish */
	usleep(10000);

	// Wait for the child to finish up
	int wstatus;
	while (waitpid(child, &wstatus, WNOHANG) == 0) {
		fprintf(stdout, "   Waiting for child to quit...\n");
		usleep(10000);
	}

	/* Verify that gamemode is now innactive */
	if (verify_deactivated() != 0)
		return -1;

	if (status == 0)
		fprintf(stdout, "       *passed*\n");

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
	fprintf(stdout, "Running tests...\n");

	/* Run the basic tests */
	if (run_basic_client_tests() != 0)
		return -1;

	/* Run the dual client tests */
	if (run_dual_client_tests() != 0)
		return -1;

	return status;
}
