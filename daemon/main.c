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
#include "logging.h"

#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#define USAGE_TEXT                                                                                 \
	"Usage: %s [-d] [-l] [-h] [-v]\n\n"                                                            \
	"  -d  daemonize self after launch\n"                                                          \
	"  -l  log to syslog\n"                                                                        \
	"  -h  print this help\n"                                                                      \
	"  -v  print version\n"                                                                        \
	"\n"                                                                                           \
	"See man page for more information.\n"

#define VERSION_TEXT "gamemode version: v" GAMEMODE_VERSION "\n"

static void sigint_handler(__attribute__((unused)) int signo)
{
	LOG_MSG("Quitting by request...\n");

	/* Clean up nicely */
	game_mode_context_destroy(game_mode_context_instance());

	_Exit(EXIT_SUCCESS);
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
	while ((opt = getopt(argc, argv, "dlvh")) != -1) {
		switch (opt) {
		case 'd':
			daemon = true;
			break;
		case 'l':
			use_syslog = true;
			break;
		case 'v':
			fprintf(stdout, VERSION_TEXT);
			exit(EXIT_SUCCESS);
			break;
		case 'h':
			fprintf(stdout, USAGE_TEXT, argv[0]);
			exit(EXIT_SUCCESS);
			break;
		default:
			fprintf(stderr, USAGE_TEXT, argv[0]);
			exit(EXIT_FAILURE);
			break;
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

	/* Set up the game mode context */
	context = game_mode_context_instance();
	game_mode_context_init(context);

	/* Handle quits cleanly */
	if (signal(SIGINT, sigint_handler) == SIG_ERR) {
		FATAL_ERRORNO("Could not catch SIGINT");
	}

	/* Run the main dbus message loop */
	game_mode_context_loop(context);

	game_mode_context_destroy(context);

	/* Log we're finished */
	LOG_MSG("Quitting naturally...\n");
}
