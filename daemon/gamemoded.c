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

#include "gamemode.h"
#include "common-logging.h"
#include "gamemode-config.h"

#include "gamemode_client.h"

#include "build-config.h"

#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <sys/stat.h>
#include <systemd/sd-daemon.h> /* TODO: Move usage to gamemode-dbus.c */
#include <unistd.h>

#define USAGE_TEXT                                                                                 \
	"Usage: %s [-d] [-l] [-r] [-t] [-h] [-v]\n\n"                                                  \
	"  -r[PID], --request=[PID] Toggle gamemode for process\n"                                     \
	"                           When no PID given, requests gamemode and pauses\n"                 \
	"  -s[PID], --status=[PID]  Query the status of gamemode for process\n"                        \
	"                           When no PID given, queries the status globally\n"                  \
	"  -d, --daemonize          Daemonize self after launch\n"                                     \
	"  -l, --log-to-syslog      Log to syslog\n"                                                   \
	"  -t, --test               Run tests\n"                                                       \
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
 * Helper to perform standard UNIX daemonization
 */
static void daemonize(const char *name)
{
	/* Initial fork */
	pid_t pid = fork();
	if (pid < 0) {
		FATAL_ERRORNO("Failed to fork");
	}

	if (pid != 0) {
		LOG_MSG("Daemon launched as %s...\n", name);
		exit(EXIT_SUCCESS);
	}

	/* Fork a second time */
	pid = fork();
	if (pid < 0) {
		FATAL_ERRORNO("Failed to fork");
	} else if (pid > 0) {
		exit(EXIT_SUCCESS);
	}

	/* Now continue execution */
	umask(0022);
	if (setsid() < 0) {
		FATAL_ERRORNO("Failed to create process group\n");
	}
	if (chdir("/") < 0) {
		FATAL_ERRORNO("Failed to change to root directory\n");
	}

	/* replace standard file descriptors by /dev/null */
	int devnull_r = open("/dev/null", O_RDONLY);
	int devnull_w = open("/dev/null", O_WRONLY);

	if (devnull_r == -1 || devnull_w == -1) {
		LOG_ERROR("Failed to redirect standard input and output to /dev/null\n");
	} else {
		dup2(devnull_r, STDIN_FILENO);
		dup2(devnull_w, STDOUT_FILENO);
		dup2(devnull_w, STDERR_FILENO);
		close(devnull_r);
		close(devnull_w);
	}
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
		{ "daemonize", no_argument, 0, 'd' },     { "log-to-syslog", no_argument, 0, 'l' },
		{ "request", optional_argument, 0, 'r' }, { "test", no_argument, 0, 't' },
		{ "status", optional_argument, 0, 's' },  { "help", no_argument, 0, 'h' },
		{ "version", no_argument, 0, 'v' },       { NULL, 0, NULL, 0 },
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
			if (optarg != NULL) {
				pid_t pid = atoi(optarg);
				switch (gamemode_query_status_for(pid)) {
				case 0: /* inactive */
					LOG_MSG("gamemode is inactive\n");
					break;
				case 1: /* active not not registered */
					LOG_MSG("gamemode is active but [%d] not registered\n", pid);
					break;
				case 2: /* active for client */
					LOG_MSG("gamemode is active and [%d] registered\n", pid);
					break;
				case -1:
					LOG_ERROR("gamemode_query_status_for(%d) failed: %s\n",
					          pid,
					          gamemode_error_string());
					exit(EXIT_FAILURE);
				default:
					LOG_ERROR("gamemode_query_status returned unexpected value 2\n");
					exit(EXIT_FAILURE);
				}
			} else {
				int ret = 0;
				switch ((ret = gamemode_query_status())) {
				case 0: /* inactive */
					LOG_MSG("gamemode is inactive\n");
					break;
				case 1: /* active */
					LOG_MSG("gamemode is active\n");
					break;
				case -1: /* error */
					LOG_ERROR("gamemode status request failed: %s\n", gamemode_error_string());
					exit(EXIT_FAILURE);
				default: /* unexpected value eg. 2 */
					LOG_ERROR("gamemode_query_status returned unexpected value %d\n", ret);
					exit(EXIT_FAILURE);
				}
			}

			exit(EXIT_SUCCESS);

		case 'r':

			if (optarg != NULL) {
				pid_t pid = atoi(optarg);

				/* toggle gamemode for the process */
				switch (gamemode_query_status_for(pid)) {
				case 0: /* inactive */
				case 1: /* active not not registered */
					LOG_MSG("gamemode not active for client, requesting start for %d...\n", pid);
					if (gamemode_request_start_for(pid) < 0) {
						LOG_ERROR("gamemode_request_start_for(%d) failed: %s\n",
						          pid,
						          gamemode_error_string());
						exit(EXIT_FAILURE);
					}
					LOG_MSG("request succeeded\n");
					break;
				case 2: /* active for client */
					LOG_MSG("gamemode active for client, requesting end for %d...\n", pid);
					if (gamemode_request_end_for(pid) < 0) {
						LOG_ERROR("gamemode_request_end_for(%d) failed: %s\n",
						          pid,
						          gamemode_error_string());
						exit(EXIT_FAILURE);
					}
					LOG_MSG("request succeeded\n");
					break;
				case -1: /* error */
					LOG_ERROR("gamemode_query_status_for(%d) failed: %s\n",
					          pid,
					          gamemode_error_string());
					exit(EXIT_FAILURE);
				}

			} else {
				/* Request gamemode for this process */
				if (gamemode_request_start() < 0) {
					LOG_ERROR("gamemode request failed: %s\n", gamemode_error_string());
					exit(EXIT_FAILURE);
				}

				/* Request and report on the status */
				switch (gamemode_query_status()) {
				case 2: /* active for this client */
					LOG_MSG("gamemode request succeeded and is active\n");
					break;
				case 1: /* active */
					LOG_ERROR("gamemode request succeeded and is active but registration failed\n");
					exit(EXIT_FAILURE);
				case 0: /* inactive */
					LOG_ERROR("gamemode request succeeded but is not active\n");
					exit(EXIT_FAILURE);
				case -1: /* error */
					LOG_ERROR("gamemode_query_status failed: %s\n", gamemode_error_string());
					exit(EXIT_FAILURE);
				}

				/* Simply pause and wait a SIGINT */
				if (signal(SIGINT, sigint_handler_noexit) == SIG_ERR) {
					FATAL_ERRORNO("Could not catch SIGINT");
				}
				pause();

				/* Explicitly clean up */
				if (gamemode_request_end() < 0) {
					LOG_ERROR("gamemode request failed: %s\n", gamemode_error_string());
					exit(EXIT_FAILURE);
				}
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
