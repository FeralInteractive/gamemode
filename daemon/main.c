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

/**
 * Simple daemon to allow user space programs to control the CPU governors
 *
 * The main process is responsible for bootstrapping the D-BUS daemon, caching
 * the initial governor settings, and then responding to requests over D-BUS.
 *
 * Clients register their pid(s) with the service, which are routinely checked
 * to see if they've expired. Once we reach our first actively registered client
 * we put the system into "game mode", i.e. move the CPU governor into a performance
 * mode.
 *
 * Upon exit, or when all clients have stopped running, we put the system back
 * into the default governor policy, which is invariably powersave or similar
 * on laptops. This ensures that the system is obtaining the maximum performance
 * whilst gaming, and allowed to sanely return to idle once the workload is
 * complete.
 */

#define _GNU_SOURCE

#include "config.h"
#include "daemonize.h"
#include "dbus_messaging.h"
#include "gamemode.h"
#include "gamemode_client.h"
#include "logging.h"

#include <getopt.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <systemd/sd-daemon.h>
#include <unistd.h>

#define USAGE_TEXT                                                                                 \
	"Usage: %s [-d] [-l] [-r] [-t] [-h] [-v]\n\n"                                                  \
	"  -r, --request            Request gamemode and pause\n"                                      \
	"  -s, --status             Query the status of gamemode\n"                                    \
	"  -d, --daemonize          Daemonize self after launch\n"                                     \
	"  -l, --log-to-syslog      Log to syslog\n"                                                   \
	"  -r, --test               Run tests\n"                                                       \
	"  -h, --help               Print this help\n"                                                 \
	"  -v, --version            Print version\n"                                                   \
	"\n"                                                                                           \
	"See man page for more information.\n"

#define VERSION_TEXT "gamemode version: v" GAMEMODE_VERSION "\n"

static void sigint_handler(__attribute__((unused)) int signo)
{
	LOG_MSG("Quitting by request...\n");
	sd_notify(0, "STATUS=GameMode is quitting by request...\n");

	/* Clean up nicely */
	game_mode_context_destroy(game_mode_context_instance());

	_Exit(EXIT_SUCCESS);
}

static void sigint_handler_noexit(__attribute__((unused)) int signo)
{
	LOG_MSG("Quitting by request...\n");
}

/**
 * Main bootstrap entry into gamemoded
 */
int main(int argc, char *argv[])
{
	GameModeContext *context = NULL;

	/* Gather command line options */
	bool daemon = false;
	bool use_syslog = false;
	int opt = 0;

	/* Options struct for getopt_long */
	static struct option long_options[] = {
		{ "daemonize", no_argument, 0, 'd' }, { "log-to-syslog", no_argument, 0, 'l' },
		{ "request", no_argument, 0, 'r' },   { "test", no_argument, 0, 't' },
		{ "status", no_argument, 0, 's' },    { "help", no_argument, 0, 'h' },
		{ "version", no_argument, 0, 'v' }
	};
	static const char *short_options = "dls::r::tvh";

	while ((opt = getopt_long(argc, argv, short_options, long_options, 0)) != -1) {
		switch (opt) {
		case 'd':
			daemon = true;
			break;
		case 'l':
			use_syslog = true;
			break;

		case 's':

			switch (gamemode_query_status()) {
			case 0:
				LOG_MSG("gamemode is inactive\n");
				break;
			case 1:
				LOG_MSG("gamemode is active\n");
				break;
			case -1:
				LOG_ERROR("gamemode status request failed: %s\n", gamemode_error_string());
				exit(EXIT_FAILURE);
			default:
				LOG_ERROR("gamemode_query_status returned unexpected value 2\n");
				exit(EXIT_FAILURE);
			}

			exit(EXIT_SUCCESS);

		case 'r':

			if (gamemode_request_start() < 0) {
				LOG_ERROR("gamemode request failed: %s\n", gamemode_error_string());
				exit(EXIT_FAILURE);
			}

			switch (gamemode_query_status()) {
			case 2:
				LOG_MSG("gamemode request succeeded and is active\n");
				break;
			case 1:
				LOG_ERROR("gamemode request succeeded and is active but registration failed\n");
				exit(EXIT_FAILURE);
			case 0:
				LOG_ERROR("gamemode request succeeded but is not active\n");
				exit(EXIT_FAILURE);
			}

			// Simply pause and wait a SIGINT
			if (signal(SIGINT, sigint_handler_noexit) == SIG_ERR) {
				FATAL_ERRORNO("Could not catch SIGINT");
			}
			pause();

			// Explicitly clean up
			if (gamemode_request_end() < 0) {
				LOG_ERROR("gamemode request failed: %s\n", gamemode_error_string());
				exit(EXIT_FAILURE);
			}

			exit(EXIT_SUCCESS);

		case 't': {
			int status = game_mode_run_client_tests();
			exit(status);
		}
		case 'v':
			LOG_MSG(VERSION_TEXT);
			exit(EXIT_SUCCESS);
		case 'h':
			LOG_MSG(USAGE_TEXT, argv[0]);
			exit(EXIT_SUCCESS);
		default:
			fprintf(stderr, USAGE_TEXT, argv[0]);
			exit(EXIT_FAILURE);
		}
	}

	/* If syslog is requested, set it up with our process name */
	if (use_syslog) {
		set_use_syslog(argv[0]);
	}

	/* Daemonize ourselves first if asked */
	if (daemon) {
		daemonize(argv[0]);
	}

	/* Log a version message on startup */
	LOG_MSG("v%s\n", GAMEMODE_VERSION);

	/* Set up the game mode context */
	context = game_mode_context_instance();
	game_mode_context_init(context);

	/* Handle quits cleanly */
	if (signal(SIGINT, sigint_handler) == SIG_ERR) {
		FATAL_ERRORNO("Could not catch SIGINT");
	}
	if (signal(SIGTERM, sigint_handler) == SIG_ERR) {
		FATAL_ERRORNO("Could not catch SIGTERM");
	}

	/* Run the main dbus message loop */
	game_mode_context_loop(context);

	game_mode_context_destroy(context);

	/* Log we're finished */
	LOG_MSG("Quitting naturally...\n");
	sd_notify(0, "STATUS=GameMode is quitting naturally...\n");
}
